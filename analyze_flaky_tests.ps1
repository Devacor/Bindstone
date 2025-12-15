# Run tests multiple times and identify flaky tests
$runs = 5
$results = @{}

Write-Host "Running tests $runs times to identify flaky tests..." -ForegroundColor Cyan

for ($i = 1; $i -le $runs; $i++) {
    Write-Host "`nRun $i of $runs..." -ForegroundColor Yellow

    $output = & .\out\build\x64-Debug\source\tests\jaiscript_tests.exe 2>&1 | Out-String

    # Extract test results
    $lines = $output -split "`n"
    $currentSuite = ""

    foreach ($line in $lines) {
        # Detect suite name
        if ($line -match "├──\s+(.+?)\s+──┤") {
            $currentSuite = $matches[1]
        }
        # Detect test failure
        elseif ($line -match "^\s+(\w+)\s+\.\.\.\s+x$") {
            $testName = $matches[1]
            $fullName = "$currentSuite::$testName"

            if (-not $results.ContainsKey($fullName)) {
                $results[$fullName] = @{
                    Passes = 0
                    Failures = 0
                }
            }
            $results[$fullName].Failures++
        }
        # Detect test pass
        elseif ($line -match "^\s+(\w+)\s+\.\.\.\s+<3") {
            $testName = $matches[1]
            $fullName = "$currentSuite::$testName"

            if (-not $results.ContainsKey($fullName)) {
                $results[$fullName] = @{
                    Passes = 0
                    Failures = 0
                }
            }
            $results[$fullName].Passes++
        }
    }

    # Count total failures this run
    $failCount = ($output | Select-String "x (\d+) test\(s\) failed").Matches.Groups[1].Value
    Write-Host "Run $i: $failCount test(s) failed" -ForegroundColor $(if ($failCount -eq "13") { "Green" } else { "Red" })
}

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "FLAKY TESTS (inconsistent results):" -ForegroundColor Red
Write-Host "========================================" -ForegroundColor Cyan

$flakyTests = $results.GetEnumerator() | Where-Object {
    $_.Value.Passes -gt 0 -and $_.Value.Failures -gt 0
} | Sort-Object Name

if ($flakyTests.Count -eq 0) {
    Write-Host "No flaky tests detected!" -ForegroundColor Green
} else {
    foreach ($test in $flakyTests) {
        $passRate = [math]::Round(($test.Value.Passes / $runs) * 100, 1)
        Write-Host "$($test.Name)" -ForegroundColor Yellow
        Write-Host "  Passed: $($test.Value.Passes)/$runs ($passRate%)" -ForegroundColor Green
        Write-Host "  Failed: $($test.Value.Failures)/$runs" -ForegroundColor Red
    }
}

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "CONSISTENTLY FAILING TESTS:" -ForegroundColor Red
Write-Host "========================================" -ForegroundColor Cyan

$consistentFails = $results.GetEnumerator() | Where-Object {
    $_.Value.Failures -eq $runs
} | Sort-Object Name

foreach ($test in $consistentFails) {
    Write-Host "  $($test.Name)" -ForegroundColor Red
}

Write-Host "`nTotal consistently failing: $($consistentFails.Count)" -ForegroundColor Red
Write-Host "Total flaky: $($flakyTests.Count)" -ForegroundColor Yellow
