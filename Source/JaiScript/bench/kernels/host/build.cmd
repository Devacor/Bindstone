@echo off
rem Builds the JaiScript kernel host into bench\kernels\bin.
rem   set JAI_BUILD=<JaiScript build dir>    (default: build, the package's Release build directory)
setlocal
set HERE=%~dp0
set ROOT=%HERE%..\..\..
set OUT=%HERE%..\bin
if "%JAI_BUILD%"=="" set JAI_BUILD=%ROOT%\build
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
cl /nologo /std:c++20 /EHsc /O2 /Ob2 /Oi /Ot /GL /MT /permissive- /bigobj /DNDEBUG /DJAISCRIPT_ENABLE_DEBUGGER /I"%ROOT%\include" "%HERE%jai_kernel_host.cpp" /Fe:jai_kernel_host.exe /link /nologo /LTCG /OPT:REF /OPT:ICF "%JAI_BUILD%\lib\jaiscript.lib" ws2_32.lib || goto :fail
del /q *.obj
popd
echo BUILD OK
exit /b 0
:fail
popd
echo BUILD FAILED
exit /b 1
