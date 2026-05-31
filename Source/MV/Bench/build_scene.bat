@echo off
REM Build the scene-graph dirty-flag bench against the built engine static lib (Release /MD, C++20).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d d:\git\Bindstone\Source\MV\Bench
set ROOT=d:\git\Bindstone
cl /nologo /O2 /std:c++20 /EHsc /MD /DNDEBUG ^
  /DASIO_STANDALONE /D_WIN32_WINNT=0x0602 /DCEREAL_FUTURE_EXPERIMENTAL /DNOMINMAX /DSDL_MAIN_HANDLED ^
  /I"%ROOT%\Source" /I"%ROOT%\External" /I"%ROOT%\External\glm\glm" ^
  /I"%ROOT%\External\cereal\include" /I"%ROOT%\External\ChaiScript-6.1.0\include" ^
  /I"%ROOT%\External\gl3w\include" /I"%ROOT%\External\asio\include" ^
  /I"%ROOT%\External\openssl\openssl-1.1.0c\include" /I"%ROOT%\Source\JaiScript\include" ^
  /I"%ROOT%\VSProjects\SDL2\include" ^
  bench_scene.cpp "%ROOT%\External\gl3w\src\gl3w.c" /Fe:bench_scene.exe ^
  /link /LTCG /LIBPATH:"%ROOT%\Builds\Windows\x64\Release" ^
  /LIBPATH:"%ROOT%\External\openssl\openssl-1.1.0c\x64\Release\lib" ^
  mutedvision.lib external.lib SDL2_Static.lib opengl32.lib setupapi.lib winmm.lib ^
  imm32.lib version.lib Crypt32.lib libssl.lib libcrypto.lib ws2_32.lib ^
  user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib cfgmgr32.lib
