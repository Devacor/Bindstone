@echo off
REM Build script for BindstoneClient_Windows project
REM This script builds the BindstoneClient_Windows project using MSBuild

echo ========================================
echo Building BindstoneClient_Windows Project
echo ========================================
echo.

REM Set MSBuild path - adjust this if your VS installation is different
set MSBUILD_PATH="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

REM Check if MSBuild exists
if not exist %MSBUILD_PATH% (
    echo ERROR: MSBuild not found at %MSBUILD_PATH%
    echo Please update the MSBUILD_PATH variable in this script
    echo.
    echo Common MSBuild locations:
    echo - VS 2022 Professional: "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    echo - VS 2022 Community: "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    echo - VS 2022 Enterprise: "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    exit /b 1
)

REM Set project variables
set SOLUTION_FILE=Bindstone.sln
set PROJECT_FILE=VSProjects\BindstoneClient\BindstoneClient_Windows\BindstoneClient_Windows.vcxproj
set PROJECT_NAME=BindstoneClient_Windows
set CONFIGURATION=Release
set PLATFORM=x64

REM Display build configuration
echo Configuration:
echo - Solution: %SOLUTION_FILE%
echo - Project: %PROJECT_FILE%
echo - Configuration: %CONFIGURATION%
echo - Platform: %PLATFORM%
echo - MSBuild: %MSBUILD_PATH%
echo.

REM Build the project
echo Starting build...
echo ========================================
%MSBUILD_PATH% %PROJECT_FILE% /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM% /m /v:minimal

REM Check build result
if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo BUILD SUCCESSFUL
    echo ========================================
    echo Output should be in: %CONFIGURATION%\%PLATFORM%\
) else (
    echo.
    echo ========================================
    echo BUILD FAILED with error code: %ERRORLEVEL%
    echo ========================================
    echo.
    echo Troubleshooting tips:
    echo 1. Check that all dependencies are installed
    echo 2. Try running "nuget restore %SOLUTION_FILE%" first
    echo 3. Open the solution in Visual Studio to check for missing components
    echo 4. Run with /v:normal or /v:detailed for more verbose output
)

echo.
pause
