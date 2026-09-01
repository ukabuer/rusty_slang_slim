# Verifies that the checked-out Slang tree is the revision pinned by this
# repository. The superproject gitlink is checked as well as the worktree so a
# CI checkout cannot silently build a different Slang revision.
[CmdletBinding()]
param(
    [string]$ExpectedCommit = "ab5db6cf5c645a816894db670dacd322ec59d3ac",
    [string]$ExpectedTag = "v2026.16.1"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$submoduleRelativePath = "third_party/slang"
$submodulePath = Join-Path $repositoryRoot $submoduleRelativePath

function Invoke-GitValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $safeDirectory = $WorkingDirectory.Replace('\', '/')
    $value = & git -c "safe.directory=$safeDirectory" -C $WorkingDirectory @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "git -C $WorkingDirectory $($Arguments -join ' ') failed: $($value -join ' ')"
    }
    return ($value -join "`n").Trim()
}

if (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot ".gitmodules"))) {
    throw "Missing .gitmodules in $repositoryRoot"
}
if (-not (Test-Path -LiteralPath $submodulePath -PathType Container)) {
    throw "Slang submodule is not initialized at $submodulePath"
}
if (-not (Test-Path -LiteralPath (Join-Path $submodulePath "CMakeLists.txt"))) {
    throw "Slang submodule at $submodulePath is incomplete"
}

$configuredUrl = Invoke-GitValue $repositoryRoot @(
    "config",
    "-f",
    ".gitmodules",
    "--get",
    "submodule.$submoduleRelativePath.url"
)
if ($configuredUrl -ne "https://github.com/shader-slang/slang.git") {
    throw "Unexpected Slang submodule URL $configuredUrl"
}

$treeLine = Invoke-GitValue $repositoryRoot @("ls-tree", "HEAD", "--", $submoduleRelativePath)
$treeFields = $treeLine -split "\s+", 4
if ($treeFields.Count -lt 3 -or $treeFields[1] -ne "commit") {
    throw "HEAD does not contain a gitlink for $submoduleRelativePath"
}
$gitlinkCommit = $treeFields[2].ToLowerInvariant()
$actualCommit = (Invoke-GitValue $submodulePath @("rev-parse", "HEAD")).ToLowerInvariant()
$expectedCommit = $ExpectedCommit.ToLowerInvariant()

if ($gitlinkCommit -ne $expectedCommit) {
    throw "The repository gitlink pins Slang to $gitlinkCommit, expected $ExpectedCommit"
}
if ($actualCommit -ne $expectedCommit) {
    throw "The checked-out Slang revision is $actualCommit, expected $ExpectedCommit"
}

$dirty = Invoke-GitValue $submodulePath @("status", "--porcelain")
if (-not [string]::IsNullOrWhiteSpace($dirty)) {
    throw "The Slang submodule has local changes:`n$dirty"
}

$tagOutput = Invoke-GitValue $submodulePath @("tag", "--points-at", "HEAD")
$tags = @($tagOutput -split "`n" |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($tags.Count -gt 0 -and $tags -notcontains $ExpectedTag) {
    throw "Slang revision $ExpectedCommit has tags $($tags -join ', '), expected $ExpectedTag"
}
if ($tags.Count -eq 0) {
    Write-Warning "No local tag was fetched for the pinned Slang revision; commit pin was verified"
}

Write-Host "Verified Slang submodule $ExpectedTag @ $ExpectedCommit"
