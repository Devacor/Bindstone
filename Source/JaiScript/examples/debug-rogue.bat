@echo off
setlocal
rem Launch jai_rogue with the JaiScript debugger listening (VS Code: F5 -> Attach to JaiScript).
rem Prefers the Debug build, falls back to Release. Extra args pass through (e.g. --debug-port 40000).
set "ROOT=%~dp0.."
set "EXE=%ROOT%\out\build\x64-Release BENCHMARKS\bin\jai_rogue.exe"
if not exist "%EXE%" set "EXE=%ROOT%\out\build\x64-Debug\bin\jai_rogue.exe"
if not exist "%EXE%" (
    echo Could not find jai_rogue.exe - build the demos first.
    pause
    exit /b 1
)
echo Launching with --debug ^(port 52472^). In VS Code press F5 -^> "Attach to JaiScript".
echo.
"%EXE%" --debug %*
echo.
echo [jai_rogue exited] press any key to close this window.
pause >nul
