@echo off
REM One-time/idempotent local PostgreSQL setup for the Bindstone lobby server.
REM Downloads portable postgres binaries (no installer, no service, no admin),
REM initializes a data dir with trust auth, starts it, and applies the schema.
REM Everything lives under Tools\LocalServer (pgsql\, pgdata\ - both gitignored).
setlocal
set "HERE=%~dp0"
set "PGSQL=%HERE%pgsql"
set "PGDATA=%HERE%pgdata"
set "PGZIP=%HERE%postgresql-binaries.zip"
set "PGURL=https://get.enterprisedb.com/postgresql/postgresql-16.4-1-windows-x64-binaries.zip"

if not exist "%PGSQL%\bin\pg_ctl.exe" (
	echo [LocalServer] Downloading portable PostgreSQL binaries...
	curl -L --fail -o "%PGZIP%" "%PGURL%"
	if errorlevel 1 echo [LocalServer] Download failed. && exit /b 1
	echo [LocalServer] Extracting...
	tar -xf "%PGZIP%" -C "%HERE%."
	if errorlevel 1 echo [LocalServer] Extract failed. && exit /b 1
	del "%PGZIP%"
)

if not exist "%PGDATA%\PG_VERSION" (
	echo [LocalServer] Initializing database cluster...
	"%PGSQL%\bin\initdb.exe" -D "%PGDATA%" -U bindstone -E UTF8 -A trust
	if errorlevel 1 echo [LocalServer] initdb failed. && exit /b 1
)

"%PGSQL%\bin\pg_ctl.exe" -D "%PGDATA%" status >nul 2>&1
if errorlevel 1 (
	echo [LocalServer] Starting PostgreSQL...
	"%PGSQL%\bin\pg_ctl.exe" -D "%PGDATA%" -l "%HERE%pg.log" -w start
	if errorlevel 1 echo [LocalServer] PostgreSQL failed to start, see Tools\LocalServer\pg.log && exit /b 1
)

"%PGSQL%\bin\createdb.exe" -h localhost -U bindstone bindstone 2>nul
"%PGSQL%\bin\psql.exe" -v ON_ERROR_STOP=1 -q -h localhost -U bindstone -d bindstone -f "%HERE%players_schema.sql"
if errorlevel 1 echo [LocalServer] Schema apply failed. && exit /b 1

echo [LocalServer] PostgreSQL ready on localhost:5432 (db=bindstone user=bindstone)
exit /b 0
