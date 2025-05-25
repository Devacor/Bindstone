@echo off
REM Cascade MSBuild script - leverages MSBuild properly to build BindstoneClient_Windows

echo ========================================
echo Cascade MSBuild - BindstoneClient_Windows
echo ========================================
echo.

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

REM First, restore NuGet packages if needed
echo Restoring NuGet packages...
%MSBUILD% Bindstone.sln /t:Restore /p:Configuration=Release /p:Platform=x64 /v:quiet

echo.
echo Building BindstoneClient_Windows project...
echo.

REM Build just the BindstoneClient_Windows project - MSBuild will handle dependencies
%MSBUILD% VSProjects\BindstoneClient\BindstoneClient_Windows\BindstoneClient_Windows.vcxproj ^
    /p:Configuration=Release ^
    /p:Platform=x64 ^
    /p:SolutionDir="%CD%\\" ^
    /v:minimal ^
    /m ^
    /fl ^
    /flp:logfile=cascade_build.log;errorsonly

if %ERRORLEVEL% EQU 0 (
    echo.
    echo BUILD SUCCESSFUL!
    echo Check Release\x64 for output files.
) else (
    echo.
    echo BUILD FAILED!
    echo Check cascade_build.log for errors.
    echo.
    type cascade_build.log 2>nul
)

echo.
