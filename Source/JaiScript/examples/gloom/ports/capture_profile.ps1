#Requires -RunAsAdministrator
param(
	[int]$Runs = 5
)
$ErrorActionPreference = 'Stop'
$jaiRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$output = Join-Path $jaiRoot 'out\profile-2026-09'
$binary = Join-Path $jaiRoot 'out\astra\profile\bin\jai_gloom.exe'
$profile = Join-Path $PSScriptRoot 'gloom_cpu.wprp'
if (-not (Test-Path -LiteralPath $binary)) { throw "Missing prepared profile binary: $binary" }
New-Item -ItemType Directory -Force -Path $output,(Join-Path $output 'temp') | Out-Null
$state = & wpr -status 2>&1 | Out-String
if ($state -notmatch 'not recording') {
	throw "Another WPR recording may be active. It will not be stopped. Status: $state"
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$manifest = [System.Collections.Generic.List[object]]::new()
foreach ($workers in @(0,8)) {
	$mode = if ($workers -eq 0) { 'serial' } else { 'workers8' }
	$trace = Join-Path $output "$stamp-$mode.etl"
	$started = $false
	try {
		& wpr -start ($profile + '!GloomCPU') -filemode -recordtempto (Join-Path $output 'temp')
		if ($LASTEXITCODE -ne 0) { throw "WPR start failed: $LASTEXITCODE" }
		$started = $true
		for ($run = 1; $run -le $Runs; $run++) {
			while (@(Get-Process cl,link,ninja,MSBuild,jaiscript_tests,jai_gloom -ErrorAction SilentlyContinue).Count -gt 0) {
				Write-Host 'Waiting for another compiler, test or GLOOM process to finish...'
				Start-Sleep -Seconds 2
			}
			$log = Join-Path $output "$stamp-$mode-$run.log"
			$err = Join-Path $output "$stamp-$mode-$run.err"
			$arguments = @('--smoke','--backend','vm','--workers',"$workers",'--ticks','600','--warmup','600','--seed','666')
			$process = Start-Process -FilePath $binary -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError $err
			$mask = if ($workers -eq 0) { [IntPtr]1 } else { [IntPtr]0xC03C03 }
			$process.ProcessorAffinity = $mask
			$process.WaitForExit()
			$process.Refresh()
			$text = Get-Content -LiteralPath $log -Raw
			$ok = $process.ExitCode -eq 0 -and $text -match 'dfd969ed5a336dca\s+\|\s+1503537018'
			$manifest.Add([pscustomobject]@{mode=$mode;run=$run;pid=$process.Id;exit=$process.ExitCode;hash_match=$ok;log=$log;trace=$trace;affinity=$mask.ToInt64()})
			$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output "$stamp-captures.json") -Encoding UTF8
			if (-not $ok) { throw "GLOOM hash/exit gate failed: $log" }
			Write-Host "$mode run $run/${Runs}: hashes match"
		}
	} finally {
		if ($started) {
			& wpr -stop $trace "GLOOM $mode 600 measured ticks; phase markers exclude loading and warm-up"
			if ($LASTEXITCODE -ne 0) { Write-Error "WPR stop failed: $LASTEXITCODE" }
		}
	}
}
Get-FileHash -Algorithm SHA256 -LiteralPath $binary | Format-List | Out-File (Join-Path $output "$stamp-binary.txt")
Write-Host "Captures complete: $output"
