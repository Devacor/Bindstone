@echo off
rem Builds OpenSSL for Windows x64 as static libraries and installs them into External\openssl\openssl-<version>.
rem Upgrading: change OPENSSL_VERSION, run this, switch the projects' openssl-<version> paths (see README.md).
rem Needs: Visual Studio (VsDevCmd), a native Windows Perl (Strawberry; MSYS/Git perl does not work), NASM, curl, tar.
rem   OPENSSL_PERL   perl.exe to use (default: first perl.exe on PATH that reports MSWin32)
rem   OPENSSL_NASM   folder holding nasm.exe (default: nasm on PATH)
rem   OPENSSL_WORK   scratch folder for download, source and build (default: %TEMP%\openssl-build-<version>)
rem   VSDEVCMD       VsDevCmd.bat (default: located with vswhere)
setlocal EnableExtensions EnableDelayedExpansion

set OPENSSL_VERSION=3.5.9
set OPENSSL_SIGNING_FINGERPRINT=B146647E45A7B33947AB226B2A2C87D161692D40
set OPENSSL_CONFIGURE=VC-WIN64A no-shared no-tests

set OPENSSL_URL=https://github.com/openssl/openssl/releases/download/openssl-%OPENSSL_VERSION%
set OPENSSL_PUBKEYS_URL=https://openssl-library.org/source/pubkeys.asc
set TARBALL=openssl-%OPENSSL_VERSION%.tar.gz
set DEST=%~dp0openssl-%OPENSSL_VERSION%
if "%OPENSSL_WORK%"=="" set OPENSSL_WORK=%TEMP%\openssl-build-%OPENSSL_VERSION%
set DOWNLOAD=%OPENSSL_WORK%\download
set SOURCE=%OPENSSL_WORK%\src\openssl-%OPENSSL_VERSION%
set STAGE=%OPENSSL_WORK%\stage

if "%OPENSSL_PERL%"=="" (
	for /f "delims=" %%p in ('where perl.exe 2^>nul') do (
		if "!OPENSSL_PERL!"=="" (
			for /f "delims=" %%o in ('""%%p" -V:osname" 2^>nul') do if "%%o"=="osname='MSWin32';" set OPENSSL_PERL=%%p
		)
	)
)
if "%OPENSSL_PERL%"=="" (
	echo No native Windows perl found: install Strawberry Perl or set OPENSSL_PERL.
	exit /b 1
)
for %%p in ("%OPENSSL_PERL%") do set PERL_DIR=%%~dpp
if not "%OPENSSL_NASM%"=="" set PATH=%OPENSSL_NASM%;%PATH%
where nasm.exe >nul 2>&1 || (echo NASM not found: install it or set OPENSSL_NASM. & exit /b 1)

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if "%VSDEVCMD%"=="" (
	for /f "usebackq delims=" %%i in (`""!VSWHERE!" -latest -products * -property installationPath"`) do set VSDEVCMD=%%i\Common7\Tools\VsDevCmd.bat
)
if not exist "%VSDEVCMD%" (echo VsDevCmd.bat not found: set VSDEVCMD. & exit /b 1)

if not exist "%DOWNLOAD%" mkdir "%DOWNLOAD%"
for %%f in (%TARBALL% %TARBALL%.sha256 %TARBALL%.asc) do (
	if not exist "%DOWNLOAD%\%%f" (
		curl -sSfL -o "%DOWNLOAD%\%%f" "%OPENSSL_URL%/%%f" || (echo Download of %%f failed. & exit /b 1)
	)
)

set EXPECTED_SHA256=
set ACTUAL_SHA256=
for /f "usebackq tokens=1" %%h in ("%DOWNLOAD%\%TARBALL%.sha256") do set EXPECTED_SHA256=%%h
for /f "delims=" %%h in ('certutil -hashfile "%DOWNLOAD%\%TARBALL%" SHA256 ^| findstr /v ":"') do set ACTUAL_SHA256=%%h
set ACTUAL_SHA256=%ACTUAL_SHA256: =%
echo SHA256 %ACTUAL_SHA256%
if /i not "%ACTUAL_SHA256%"=="%EXPECTED_SHA256%" (echo SHA256 mismatch: published %EXPECTED_SHA256%. & exit /b 1)

