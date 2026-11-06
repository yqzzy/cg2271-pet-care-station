param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$Ide = 'C:\nxp\MCUXpressoIDE_25.6.136\ide\mcuxpressoidec.exe'
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Ide -PathType Leaf)) {
    throw "MCUXpresso console launcher missing. Pass -Ide with the full path to mcuxpressoidec.exe."
}
$project = Join-Path $PSScriptRoot 'pet-station'
$workspace = Join-Path $PSScriptRoot 'build/headless-workspace'
& $Ide -nosplash --launcher.suppressErrors -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data $workspace -import $project -cleanBuild "pet-station/$Configuration"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (!(Test-Path -LiteralPath (Join-Path $project "$Configuration/pet-station.axf"))) {
    throw "Build ended without producing pet-station.axf."
}
