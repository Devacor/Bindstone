@echo off
rem Gator Quest launcher: finds jaiscript.exe beside this file (the shipped demo) or in the build tree.
setlocal
cd /d "%~dp0"
set "EXE=%~dp0jaiscript.exe"
if not exist "%EXE%" set "EXE=%~dp0..\..\out\build\x64-Release BENCHMARKS\bin\jaiscript.exe"
if not exist "%EXE%" (
	echo Could not find jaiscript.exe. Copy it next to play.bat, or build the runner:
	echo   cmake --build "out/build/x64-Release BENCHMARKS" --target jaiscript_runner
	pause
	exit /b 1
)
"%EXE%" --no-debug --no-pause "%~dp0scripts\gatorquest.jai" -- %*
