# Use Process.Start directly so the process handle and exit code survive termination.
function Invoke-GloomProfileProcess {
	param(
		[Parameter(Mandatory=$true)][string]$FilePath,
		[string[]]$Arguments = @(),
		[IntPtr]$Affinity = [IntPtr]1,
		[string]$WorkingDirectory = '',
		[hashtable]$Environment = @{}
	)
	$startInfo = [Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $FilePath
	# Quote for the Windows argv decoder; no command shell is involved.
	$quoted = foreach ($argument in $Arguments) {
		if ($argument.Length -gt 0 -and $argument -notmatch '[\s"]') { $argument }
		else {
			$escaped = [regex]::Replace($argument, '(\\*)"', '$1$1\"')
			$escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
			'"' + $escaped + '"'
		}
	}
	$startInfo.Arguments = $quoted -join ' '
	if ($WorkingDirectory) { $startInfo.WorkingDirectory = $WorkingDirectory }
	foreach ($key in $Environment.Keys) { $startInfo.EnvironmentVariables[$key] = $Environment[$key] }
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $startInfo
	$started = $false
	try {
		$started = $process.Start()
		if (-not $started) { throw "Could not start $FilePath" }
		$stdoutTask = $process.StandardOutput.ReadToEndAsync()
		$stderrTask = $process.StandardError.ReadToEndAsync()
		try {
			if (-not $process.HasExited) { $process.ProcessorAffinity = $Affinity }
		} catch {
			if (-not $process.HasExited) { throw }
		}
		$process.WaitForExit()
		$result = [pscustomobject]@{
			Pid = $process.Id
			ExitCode = $process.ExitCode
			Stdout = $stdoutTask.GetAwaiter().GetResult()
			Stderr = $stderrTask.GetAwaiter().GetResult()
		}
		if ($null -eq $result.ExitCode) { throw 'Child exit status was unavailable.' }
		return $result
	} finally {
		if ($started -and -not $process.HasExited) {
			$process.Kill()
			$process.WaitForExit()
		}
		$process.Dispose()
	}
}
