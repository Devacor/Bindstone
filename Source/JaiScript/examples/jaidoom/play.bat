@echo off
rem JaiDOOM launcher: double-click me. Works from any copied location, using the
rem jaiscript.exe sitting next to this file (or one on PATH); the game finds wads/ from
rem its own location. It passes fetch, so a first run downloads the game data on its loading
rem screen; jaidoom.jai run without it (dropped onto jaiscript.exe) downloads nothing.
cd /d "%~dp0scripts"
if exist "%~dp0jaiscript.exe" (
	"%~dp0jaiscript.exe" jaidoom.jai -- fetch
) else (
	jaiscript.exe jaidoom.jai -- fetch
)
