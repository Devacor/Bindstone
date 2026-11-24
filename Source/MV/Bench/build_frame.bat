@echo off
REM Build the frame benchmark. %1 = directory containing mutedvision.lib (the engine under test).
REM                            %2 = output exe name (e.g. bench_frame_opt.exe).
REM external.lib / SDL3_Static.lib (unaffected by the optimization) always come from the main tree.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d %~dp0
set ROOT=%~dp0..\..\..
set ENGINE_LIBDIR=%~1
set OUTEXE=%~2
if "%ENGINE_LIBDIR%"=="" set ENGINE_LIBDIR=%ROOT%\Builds\Windows\x64\Release
if "%OUTEXE%"=="" set OUTEXE=bench_frame.exe
REM defines match the engine lib's (no NDEBUG, JAISCRIPT_ENABLE_DEBUGGER): JaiScript's header classes change layout with them
cl /nologo /O2 /std:c++20 /EHsc /MD /DJAISCRIPT_ENABLE_DEBUGGER ^
  /D_WIN32_WINNT=0x0602 /DNOMINMAX /DSDL_MAIN_HANDLED ^
  /I"%ROOT%\Source" /I"%ROOT%\External" /I"%ROOT%\External\glm\glm" ^
  /I"%ROOT%\External\gl3w\include" ^
  /I"%ROOT%\External\openssl\openssl-3.5.9\include" /I"%ROOT%\Source\JaiScript\include" ^
  /I"%ROOT%\VSProjects\SDL3\include" ^
  bench_frame.cpp "%ROOT%\External\gl3w\src\gl3w.c" /Fe:%OUTEXE% ^
  /link /LTCG /LIBPATH:"%ENGINE_LIBDIR%" ^
  /LIBPATH:"%ROOT%\Builds\Windows\x64\Release" ^
  /LIBPATH:"%ROOT%\External\openssl\openssl-3.5.9\x64\lib" ^
  mutedvision.lib external.lib SDL3_Static.lib opengl32.lib setupapi.lib winmm.lib ^
  imm32.lib version.lib Crypt32.lib libssl.lib libcrypto.lib ws2_32.lib ^
  user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib cfgmgr32.lib
