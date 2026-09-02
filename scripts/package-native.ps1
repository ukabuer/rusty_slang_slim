# Creates one deterministic GitHub Release archive and its checksum sidecar
# from a native Release build.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("x86_64-pc-windows-msvc", "aarch64-linux-android")]
    [string]$Target,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$')]
    [string]$Version,

    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot "build/packages"
}
elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

function Get-GitValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $safeDirectory = $WorkingDirectory.Replace('\', '/')
    $value = & git -c "safe.directory=$safeDirectory" -C $WorkingDirectory @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "git -C $WorkingDirectory $($Arguments -join ' ') failed"
    }
    return ($value -join "`n").Trim()
}

function Get-RelativePackagePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package input $fullPath is outside $Root"
    }
    return $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
}

function Resolve-WindowsArchiver {
    $command = Get-Command lib.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) {
        return $command.Path
    }

    $candidatePaths = @()
    if (-not [string]::IsNullOrWhiteSpace($env:VCToolsInstallDir)) {
        $candidatePaths += Join-Path $env:VCToolsInstallDir "bin/Hostx64/x64/lib.exe"
    }
    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        $candidatePaths += Join-Path $env:VSINSTALLDIR "VC/Tools/MSVC"
    }

    foreach ($candidate in $candidatePaths) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            $tool = Get-ChildItem -LiteralPath $candidate -Recurse -File -Filter lib.exe |
                Where-Object { $_.FullName -match '[\\/]bin[\\/]Hostx64[\\/]x64[\\/]lib\.exe$' } |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if ($null -ne $tool) {
                return $tool.FullName
            }
        }
    }

    $vswhere = $null
    if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles(x86)})) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
    }
    if ($null -ne $vswhere -and (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        $installationPath = (& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null | Select-Object -First 1)
        if (-not [string]::IsNullOrWhiteSpace($installationPath)) {
            $tool = Get-ChildItem -LiteralPath (Join-Path $installationPath "VC/Tools/MSVC") `
                -Recurse -File -Filter lib.exe |
                Where-Object { $_.FullName -match '[\\/]bin[\\/]Hostx64[\\/]x64[\\/]lib\.exe$' } |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if ($null -ne $tool) {
                return $tool.FullName
            }
        }
    }

    throw "MSVC lib.exe was not found; run this script from a VS developer shell or install the MSVC x64 toolset"
}

function Resolve-AndroidArchiver {
    $command = Get-Command llvm-ar.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) {
        return $command.Path
    }

    $ndkRoot = $env:ANDROID_NDK_HOME
    if ([string]::IsNullOrWhiteSpace($ndkRoot)) {
        $ndkRoot = $env:ANDROID_NDK_ROOT
    }
    if ([string]::IsNullOrWhiteSpace($ndkRoot)) {
        throw "ANDROID_NDK_HOME is required to locate the Android llvm-ar archiver"
    }

    $prebuiltNames = @("windows-x86_64", "linux-x86_64", "darwin-x86_64", "darwin-arm64")
    foreach ($prebuiltName in $prebuiltNames) {
        $candidate = Join-Path $ndkRoot "toolchains/llvm/prebuilt/$prebuiltName/bin/llvm-ar.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
        $candidate = Join-Path $ndkRoot "toolchains/llvm/prebuilt/$prebuiltName/bin/llvm-ar"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }

    throw "Android llvm-ar was not found below $ndkRoot"
}

function Merge-StaticLibraries {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("x86_64-pc-windows-msvc", "aarch64-linux-android")]
        [string]$Target,

        [Parameter(Mandatory = $true)]
        [string[]]$InputPaths,

        [Parameter(Mandatory = $true)]
        [string]$OutputPath
    )

    $outputDirectory = Split-Path -Parent $OutputPath
    [IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
    if ([IO.File]::Exists($OutputPath)) {
        [IO.File]::Delete($OutputPath)
    }

    if ($Target -eq "x86_64-pc-windows-msvc") {
        $archiver = Resolve-WindowsArchiver
        $arguments = @("/nologo", "/OUT:$OutputPath") + $InputPaths
        & $archiver @arguments
    }
    else {
        $archiver = Resolve-AndroidArchiver
        # L asks llvm-ar to add the contents of each input archive rather than
        # nesting the archives as members. D makes the archive metadata
        # deterministic; it is also llvm-ar's default but is explicit here.
        & $archiver qcsDL $OutputPath @InputPaths
    }

    if ($LASTEXITCODE -ne 0 -or -not [IO.File]::Exists($OutputPath)) {
        throw "Failed to merge static libraries into $OutputPath"
    }

    if ($Target -eq "x86_64-pc-windows-msvc") {
        # MSVC lib.exe stamps each COFF archive member with the current time.
        # Normalize those headers so rebuilding the same inputs produces the
        # same release SHA-256 on local and CI machines.
        Normalize-CoffArchiveTimestamps -Path $OutputPath
    }
    Write-Host "Merged $($InputPaths.Count) static libraries into $OutputPath"
}

function Normalize-CoffArchiveTimestamps {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 8 -or
        [Text.Encoding]::ASCII.GetString($bytes, 0, 8) -ne "!<arch>`n") {
        throw "Unsupported COFF archive format: $Path"
    }

    $offset = 8
    while ($offset -lt $bytes.Length) {
        if ($offset + 60 -gt $bytes.Length -or
            $bytes[$offset + 58] -ne 0x60 -or
            $bytes[$offset + 59] -ne 0x0a) {
            throw "Malformed COFF archive member header at byte $offset in $Path"
        }

        for ($index = 0; $index -lt 12; $index++) {
            $bytes[$offset + 16 + $index] = [byte][char]'0'
        }

        $sizeText = [Text.Encoding]::ASCII.GetString($bytes, $offset + 48, 10).Trim()
        try {
            $memberSize = [int64]::Parse(
                $sizeText,
                [Globalization.NumberStyles]::Integer,
                [Globalization.CultureInfo]::InvariantCulture)
        }
        catch {
            throw "Malformed COFF archive member size '$sizeText' at byte $offset in $Path"
        }
        $memberEnd = $offset + 60 + $memberSize
        if ($memberSize -lt 0 -or $memberEnd -gt $bytes.Length) {
            throw "COFF archive member at byte $offset extends past the end of $Path"
        }
        $offset = $memberEnd + ($memberSize % 2)
    }

    [IO.File]::WriteAllBytes($Path, $bytes)
}

function New-DeterministicZip {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDirectory,

        [Parameter(Mandatory = $true)]
        [string]$ArchivePath
    )

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem

    if ([IO.File]::Exists($ArchivePath)) {
        [IO.File]::Delete($ArchivePath)
    }

    $archiveStream = [IO.File]::Open(
        $ArchivePath,
        [IO.FileMode]::CreateNew,
        [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::None
    )
    try {
        $archive = [IO.Compression.ZipArchive]::new(
            $archiveStream,
            [IO.Compression.ZipArchiveMode]::Create,
            $false
        )
        try {
            $fixedTimestamp = [DateTimeOffset]::new(
                1980,
                1,
                1,
                0,
                0,
                0,
                [TimeSpan]::Zero
            )
            $files = Get-ChildItem -LiteralPath $SourceDirectory -Recurse -File |
                Sort-Object FullName
            foreach ($file in $files) {
                $relativePath = Get-RelativePackagePath `
                    -Root $SourceDirectory `
                    -Path $file.FullName
                $entry = $archive.CreateEntry(
                    $relativePath,
                    [IO.Compression.CompressionLevel]::Optimal
                )
                $entry.LastWriteTime = $fixedTimestamp

                $inputStream = $file.OpenRead()
                try {
                    $outputStream = $entry.Open()
                    try {
                        $inputStream.CopyTo($outputStream)
                    }
                    finally {
                        $outputStream.Dispose()
                    }
                }
                finally {
                    $inputStream.Dispose()
                }
            }
        }
        finally {
            $archive.Dispose()
        }
    }
    finally {
        $archiveStream.Dispose()
    }
}

$targetConfig = switch ($Target) {
    "x86_64-pc-windows-msvc" {
        [ordered]@{
            build_directory = "build/native/windows-x64"
            build_preset = "windows-x64-release"
            input_libraries = @(
                [ordered]@{ name = "slang-slim-c-api"; source = "Release/slang-slim-c-api.lib"; file = "slang-slim-c-api.lib" }
                [ordered]@{ name = "slang-compiler"; source = "slang/Release/lib/slang-compiler.lib"; file = "slang-compiler.lib" }
                [ordered]@{ name = "compiler-core"; source = "slang/Release/lib/compiler-core.lib"; file = "compiler-core.lib" }
                [ordered]@{ name = "core"; source = "slang/Release/lib/core.lib"; file = "core.lib" }
                [ordered]@{ name = "miniz"; source = "slang/external/miniz/Release/miniz.lib"; file = "miniz.lib" }
                [ordered]@{ name = "lz4"; source = "slang/external/lz4/build/cmake/Release/lz4.lib"; file = "lz4.lib" }
                [ordered]@{ name = "cmark-gfm"; source = "slang/external/cmark/src/Release/cmark-gfm.lib"; file = "cmark-gfm.lib" }
            )
            output_library = [ordered]@{ name = "slang-slim"; file = "slang-slim.lib" }
            runtime_libraries = @()
            system_libraries = @(
                "kernel32",
                "user32",
                "gdi32",
                "winspool",
                "shell32",
                "ole32",
                "oleaut32",
                "uuid",
                "comdlg32",
                "advapi32"
            )
            link_arguments = @()
        }
    }
    "aarch64-linux-android" {
        [ordered]@{
            build_directory = "build/native/android-arm64"
            build_preset = "android-arm64-release"
            input_libraries = @(
                [ordered]@{ name = "slang-slim-c-api"; source = "Release/libslang-slim-c-api.a"; file = "libslang-slim-c-api.a" }
                [ordered]@{ name = "slang-compiler"; source = "slang/Release/lib/libslang-compiler.a"; file = "libslang-compiler.a" }
                [ordered]@{ name = "compiler-core"; source = "slang/Release/lib/libcompiler-core.a"; file = "libcompiler-core.a" }
                [ordered]@{ name = "core"; source = "slang/Release/lib/libcore.a"; file = "libcore.a" }
                [ordered]@{ name = "miniz"; source = "slang/external/miniz/Release/libminiz.a"; file = "libminiz.a" }
                [ordered]@{ name = "lz4"; source = "slang/external/lz4/build/cmake/Release/liblz4.a"; file = "liblz4.a" }
                [ordered]@{ name = "cmark-gfm"; source = "slang/external/cmark/src/Release/libcmark-gfm.a"; file = "libcmark-gfm.a" }
            )
            output_library = [ordered]@{ name = "slang-slim"; file = "libslang-slim.a" }
            runtime_libraries = @("c++_static")
            system_libraries = @("dl", "atomic", "m")
            link_arguments = @("-pthread")
        }
    }
}

$buildRoot = Join-Path $repositoryRoot $targetConfig.build_directory
$headerPath = Join-Path $repositoryRoot "native/include/slang_c_api.h"
if (-not [IO.File]::Exists($headerPath)) {
    throw "Missing public header: $headerPath"
}

foreach ($library in $targetConfig.input_libraries) {
    $sourcePath = Join-Path $buildRoot $library.source
    if (-not [IO.File]::Exists($sourcePath)) {
        throw "Missing $sourcePath; build it with: cmake --build --preset $($targetConfig.build_preset)"
    }
}

[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$assetStem = "slang-slim-native-v$Version-$Target"
$assetPath = Join-Path $OutputDirectory "$assetStem.zip"
$checksumPath = "$assetPath.sha256"
$stagingRoot = Join-Path $OutputDirectory ".staging-$assetStem-$([Guid]::NewGuid().ToString('N'))"
$stagingRoot = [IO.Path]::GetFullPath($stagingRoot)
$outputPrefix = $OutputDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $stagingRoot.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create staging directory outside $OutputDirectory"
}

try {
    $includeDirectory = Join-Path $stagingRoot "include"
    $libraryDirectory = Join-Path $stagingRoot "lib"
    [IO.Directory]::CreateDirectory($includeDirectory) | Out-Null
    [IO.Directory]::CreateDirectory($libraryDirectory) | Out-Null

    Copy-Item -LiteralPath $headerPath -Destination (Join-Path $includeDirectory "slang_c_api.h")

    $inputPaths = @(
        $targetConfig.input_libraries | ForEach-Object {
            Join-Path $buildRoot $_.source
        }
    )
    $mergedLibraryPath = Join-Path $libraryDirectory $targetConfig.output_library.file
    Merge-StaticLibraries -Target $Target -InputPaths $inputPaths -OutputPath $mergedLibraryPath
    $manifestLibraries = @([ordered]@{
        name = $targetConfig.output_library.name
        path = "lib/$($targetConfig.output_library.file)"
    })

    $fileManifest = @()
    $payloadFiles = Get-ChildItem -LiteralPath $stagingRoot -Recurse -File |
        Sort-Object FullName
    foreach ($file in $payloadFiles) {
        $relativePath = Get-RelativePackagePath -Root $stagingRoot -Path $file.FullName
        $fileManifest += [ordered]@{
            path = $relativePath
            size = $file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }

    $slangSource = Join-Path $repositoryRoot "third_party/slang"
    $manifest = [ordered]@{
        schema_version = 1
        package = "slang-slim-native"
        version = $Version
        abi_version = 1
        target = $Target
        # Keep the package hash independent of the root repository commit.
        # Embedding HEAD would make a release package change when only release
        # metadata changes.
        slang_commit = Get-GitValue -WorkingDirectory $slangSource -Arguments @("rev-parse", "HEAD")
        link = [ordered]@{
            kind = "static"
            search_path = "lib"
            libraries = $manifestLibraries
            runtime_libraries = @($targetConfig.runtime_libraries)
            system_libraries = @($targetConfig.system_libraries)
            arguments = @($targetConfig.link_arguments)
        }
        files = $fileManifest
    }

    $manifestJson = ($manifest | ConvertTo-Json -Depth 8) + "`n"
    [IO.File]::WriteAllText(
        (Join-Path $stagingRoot "manifest.json"),
        $manifestJson,
        [Text.UTF8Encoding]::new($false)
    )

    New-DeterministicZip -SourceDirectory $stagingRoot -ArchivePath $assetPath
    $assetHash = (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText(
        $checksumPath,
        "$assetHash  $([IO.Path]::GetFileName($assetPath))`n",
        [Text.UTF8Encoding]::new($false)
    )

    $asset = Get-Item -LiteralPath $assetPath
    Write-Host "Created $assetPath"
    Write-Host "Size: $($asset.Length) bytes"
    Write-Host "SHA-256: $assetHash"
}
finally {
    if ([IO.Directory]::Exists($stagingRoot)) {
        if (-not $stagingRoot.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove staging directory outside $OutputDirectory"
        }
        [IO.Directory]::Delete($stagingRoot, $true)
    }
}
