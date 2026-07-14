@echo off
rem JaiDOOM launcher — double-click me. Works from any copied location:
rem cd into scripts/ so the game's relative wad path (../wads/) resolves,
rem then use the jaiscript.exe sitting next to this file (or one on PATH).
cd /d "%~dp0scripts"
if exist "%~dp0jaiscript.exe" (
	"%~dp0jaiscript.exe" jaidoom.jai
) else (
	jaiscript.exe jaidoom.jai
)
