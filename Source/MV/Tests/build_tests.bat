@echo off
REM Build the Bindstone (MV) Foundry test runner -> mv_tests.exe, then run it.
REM Uses JaiScript's Foundry framework (header-only) + its generic main_test_runner.cpp, linked
REM against the built engine static lib. Returns non-zero if any test fails (CI gate).
REM Optional arg %1 = directory containing mutedvision.lib (default = optimized main tree).
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d d:\git\Bindstone\Source\MV\Tests
set ROOT=d:\git\Bindstone
set ENGINE_LIBDIR=%~1
if "%ENGINE_LIBDIR%"=="" set ENGINE_LIBDIR=%ROOT%\Builds\Windows\x64\Release

cl /nologo /O2 /std:c++20 /EHsc /MD /DNDEBUG ^
  /DASIO_STANDALONE /D_WIN32_WINNT=0x0602 /DCEREAL_FUTURE_EXPERIMENTAL /DNOMINMAX /DSDL_MAIN_HANDLED ^
  /I"%ROOT%\Source" /I"%ROOT%\External" /I"%ROOT%\External\glm\glm" ^
  /I"%ROOT%\External\cereal\include" /I"%ROOT%\External\ChaiScript-6.1.0\include" ^
  /I"%ROOT%\External\gl3w\include" /I"%ROOT%\External\asio\include" ^
  /I"%ROOT%\External\openssl\openssl-1.1.0c\include" /I"%ROOT%\Source\JaiScript\include" ^
  /I"%ROOT%\VSProjects\SDL2\include" ^
  matrix_tests.cpp scene_tests.cpp atlas_tests.cpp rhi_tests.cpp ^
  "%ROOT%\Source\JaiScript\source\tests\main_test_runner.cpp" ^
  "%ROOT%\External\gl3w\src\gl3w.c" /Fe:mv_tests.exe ^
  /link /LTCG /LIBPATH:"%ENGINE_LIBDIR%" ^
  /LIBPATH:"%ROOT%\Builds\Windows\x64\Release" ^
  /LIBPATH:"%ROOT%\External\openssl\openssl-1.1.0c\x64\Release\lib" ^
  mutedvision.lib external.lib SDL2_Static.lib opengl32.lib setupapi.lib winmm.lib ^
  imm32.lib version.lib Crypt32.lib libssl.lib libcrypto.lib ws2_32.lib ^
  user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib cfgmgr32.lib
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo.
mv_tests.exe --verbose
