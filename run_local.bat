@echo off
REM One-click local Bindstone: postgres + LobbyServer + GameServer + two logged-in clients.
REM Usage: run_local.bat [Release|Debug]   (default Release)
setlocal
set "ROOT=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "BIN=%ROOT%Builds\Windows\x64\%CONFIG%"

for %%E in (LobbyServer GameServer Bindstone) do (
	if not exist "%BIN%\%%E.exe" echo Missing %BIN%\%%E.exe - build the %CONFIG% configuration first. && exit /b 1
)

call "%ROOT%Tools\LocalServer\setup_local_db.bat"
if errorlevel 1 exit /b 1

start "LobbyServer (%CONFIG%)" /D "%ROOT%" "%BIN%\LobbyServer.exe"
timeout /t 2 /nobreak >nul
start "GameServer (%CONFIG%)" /D "%ROOT%" "%BIN%\GameServer.exe"
start "Bindstone - test1" /D "%ROOT%" "%BIN%\Bindstone.exe" -n test1 -p password1
start "Bindstone - test2" /D "%ROOT%" "%BIN%\Bindstone.exe" -n test2 -p password2
exit /b 0
