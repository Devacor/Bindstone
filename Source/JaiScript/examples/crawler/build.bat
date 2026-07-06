@echo off
REM Build jai_crawler: Debug (~1s link, --dev iteration) and Release (play this one).
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
cd /d C:\git\Bindstone\Source\JaiScript
cmake --build out/build/x64-Debug --target jai_crawler
if errorlevel 1 (echo DEBUG BUILD FAILED & exit /b 1)
cmake --build "out/build/x64-Release BENCHMARKS" --target jai_crawler
if errorlevel 1 (echo RELEASE BUILD FAILED & exit /b 1)
echo.
echo Dev:  %CD%\out\build\x64-Debug\bin\jai_crawler.exe            (--dev hot reload)
echo Play: %CD%\out\build\x64-Release BENCHMARKS\bin\jai_crawler.exe   (recommended)
