@echo off
rem PCSX2 GS texture-replacement deps: the zlib and libpng sections of PCSX2 v2.9.134's
rem .github/workflows/scripts/windows/build-dependencies.bat (versions lines 77-82, downloads 118 and 124, builds
rem 282-301), same URLs, SHA-256 pins and cmake lines. Only what GSTextureReplacementLoaders.cpp (PNG) and
rem 3rdparty/libzip (zlib) need (docs/ops/gs.md "Texture replacement").
rem Differences: 7-Zip -> Windows tar.exe (bsdtar); dirs from args; libpng's APNG patch is not applied (animated PNG
rem is not used by texture replacement, and it is a lone .gz that tar cannot unpack).
rem Separate from pcsx2-deps.bat so adding these never rebuilds shaderc.
rem Usage (inside vcvars, via tools/ob/vc.bat): pcsx2-deps-png.bat <install dir> <build dir>
setlocal enabledelayedexpansion
set "INSTALLDIR=%~1"
set "BUILDDIR=%~2"
set "TAR=%SystemRoot%\System32\tar.exe"
set LIBPNG=1658
set ZLIB=1.3.2
set ZLIBSHORT=132
if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"
if not exist "%INSTALLDIR%" mkdir "%INSTALLDIR%"
cd /d "%BUILDDIR%" || goto error

call :downloadfile "lpng%LIBPNG%.zip" https://download.sourceforge.net/libpng/lpng1658.zip b32f170855dbbe3e6d9e645af40b538137041773672c3ba3e02db5816c82d376 || goto error
call :downloadfile "zlib%ZLIBSHORT%.zip" "https://github.com/madler/zlib/releases/download/v%ZLIB%/zlib%ZLIBSHORT%.zip" e8bf55f3017aa181690990cb58a994e77885da140609fc8f94abe9b65d2cae28 || goto error

echo Building Zlib...
if exist "zlib-%ZLIB%" rmdir /S /Q "zlib-%ZLIB%"
"%TAR%" -xf "zlib%ZLIBSHORT%.zip" || goto error
cd "zlib-%ZLIB%" || goto error
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%INSTALLDIR%" -DCMAKE_INSTALL_PREFIX="%INSTALLDIR%" -DBUILD_SHARED_LIBS=ON -DZLIB_BUILD_EXAMPLES=OFF -B build -G Ninja || goto error
cmake --build build --parallel || goto error
ninja -C build install || goto error
cd .. || goto error

echo Building libpng...
if exist "lpng%LIBPNG%" rmdir /S /Q "lpng%LIBPNG%"
"%TAR%" -xf "lpng%LIBPNG%.zip" || goto error
cd "lpng%LIBPNG%" || goto error
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%INSTALLDIR%" -DCMAKE_INSTALL_PREFIX="%INSTALLDIR%" -DBUILD_SHARED_LIBS=ON -DBUILD_SHARED_LIBS=ON -DPNG_TESTS=OFF -DPNG_STATIC=OFF -DPNG_SHARED=ON -DPNG_TOOLS=OFF -B build -G Ninja || goto error
cmake --build build --parallel || goto error
ninja -C build install || goto error
cd .. || goto error

echo Exiting with success.
exit /b 0

:error
echo Failed with error #%errorlevel%.
exit /b 1

:downloadfile
if not exist "%~1" (
  echo Downloading %~1 from %~2...
  curl -L -o "%~1" "%~2" || exit /b 1
)
set idx=0
for /f %%F in ('certutil -hashfile "%~1" SHA256') do (
    set "out!idx!=%%F"
    set /a idx += 1
)
set filechecksum=%out1%
if /i %~3==%filechecksum% (
    echo Validated %~1.
    exit /B 0
) else (
    echo Expected %~3 got %filechecksum%.
    exit /B 1
)
