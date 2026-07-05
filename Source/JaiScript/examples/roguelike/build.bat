@echo off
REM Build jai_rogue (Debug, ~1s link). Run from anywhere.
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
cd /d C:\git\Bindstone\Source\JaiScript
cmake --build out/build/x64-Debug --target jai_rogue
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo.
echo Play: %CD%\out\build\x64-Debug\bin\jai_rogue.exe  (--dev for hot reload, --seed N, --god)
