@echo off
rem Builds the peers and the JaiScript host into bench\kernels\bin.
rem   set LUA_SRC=<lua-5.5.x source root>    (the folder holding src\lua.h)
rem   set LUAJIT_SRC=<LuaJIT checkout root>  (the folder holding src\msvcbuild.bat)
rem   set JAI_BUILD=<JaiScript build dir>    (default: build, the package's Release build directory)
setlocal enabledelayedexpansion
rem msvcbuild.bat runs its freshly built helpers from the current directory
set NoDefaultCurrentDirectoryInExePath=
set HERE=%~dp0
set ROOT=%HERE%..\..\..
set OUT=%HERE%..\bin
if "%JAI_BUILD%"=="" set JAI_BUILD=%ROOT%\build
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

if "%LUAJIT_SRC%"=="" goto :jai
pushd "%LUAJIT_SRC%\src"
call "%LUAJIT_SRC%\src\msvcbuild.bat" static
popd
if not exist "%LUAJIT_SRC%\src\luajit.exe" goto :fail
copy /y "%LUAJIT_SRC%\src\luajit.exe" . >nul
cl /nologo /O2 /MT /LD "%HERE%addOne.c" /Fe:addOne.dll /link /nologo || goto :fail
del /q *.obj

:jai

cl /nologo /std:c++20 /EHsc /O2 /Ob2 /Oi /Ot /GL /MT /permissive- /bigobj /DNDEBUG /DJAISCRIPT_ENABLE_DEBUGGER /I"%ROOT%\include" "%HERE%jai_kernel_host.cpp" /Fe:jai_kernel_host.exe /link /nologo /LTCG /OPT:REF /OPT:ICF "%JAI_BUILD%\lib\jaiscript.lib" ws2_32.lib || goto :fail
del /q *.obj
popd
echo BUILD OK
exit /b 0
:fail
popd
echo BUILD FAILED
exit /b 1
