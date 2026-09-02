# Checks the compressed release archive and its uncompressed payload against
# the v0.1 native distribution budgets. Keeping this separate from packaging
# makes the same gate usable in CI and by maintainers before a release.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("x86_64-pc-windows-msvc", "aarch64-linux-android")]
    [string]$Target,

    [Parameter(Mandatory = $true)]
    [string]$PackagePath,

    [int]$MaxArchiveMiB = 0,
    [int]$MaxPayloadMiB = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if (-not [IO.Path]::IsPathRooted($PackagePath)) {
    $PackagePath = Join-Path $repositoryRoot $PackagePath
}
$PackagePath = [IO.Path]::GetFullPath($PackagePath)

$budgets = @{
    "x86_64-pc-windows-msvc" = @{ archive = 40; payload = 150 }
    "aarch64-linux-android" = @{ archive = 30; payload = 90 }
}
$budget = $budgets[$Target]
if ($MaxArchiveMiB -le 0) {
    $MaxArchiveMiB = $budget.archive
}
if ($MaxPayloadMiB -le 0) {
    $MaxPayloadMiB = $budget.payload
}

if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
    throw "Native package does not exist: $PackagePath"
}

$archive = Get-Item -LiteralPath $PackagePath
$maxArchiveBytes = [int64]$MaxArchiveMiB * 1024 * 1024
if ($archive.Length -gt $maxArchiveBytes) {
    throw "Native package $($archive.Name) is $($archive.Length) bytes, above the $MaxArchiveMiB MiB archive budget"
}

$actualArchiveHash = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash.ToLowerInvariant()

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($PackagePath)
try {
    $manifestEntry = $zip.Entries | Where-Object FullName -eq "manifest.json"
    if ($null -eq $manifestEntry) {
        throw "Native package $($archive.Name) has no manifest.json"
    }
    $manifestReader = [IO.StreamReader]::new($manifestEntry.Open())
    try {
        $manifest = $manifestReader.ReadToEnd() | ConvertFrom-Json
    }
    finally {
        $manifestReader.Dispose()
    }

    if ($manifest.target -ne $Target) {
        throw "Package manifest target $($manifest.target) does not match $Target"
    }
    if ($manifest.link.kind -ne "static") {
        throw "Unsupported package link kind $($manifest.link.kind)"
    }

    $expectedLibraryPath = switch ($Target) {
        "x86_64-pc-windows-msvc" { "lib/slang-slim.lib" }
        "aarch64-linux-android" { "lib/libslang-slim.a" }
    }
    $manifestLibraries = @($manifest.link.libraries)
    if ($manifestLibraries.Count -ne 1) {
        throw "Single-library package must list exactly one link library, found $($manifestLibraries.Count)"
    }
    if ($manifestLibraries[0].name -ne "slang-slim" -or
        $manifestLibraries[0].path -ne $expectedLibraryPath) {
        throw "Package manifest library must be slang-slim at $expectedLibraryPath"
    }

    $payloadEntries = @($zip.Entries | Where-Object FullName -ne "manifest.json")
    $libraryEntries = @($payloadEntries | Where-Object FullName -like "lib/*")
    if ($libraryEntries.Count -ne 1 -or $libraryEntries[0].FullName -ne $expectedLibraryPath) {
        throw "Package must contain exactly one static library at $expectedLibraryPath"
    }
    $payloadBytes = [int64](($payloadEntries | Measure-Object -Property Length -Sum).Sum)
    $maxPayloadBytes = [int64]$MaxPayloadMiB * 1024 * 1024
    if ($payloadBytes -gt $maxPayloadBytes) {
        throw "Native package payload is $payloadBytes bytes, above the $MaxPayloadMiB MiB payload budget"
    }

    $manifestFiles = @($manifest.files)
    $entryByPath = @{}
    foreach ($entry in $payloadEntries) {
        $entryByPath[$entry.FullName] = $entry
    }
    foreach ($file in $manifestFiles) {
        if (-not $entryByPath.ContainsKey($file.path)) {
            throw "Manifest file $($file.path) is missing from $($archive.Name)"
        }
        $entry = $entryByPath[$file.path]
        if ([int64]$file.size -ne [int64]$entry.Length) {
            throw "Manifest size mismatch for $($file.path): $($entry.Length) versus $($file.size)"
        }
    }
    if ($manifestFiles.Count -ne $payloadEntries.Count) {
        throw "Manifest lists $($manifestFiles.Count) payload files, archive contains $($payloadEntries.Count)"
    }
}
finally {
    $zip.Dispose()
}

$archiveMiB = [math]::Round($archive.Length / 1MB, 2)
$payloadMiB = [math]::Round($payloadBytes / 1MB, 2)
Write-Host "Native package $($archive.Name): archive=$archiveMiB MiB/$MaxArchiveMiB MiB, payload=$payloadMiB MiB/$MaxPayloadMiB MiB, SHA-256=$actualArchiveHash"
