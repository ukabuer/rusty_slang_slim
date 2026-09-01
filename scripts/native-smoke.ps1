# Runs the native C ABI feasibility probe. Windows can execute the probe; the
# Android job is a cross-link smoke test and validates the resulting ARM64 ELF
# with the NDK's readelf because an Android emulator is not part of CI.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("x86_64-pc-windows-msvc", "aarch64-linux-android")]
    [string]$Target,

    [string]$BuildDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = switch ($Target) {
        "x86_64-pc-windows-msvc" { Join-Path $repositoryRoot "build/native/windows-x64" }
        "aarch64-linux-android" { Join-Path $repositoryRoot "build/native/android-arm64" }
    }
}
elseif (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot $BuildDirectory
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)

if (-not (Test-Path -LiteralPath $BuildDirectory -PathType Container)) {
    throw "Native build directory does not exist: $BuildDirectory"
}

$candidates = @(Get-ChildItem -LiteralPath $BuildDirectory -Recurse -File |
    Where-Object {
        $_.Directory.Name -eq "Release" -and
        ($_.Name -eq "slang-slim-abi-feasibility.exe" -or
            $_.Name -eq "slang-slim-abi-feasibility")
    } |
    Sort-Object FullName)
if ($candidates.Count -ne 1) {
    throw "Expected exactly one ABI feasibility binary below $BuildDirectory, found $($candidates.Count)"
}
$binary = $candidates[0].FullName

switch ($Target) {
    "x86_64-pc-windows-msvc" {
        Write-Host "Running Windows native ABI smoke test: $binary"
        & $binary
        if ($LASTEXITCODE -ne 0) {
            throw "Windows native ABI smoke test failed with exit code $LASTEXITCODE"
        }
    }
    "aarch64-linux-android" {
        if ([string]::IsNullOrWhiteSpace($env:ANDROID_NDK_HOME)) {
            throw "ANDROID_NDK_HOME is required for Android ELF validation"
        }
        $readelf = Join-Path $env:ANDROID_NDK_HOME `
            "toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe"
        if (-not (Test-Path -LiteralPath $readelf -PathType Leaf)) {
            throw "NDK llvm-readelf was not found at $readelf"
        }
        Write-Host "Inspecting Android ARM64 native ABI binary: $binary"
        $headers = & $readelf -h $binary 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "llvm-readelf failed for $binary"
        }
        $headerText = $headers -join "`n"
        if ($headerText -notmatch "Class:\s+ELF64") {
            throw "Android ABI smoke binary is not ELF64: $binary"
        }
        if ($headerText -notmatch "Machine:\s+AArch64") {
            throw "Android ABI smoke binary is not AArch64: $binary"
        }
        Write-Host "Android ARM64 ELF smoke check passed"
    }
}
