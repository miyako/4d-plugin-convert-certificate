@echo off
rem Builds OpenSSL's libcrypto from the pinned release tarball as a static
rem x64 library for the Visual Studio project (Release|x64):
rem   lib\64\libcrypto.lib   (VC-WIN64A, static CRT /MT like the vcxproj)
rem   include\openssl\*.h    (installed headers)
rem
rem no-shared no-module: static only, with the legacy provider (MDC2, RC2,
rem PKCS#12 encryption of older exports) linked into libcrypto.
rem no-asm: no NASM needed.
rem
rem usage: scripts\build-windows-deps.cmd   (from a "x64 Native Tools" prompt,
rem or anywhere: it finds Visual Studio with vswhere and calls vcvars64)
rem Needs: Visual Studio 2022 C++ tools, Strawberry Perl, curl, tar.

setlocal EnableExtensions
set OPENSSL_VERSION=3.5.9
set OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
set OPENSSL_URL=https://github.com/openssl/openssl/releases/download/openssl-%OPENSSL_VERSION%/openssl-%OPENSSL_VERSION%.tar.gz

set ROOT=%~dp0..
pushd "%ROOT%" || exit /b 1
set ROOT=%CD%
set WORK=%ROOT%\build\deps\win64
set PREFIX=%WORK%\install
set TARBALL=%WORK%\openssl-%OPENSSL_VERSION%.tar.gz

if exist "C:\Strawberry\perl\bin\perl.exe" (set PERL=C:\Strawberry\perl\bin\perl.exe) else (set PERL=perl)

where cl >nul 2>&1 && goto :have_cl
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSINSTALL=%%i
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
:have_cl

if not exist "%WORK%" mkdir "%WORK%"
if not exist "%TARBALL%" (
  curl -fsSL --retry 3 -o "%TARBALL%" "%OPENSSL_URL%" || exit /b 1
)
powershell -NoProfile -Command "if ((Get-FileHash -Algorithm SHA256 '%TARBALL%').Hash -ne '%OPENSSL_SHA256%') { Write-Error 'SHA-256 mismatch'; exit 1 }" || exit /b 1

set SRC=%WORK%\openssl-%OPENSSL_VERSION%
if exist "%SRC%" rmdir /s /q "%SRC%"
tar -xzf "%TARBALL%" -C "%WORK%" || exit /b 1

pushd "%SRC%" || exit /b 1
"%PERL%" Configure VC-WIN64A no-shared no-module no-tests no-docs no-apps no-asm --prefix="%PREFIX%" --openssldir="%PREFIX%\ssl" || exit /b 1
nmake /nologo build_libs || exit /b 1
nmake /nologo install_dev || exit /b 1
popd

if not exist "%ROOT%\lib\64" mkdir "%ROOT%\lib\64"
copy /y "%PREFIX%\lib\libcrypto.lib" "%ROOT%\lib\64\libcrypto.lib" || exit /b 1
if exist "%ROOT%\include\openssl" rmdir /s /q "%ROOT%\include\openssl"
xcopy /e /i /q /y "%PREFIX%\include\openssl" "%ROOT%\include\openssl" || exit /b 1
findstr /c:"OpenSSL %OPENSSL_VERSION% " "%ROOT%\include\openssl\opensslv.h" || exit /b 1
echo OpenSSL %OPENSSL_VERSION% -^> lib\64\libcrypto.lib, include\openssl
popd
endlocal
