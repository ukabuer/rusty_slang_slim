# Updates the checked-in native-artifacts.json entries from generated release
# archives. This is an explicit maintainer action: the release workflow still
# verifies the resulting index before publishing a tag.
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
$newArtifacts = @()
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
    $checksumFields = (Get-Content -LiteralPath $checksumPath -Raw).Trim() -split "\s+"
    if ($checksumFields.Count -lt 2 -or $checksumFields[1] -ne $archiveName) {
        throw "Checksum sidecar $checksumPath does not name $archiveName"
    }
    if ($checksumFields[0].ToLowerInvariant() -ne $actualHash) {
        throw "Checksum sidecar $checksumPath has $($checksumFields[0]), actual archive has $actualHash"
    }
    $newArtifacts += [ordered]@{
        version = $Version
        target = $target
        archive = $archiveName
        sha256 = $actualHash
    }
}

$updatedArtifacts = @(
    $index.artifacts | Where-Object {
        $_.version -ne $Version -or $_.target -notin $targets
    }
)
$updatedArtifacts += $newArtifacts
$index.artifacts = $updatedArtifacts
$json = ($index | ConvertTo-Json -Depth 8) + "`n"
[IO.File]::WriteAllText(
    $indexPath,
    $json,
    [Text.UTF8Encoding]::new($false)
)

Write-Host "Updated $indexPath for native version $Version"
foreach ($artifact in $newArtifacts) {
    Write-Host "$($artifact.target): $($artifact.archive) ($($artifact.sha256))"
}
