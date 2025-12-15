# Quick check for failures across 3 runs
for ($i = 1; $i -le 3; $i++) {
    Write-Host "`n=== RUN $i ===" -ForegroundColor Cyan
    $output = & .\out\build\x64-Debug\source\tests\jaiscript_tests.exe 2>&1 | Out-String

    # Extract just the failed tests
    $lines = $output -split "`n"
    $currentSuite = ""
    $failures = @()

    foreach ($line in $lines) {
        # Match suite headers (looking for pattern between box drawing chars)
        if ($line -match "\s+([A-Za-z0-9\s]+?)\s+(Tests?|Debug|Handling)") {
            $currentSuite = $matches[1].Trim()
        }
        # Match failed tests (test name followed by " ... x")
        elseif ($line -match "^\s+(\w+)\s+\.\.\.\s+x\s*$") {
            $testName = $matches[1]
            $failures += "$currentSuite::$testName"
        }
    }

    Write-Host "Failed tests ($($failures.Count)):" -ForegroundColor Red
    $failures | Sort-Object | ForEach-Object { Write-Host "  $_" }
}
