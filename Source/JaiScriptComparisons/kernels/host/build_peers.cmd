@echo off
rem Builds the Lua and LuaJIT peers into kernels\bin (the JaiScript host builds from JaiScript\bench\kernels\host\build.cmd).
rem   set LUA_SRC=<lua-5.5.x source root>    (the folder holding src\lua.h)
rem   set LUAJIT_SRC=<LuaJIT checkout root>  (the folder holding src\msvcbuild.bat)
setlocal enabledelayedexpansion
rem msvcbuild.bat runs its freshly built helpers from the current directory
set NoDefaultCurrentDirectoryInExePath=
set HERE=%~dp0
set OUT=%HERE%..\bin
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"

if not "%LUA_SRC%"=="" (
	set LIST=
	for %%f in ("%LUA_SRC%\src\*.c") do (
		if /I not "%%~nf"=="lua" if /I not "%%~nf"=="luac" set LIST=!LIST! "%%f"
	)
	cl /nologo /O2 /MT /DNDEBUG /I"%LUA_SRC%\src" "%HERE%lua_kernel_host.c" !LIST! /Fe:lua_kernel_host.exe /link /nologo || goto :fail
	del /q *.obj
)

if "%LUAJIT_SRC%"=="" goto :done
pushd "%LUAJIT_SRC%\src"
call "%LUAJIT_SRC%\src\msvcbuild.bat" static
popd
if not exist "%LUAJIT_SRC%\src\luajit.exe" goto :fail
copy /y "%LUAJIT_SRC%\src\luajit.exe" . >nul
cl /nologo /O2 /MT /LD "%HERE%addOne.c" /Fe:addOne.dll /link /nologo || goto :fail
del /q *.obj

:jai

del /q *.obj
popd
echo BUILD OK
exit /b 0
:fail
popd
echo BUILD FAILED
exit /b 1
