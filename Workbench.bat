@echo off
REM Launches the Workbench editor (Source/Workbench). Default = Debug build; pass "release" to use Release.
cd /d %~dp0
set WORKBENCH_EXE=Builds\Windows\x64\Debug\Bindstone.exe
if /i "%~1"=="release" set WORKBENCH_EXE=Builds\Windows\x64\Release\Bindstone.exe
if not exist "%WORKBENCH_EXE%" (
	echo %WORKBENCH_EXE% not found - build BindstoneClient_Windows first.
	pause
	exit /b 1
)
"%WORKBENCH_EXE%" -workbench
