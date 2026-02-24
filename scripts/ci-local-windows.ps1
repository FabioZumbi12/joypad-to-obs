[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [switch] $CleanBuild,
    [switch] $CleanDeps,
    [switch] $CleanRelease,
    [switch] $Package = $true,
    [switch] $DebugLogs
)

$ErrorActionPreference = 'Stop'

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    throw 'Use PowerShell 7.2+ to match CI scripts.'
}

$ProjectRoot = Resolve-Path -Path "$PSScriptRoot/.."
$BuildScript = Join-Path $ProjectRoot '.github/scripts/Build-Windows.ps1'
$PackageScript = Join-Path $ProjectRoot '.github/scripts/Package-Windows.ps1'
$BuildDir = Join-Path $ProjectRoot "build_${Target}"
$DepsDir = Join-Path $ProjectRoot '.deps'
$ReleaseDir = Join-Path $ProjectRoot 'release'

if ( -not (Test-Path $BuildScript) ) {
    throw "Build script not found: $BuildScript"
}
if ( $Package -and -not (Test-Path $PackageScript) ) {
    throw "Package script not found: $PackageScript"
}

if ( $CleanBuild -and (Test-Path $BuildDir) ) {
    Write-Host "Cleaning build directory: $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

if ( $CleanDeps -and (Test-Path $DepsDir) ) {
    Write-Host "Cleaning dependencies directory: $DepsDir"
    Remove-Item -Recurse -Force $DepsDir
}

if ( $CleanRelease -and (Test-Path $ReleaseDir) ) {
    Write-Host "Cleaning release directory: $ReleaseDir"
    Remove-Item -Recurse -Force $ReleaseDir
}

# Required by upstream CI scripts.
$env:CI = 'true'

if ( $DebugLogs ) {
    $env:RUNNER_DEBUG = '1'
} else {
    Remove-Item Env:RUNNER_DEBUG -ErrorAction SilentlyContinue
}

Write-Host "Simulating GitHub Actions Windows build"
Write-Host "  Target: $Target"
Write-Host "  Configuration: $Configuration"
Write-Host "  Package: $Package"
Write-Host "  CI: $env:CI"

Push-Location $ProjectRoot
try {
    $BuildArgs = @(
        '-NoProfile'
        '-ExecutionPolicy'
        'Bypass'
        '-File'
        $BuildScript
        '-Target'
        $Target
        '-Configuration'
        $Configuration
    )
    & pwsh @BuildArgs
    if ( $LASTEXITCODE -ne 0 ) {
        throw "Build script failed with exit code $LASTEXITCODE"
    }

    if ( $Package ) {
        $PackageArgs = @(
            '-NoProfile'
            '-ExecutionPolicy'
            'Bypass'
            '-File'
            $PackageScript
            '-Target'
            $Target
            '-Configuration'
            $Configuration
        )
        & pwsh @PackageArgs
        if ( $LASTEXITCODE -ne 0 ) {
            throw "Package script failed with exit code $LASTEXITCODE"
        }
    }
}
finally {
    Pop-Location
}

Write-Host 'Local CI simulation completed successfully.'
