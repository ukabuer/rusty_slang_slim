# Bootstraps Slang host generators and the Android ARM64 cross-build configuration.
[CmdletBinding()]
param(
    [string]$AndroidNdkHome
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$nativeSource = Join-Path $repositoryRoot "native"
$windowsBuild = Join-Path $repositoryRoot "build/native/windows-x64"
$hostTools = Join-Path $repositoryRoot "build/native/host-tools"
$pinnedNdkRevision = "27.3.13750724"

function Invoke-CheckedCMake {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$CMakeArguments
    )

    & cmake @CMakeArguments
    if ($LASTEXITCODE -ne 0) {
        throw "cmake $($CMakeArguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake 3.25 or newer is required"
}

if ([string]::IsNullOrWhiteSpace($AndroidNdkHome)) {
    $AndroidNdkHome = $env:ANDROID_NDK_HOME
}
if ([string]::IsNullOrWhiteSpace($AndroidNdkHome)) {
    $sdkNdk = Join-Path $env:LOCALAPPDATA "Android/Sdk/ndk/$pinnedNdkRevision"
    $workspaceNdk = Join-Path $repositoryRoot "build/toolchains/android-ndk-r27d"
    if (Test-Path $sdkNdk) {
        $AndroidNdkHome = $sdkNdk
    }
    elseif (Test-Path $workspaceNdk) {
        $AndroidNdkHome = $workspaceNdk
    }
}
if ([string]::IsNullOrWhiteSpace($AndroidNdkHome)) {
    throw "Android NDK r27d ($pinnedNdkRevision) was not found; pass -AndroidNdkHome or set ANDROID_NDK_HOME"
}

$AndroidNdkHome = (Resolve-Path $AndroidNdkHome).Path
$sourceProperties = Join-Path $AndroidNdkHome "source.properties"
if (-not (Test-Path $sourceProperties)) {
    throw "Invalid Android NDK path: $AndroidNdkHome"
}
$revisionLine = Select-String -Path $sourceProperties -Pattern '^Pkg\.Revision\s*=\s*(.+)$'
if (-not $revisionLine -or $revisionLine.Matches[0].Groups[1].Value.Trim() -ne $pinnedNdkRevision) {
    throw "Expected Android NDK $pinnedNdkRevision at $AndroidNdkHome"
}

$ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue
if (-not $ninja) {
    $visualStudioEditions = @("Community", "Professional", "Enterprise", "BuildTools")
    foreach ($edition in $visualStudioEditions) {
        $candidate = Join-Path $env:ProgramFiles "Microsoft Visual Studio/2022/$edition/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
        if (Test-Path $candidate) {
            $env:Path = "$(Split-Path -Parent $candidate);$env:Path"
            $ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue
            break
        }
    }
}
if (-not $ninja) {
    throw "Ninja is required for the Android cross build"
}

Push-Location $nativeSource
try {
    Invoke-CheckedCMake -CMakeArguments @("--preset", "windows-x64", "--fresh")
    Invoke-CheckedCMake -CMakeArguments @("--build", "--preset", "windows-x64-generators")
    Invoke-CheckedCMake -CMakeArguments @(
        "--install", $windowsBuild,
        "--config", "Release",
        "--prefix", $hostTools,
        "--component", "generators"
    )

    $env:ANDROID_NDK_HOME = $AndroidNdkHome
    Invoke-CheckedCMake -CMakeArguments @("--preset", "android-arm64", "--fresh")
}
finally {
    Pop-Location
}

Write-Host "Native baselines configured successfully."
Write-Host "Windows: $windowsBuild"
Write-Host "Android: $(Join-Path $repositoryRoot 'build/native/android-arm64')"
