@echo off
REM Build jai_rogue: Debug (links ~1s, for --dev hot-reload iteration) AND
REM Release (links ~2min under LTCG, but JaiScript runs ~25x faster - play this one).
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
cd /d C:\git\Bindstone\Source\JaiScript

echo === Debug build (fast link; slow to play) ===
cmake --build out/build/x64-Debug --target jai_rogue
if errorlevel 1 (echo DEBUG BUILD FAILED & exit /b 1)

echo.
echo === Release build (LTCG link takes ~2 minutes; be patient) ===
cmake --build "out/build/x64-Release BENCHMARKS" --target jai_rogue
if errorlevel 1 (echo RELEASE BUILD FAILED & exit /b 1)

echo.
echo PLAY THIS ONE (Release, fast):
echo   %CD%\out\build\x64-Release BENCHMARKS\bin\jai_rogue.exe
echo Dev iteration (Debug + hot reload on R):
echo   %CD%\out\build\x64-Debug\bin\jai_rogue.exe --dev
echo Flags: --seed N  --god  --dev  --time (latency report on exit)
