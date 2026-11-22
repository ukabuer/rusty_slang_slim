# Builds the ABI probe and its Release library dependencies for one CI target.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("x86_64-pc-windows-msvc", "aarch64-linux-android")]
    [string]$Target
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
& git -C $repositoryRoot submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    throw "Failed to initialize the pinned Slang submodule"
}
& "$PSScriptRoot/verify-slang-submodule.ps1"

Push-Location (Join-Path $repositoryRoot "native")
try {
    if ($Target -eq "x86_64-pc-windows-msvc") {
        & cmake --preset windows-x64 --fresh
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to configure the Windows native build"
        }
        $preset = "windows-x64-abi"
    } else {
        & "$PSScriptRoot/bootstrap-android.ps1"
        $preset = "android-arm64-abi"
    }

    & cmake --build --preset $preset --parallel 2
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build the native libraries and ABI probe for $Target"
    }
} finally {
    Pop-Location
}
