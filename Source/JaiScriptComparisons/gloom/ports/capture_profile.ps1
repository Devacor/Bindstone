#Requires -RunAsAdministrator
param(
	[int]$Rounds = 3,
	[string]$Workloads = 'gloom,timedemo',
	[string]$BuildRoot = '',
	[string]$JaiWorkloadRoot = '',
	[string]$OutputRoot = '',
	[double]$MaxBackgroundCores = 1.0
)
$ErrorActionPreference = 'Stop'
$comparisonRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $BuildRoot) { $BuildRoot = Join-Path $comparisonRoot 'out\gates-profile\release' }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $comparisonRoot 'out')) + '\'
if (-not $BuildRoot.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Use a build under Source/JaiScriptComparisons/out/.' }
$cache = Get-Content -LiteralPath (Join-Path $BuildRoot 'CMakeCache.txt') -Raw
if ($cache -notmatch 'CMAKE_CXX_FLAGS:[^\r\n]*GLOOM_PROFILE_CAPTURE') { throw 'Build with -ProfileMarkers before capturing.' }
if ($Rounds -lt 3) { throw 'At least three ABAB rounds are required.' }
$state = & wpr -status 2>&1 | Out-String
if ($state -notmatch 'not recording') { throw "Existing WPR recording left untouched: $state" }
if (-not $OutputRoot) { $OutputRoot = Join-Path $comparisonRoot ('out\cpu-captures\' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$output = [IO.Path]::GetFullPath($OutputRoot)
if (-not $output.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Capture output must be under Source/JaiScriptComparisons/out/.' }
if (Test-Path -LiteralPath $output) { throw "Capture output already exists: $output" }
$runnerArgs = @('--build',$BuildRoot,'--output',(Join-Path $output 'runs'),'--engines','jai','--workloads',$Workloads,'--rounds',$Rounds,'--max-background-cores',$MaxBackgroundCores,'--recording','WPR')
if ($JaiWorkloadRoot) { $runnerArgs += @('--jai-workload-root',[IO.Path]::GetFullPath($JaiWorkloadRoot)) }
New-Item -ItemType Directory -Force $output,(Join-Path $output 'temp') | Out-Null
$profile = Join-Path $PSScriptRoot 'gloom_cpu.wprp'
$started = $false
try {
	& wpr -start ($profile + '!GloomCPU') -filemode -recordtempto (Join-Path $output 'temp')
	if ($LASTEXITCODE -ne 0) { throw "WPR start failed: $LASTEXITCODE" }
	$started = $true
	& python -u (Join-Path $PSScriptRoot 'run_comparison.py') @runnerArgs
	if ($LASTEXITCODE -ne 0) { throw 'The whole-program capture gates failed; inspect the saved run logs.' }
} finally {
	if ($started) {
		& wpr -stop (Join-Path $output 'whole-program.etl') 'GLOOM and JaiDOOM warmed ABAB gates with padding controls'
		if ($LASTEXITCODE -ne 0) { Write-Error "WPR stop failed: $LASTEXITCODE" }
	}
}
Write-Output "Captures complete: $output"
