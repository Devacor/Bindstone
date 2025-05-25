# BuildBindstoneWindowsClient.ps1
# Advanced build script for BindstoneClient_Windows project

param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [ValidateSet("quiet", "minimal", "normal", "detailed", "diagnostic")]
    [string]$Verbosity = "minimal",
    [switch]$Clean,
    [switch]$Rebuild,
    [switch]$ShowErrors
)

Write-Host "========================================"
Write-Host "Building BindstoneClient_Windows Project" -ForegroundColor Green
Write-Host "========================================"
Write-Host ""

# Set MSBuild path - try to find it automatically
$msbuildPaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
)

$msbuildPath = $null
foreach ($path in $msbuildPaths) {
    if (Test-Path $path) {
        $msbuildPath = $path
        break
    }
}

if (-not $msbuildPath) {
    Write-Host "ERROR: MSBuild not found!" -ForegroundColor Red
    Write-Host "Please install Visual Studio or update the paths in this script"
    exit 1
}

# Set project variables
$solutionFile = "Bindstone.sln"
$projectName = "BindstoneClient_Windows"

# Display build configuration
Write-Host "Configuration:" -ForegroundColor Yellow
Write-Host "  Solution: $solutionFile"
Write-Host "  Project: $projectName"
Write-Host "  Configuration: $Configuration"
Write-Host "  Platform: $Platform"
Write-Host "  Verbosity: $Verbosity"
Write-Host "  MSBuild: $msbuildPath"
if ($Clean) { Write-Host "  Clean: Yes" -ForegroundColor Cyan }
if ($Rebuild) { Write-Host "  Rebuild: Yes" -ForegroundColor Cyan }
Write-Host ""

# Build command arguments
$arguments = @(
    $solutionFile,
    "/t:${projectName}",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/m",  # Enable parallel build
    "/v:$Verbosity"
)

if ($Clean) {
    $arguments[1] = "/t:${projectName}:Clean"
} elseif ($Rebuild) {
    $arguments[1] = "/t:${projectName}:Rebuild"
}

if ($ShowErrors) {
    $arguments += "/fl"
    $arguments += "/flp:errorsonly;verbosity=detailed"
}

# Start build
Write-Host "Starting build..." -ForegroundColor Green
Write-Host "========================================"

$buildStart = Get-Date
& $msbuildPath $arguments
$buildResult = $LASTEXITCODE
$buildEnd = Get-Date
$buildTime = $buildEnd - $buildStart

Write-Host ""
Write-Host "========================================"

if ($buildResult -eq 0) {
    Write-Host "BUILD SUCCESSFUL" -ForegroundColor Green
    Write-Host "Build time: $($buildTime.ToString('mm\:ss'))"
    Write-Host "Output location: $Configuration\$Platform\"
} else {
    Write-Host "BUILD FAILED" -ForegroundColor Red
    Write-Host "Error code: $buildResult"
    Write-Host ""
    Write-Host "Troubleshooting tips:" -ForegroundColor Yellow
    Write-Host "1. Try running with -Verbosity detailed for more information"
    Write-Host "2. Use -ShowErrors to generate an error log file"
    Write-Host "3. Try -Clean first to clear cached build files"
    Write-Host "4. Check that all NuGet packages are restored"
    Write-Host "5. Open in Visual Studio to check for missing dependencies"
}

Write-Host "========================================"
