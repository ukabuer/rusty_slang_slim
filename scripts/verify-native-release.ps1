# Verifies that generated native archives match the checked-in artifact index.
# A release tag must update native-artifacts.json before it can be published;
# this prevents crates.io consumers from downloading an asset with the wrong
# checksum.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$')]
    [string]$Version,

    [string]$PackageDirectory = "build/packages"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if (-not [IO.Path]::IsPathRooted($PackageDirectory)) {
    $PackageDirectory = Join-Path $repositoryRoot $PackageDirectory
}
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
$indexPath = Join-Path $repositoryRoot "crates/slang-slim-sys/native-artifacts.json"
$index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json

$targets = @(
    "x86_64-pc-windows-msvc",
    "aarch64-linux-android"
)
foreach ($target in $targets) {
    $archiveName = "slang-slim-native-v$Version-$target.zip"
    $archivePath = Join-Path $PackageDirectory $archiveName
    $checksumPath = "$archivePath.sha256"
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        throw "Missing generated release archive: $archivePath"
    }
    if (-not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) {
        throw "Missing generated release checksum: $checksumPath"
    }

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumHash = ((Get-Content -LiteralPath $checksumPath -Raw).Trim() -split "\s+")[0].ToLowerInvariant()
    if ($checksumHash -ne $actualHash) {
        throw "Checksum sidecar for $archiveName has $checksumHash, actual archive has $actualHash"
    }

    $entry = @($index.artifacts | Where-Object {
        $_.version -eq $Version -and $_.target -eq $target
    })
    if ($entry.Count -ne 1) {
        throw "native-artifacts.json must contain exactly one $Version/$target entry"
    }
    if ($entry[0].archive -ne $archiveName) {
        throw "Index archive name $($entry[0].archive) does not match $archiveName"
    }
    if ($entry[0].sha256.ToLowerInvariant() -ne $actualHash) {
        throw "Index SHA-256 for $archiveName is $($entry[0].sha256), actual archive has $actualHash"
    }
    Write-Host "Verified indexed release asset $archiveName ($actualHash)"
}
