@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul 2>&1
cd /d "d:\git\Bindstone\Source\JaiScript\out\build\x64-Release"
cmake --build . --config Release 2>&1
