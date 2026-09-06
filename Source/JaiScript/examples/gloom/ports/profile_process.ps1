# Use Process.Start directly so the process handle and exit code survive termination.
function Invoke-GloomProfileProcess {
	param(
		[Parameter(Mandatory=$true)][string]$FilePath,
		[string[]]$Arguments = @(),
		[IntPtr]$Affinity = [IntPtr]1
	)
	$startInfo = [Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = $FilePath
	# GLOOM arguments are fixed flag names and numbers, with no shell expansion.
	$startInfo.Arguments = $Arguments -join ' '
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
