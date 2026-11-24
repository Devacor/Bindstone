@echo off
REM Build the scene-graph dirty-flag bench against the built engine static lib (Release /MD, C++20).
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d %~dp0
set ROOT=%~dp0..\..\..
REM defines match the engine lib's (no NDEBUG, JAISCRIPT_ENABLE_DEBUGGER): JaiScript's header classes change layout with them
cl /nologo /O2 /std:c++20 /EHsc /MD /DJAISCRIPT_ENABLE_DEBUGGER ^
  /D_WIN32_WINNT=0x0602 /DNOMINMAX /DSDL_MAIN_HANDLED ^
  /I"%ROOT%\Source" /I"%ROOT%\External" /I"%ROOT%\External\glm\glm" ^
  /I"%ROOT%\External\gl3w\include" ^
  /I"%ROOT%\External\openssl\openssl-3.5.9\include" /I"%ROOT%\Source\JaiScript\include" ^
  /I"%ROOT%\VSProjects\SDL3\include" ^
  bench_scene.cpp "%ROOT%\External\gl3w\src\gl3w.c" /Fe:bench_scene.exe ^
  /link /LTCG /LIBPATH:"%ROOT%\Builds\Windows\x64\Release" ^
  /LIBPATH:"%ROOT%\External\openssl\openssl-3.5.9\x64\lib" ^
  mutedvision.lib external.lib SDL3_Static.lib opengl32.lib setupapi.lib winmm.lib ^
  imm32.lib version.lib Crypt32.lib libssl.lib libcrypto.lib ws2_32.lib ^
  user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib cfgmgr32.lib