set GPG=
for /f "delims=" %%g in ('where gpg.exe 2^>nul') do if "!GPG!"=="" set GPG=%%g
if "%GPG%"=="" if exist "%ProgramFiles%\Git\usr\bin\gpg.exe" set GPG=%ProgramFiles%\Git\usr\bin\gpg.exe
if "%GPG%"=="" (
	echo gpg not found: PGP signature NOT verified ^(SHA256 only^).
) else (
	if not exist "%DOWNLOAD%\pubkeys.asc" (
		curl -sSfL -o "%DOWNLOAD%\pubkeys.asc" "%OPENSSL_PUBKEYS_URL%" || (echo Download of pubkeys.asc failed. & exit /b 1)
	)
	if not exist "%OPENSSL_WORK%\gnupg" mkdir "%OPENSSL_WORK%\gnupg"
	rem Relative paths: Git for Windows' MSYS gpg reads C:\ paths as relative ones.
	pushd "%OPENSSL_WORK%"
	"!GPG!" --batch --quiet --homedir gnupg --import download/pubkeys.asc 2>nul
	"!GPG!" --batch --homedir gnupg --status-fd 1 --verify download/%TARBALL%.asc download/%TARBALL% 2>nul > gpg_status.txt
	popd
	findstr /r /c:"VALIDSIG .* %OPENSSL_SIGNING_FINGERPRINT%" "%OPENSSL_WORK%\gpg_status.txt" >nul || (
		echo PGP signature is not a valid signature by %OPENSSL_SIGNING_FINGERPRINT%.
		type "%OPENSSL_WORK%\gpg_status.txt"
		exit /b 1
	)
	echo PGP signature good: primary key %OPENSSL_SIGNING_FINGERPRINT%
)

if exist "%OPENSSL_WORK%\src" rmdir /s /q "%OPENSSL_WORK%\src"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%OPENSSL_WORK%\src"
tar -xzf "%DOWNLOAD%\%TARBALL%" -C "%OPENSSL_WORK%\src" || (echo Extraction failed. & exit /b 1)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul || exit /b 1
set NoDefaultCurrentDirectoryInExePath=
set PATH=%PERL_DIR%;%PATH%

pushd "%SOURCE%"
"%OPENSSL_PERL%" Configure %OPENSSL_CONFIGURE% || (popd & exit /b 1)
rem No debug info: it would be most of libcrypto.lib's size in git.
"%OPENSSL_PERL%" "%~dp0strip_debug_info.pl" makefile || (popd & exit /b 1)
nmake /nologo || (popd & exit /b 1)
nmake /nologo install_sw DESTDIR="%STAGE%" || (popd & exit /b 1)
popd

set INSTALLED=
for /f "delims=" %%d in ('dir /s /b /ad "%STAGE%\include" 2^>nul') do if "!INSTALLED!"=="" set INSTALLED=%%~dpd
if "%INSTALLED%"=="" (echo install_sw left no include folder under %STAGE%. & exit /b 1)

if exist "%DEST%" rmdir /s /q "%DEST%"
mkdir "%DEST%\include" "%DEST%\x64\lib" "%DEST%\x64\bin"
xcopy /e /i /q /y "%INSTALLED%include\openssl" "%DEST%\include\openssl" >nul || exit /b 1
for %%f in (libssl.lib libcrypto.lib) do (
	if exist "%INSTALLED%lib\%%f" (
		copy /y "%INSTALLED%lib\%%f" "%DEST%\x64\lib\" >nul
	) else if exist "%SOURCE%\%%f" (
		copy /y "%SOURCE%\%%f" "%DEST%\x64\lib\" >nul
	) else (
		echo Missing %%f. & exit /b 1
	)
)
copy /y "%INSTALLED%bin\openssl.exe" "%DEST%\x64\bin\" >nul || exit /b 1
copy /y "%SOURCE%\LICENSE.txt" "%DEST%\" >nul || exit /b 1

"%DEST%\x64\bin\openssl.exe" version -a
echo Installed OpenSSL %OPENSSL_VERSION% into %DEST%
endlocal
