# Creates one checksum-addressed GitHub Release asset from a native Release build.
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
            libraries = @(
                [ordered]@{ name = "slang-slim-c-api"; source = "Release/slang-slim-c-api.lib"; file = "slang-slim-c-api.lib" }
                [ordered]@{ name = "slang-compiler"; source = "slang/Release/lib/slang-compiler.lib"; file = "slang-compiler.lib" }
                [ordered]@{ name = "compiler-core"; source = "slang/Release/lib/compiler-core.lib"; file = "compiler-core.lib" }
                [ordered]@{ name = "core"; source = "slang/Release/lib/core.lib"; file = "core.lib" }
                [ordered]@{ name = "miniz"; source = "slang/external/miniz/Release/miniz.lib"; file = "miniz.lib" }
                [ordered]@{ name = "lz4"; source = "slang/external/lz4/build/cmake/Release/lz4.lib"; file = "lz4.lib" }
                [ordered]@{ name = "cmark-gfm"; source = "slang/external/cmark/src/Release/cmark-gfm.lib"; file = "cmark-gfm.lib" }
            )
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
            libraries = @(
                [ordered]@{ name = "slang-slim-c-api"; source = "Release/libslang-slim-c-api.a"; file = "libslang-slim-c-api.a" }
                [ordered]@{ name = "slang-compiler"; source = "slang/Release/lib/libslang-compiler.a"; file = "libslang-compiler.a" }
                [ordered]@{ name = "compiler-core"; source = "slang/Release/lib/libcompiler-core.a"; file = "libcompiler-core.a" }
                [ordered]@{ name = "core"; source = "slang/Release/lib/libcore.a"; file = "libcore.a" }
                [ordered]@{ name = "miniz"; source = "slang/external/miniz/Release/libminiz.a"; file = "libminiz.a" }
                [ordered]@{ name = "lz4"; source = "slang/external/lz4/build/cmake/Release/liblz4.a"; file = "liblz4.a" }
                [ordered]@{ name = "cmark-gfm"; source = "slang/external/cmark/src/Release/libcmark-gfm.a"; file = "libcmark-gfm.a" }
            )
            runtime_libraries = @("c++_static")
            system_libraries = @("dl", "atomic", "m")
            link_arguments = @("-pthread")
        }
    }
}

$buildRoot = Join-Path $repositoryRoot $targetConfig.build_directory
$headerPath = Join-Path $repositoryRoot "native/include/slang_slim.h"
if (-not [IO.File]::Exists($headerPath)) {
    throw "Missing public header: $headerPath"
}

foreach ($library in $targetConfig.libraries) {
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

    Copy-Item -LiteralPath $headerPath -Destination (Join-Path $includeDirectory "slang_slim.h")

    $manifestLibraries = @()
    foreach ($library in $targetConfig.libraries) {
        $sourcePath = Join-Path $buildRoot $library.source
        $destinationPath = Join-Path $libraryDirectory $library.file
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath
        $manifestLibraries += [ordered]@{
            name = $library.name
            path = "lib/$($library.file)"
        }
    }

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
        source_commit = Get-GitValue -WorkingDirectory $repositoryRoot -Arguments @("rev-parse", "HEAD")
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
