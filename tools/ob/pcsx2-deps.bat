@echo off
rem PCSX2 GS deps: the zstd and shaderc sections of PCSX2 v2.9.134's
rem .github/workflows/scripts/windows/build-dependencies.bat (lines 83-137, 350-357, 530-545),
rem same URLs, SHA-256 pins, patch and cmake lines; only what the GS link needs (docs/ops/gs.md).
rem Differences: 7-Zip -> Windows tar.exe (bsdtar; 7-Zip not installed); dirs from args.
rem Usage (inside vcvars, via tools/ob/vc.bat): pcsx2-deps.bat <pcsx2 src> <install dir> <build dir>
setlocal enabledelayedexpansion
set "SRC=%~1"
set "INSTALLDIR=%~2"
set "BUILDDIR=%~3"
set "TAR=%SystemRoot%\System32\tar.exe"
set PATCH="C:\Program Files\Git\usr\bin\patch.exe"
set "SCRIPTDIR=%SRC%\.github\workflows\scripts\windows"
set ZSTD=1.5.7
set SHADERC=2026.2
set SHADERC_GLSLANG=275822a6261ee689aadb1da5f09a0ec2f058685c
set SHADERC_SPIRVHEADERS=58006c901d1d5c37dece6b6610e9af87fa951375
set SHADERC_SPIRVTOOLS=6337eb62cadd7d124ac6789bf39c0f71148f0a73
if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"
if not exist "%INSTALLDIR%" mkdir "%INSTALLDIR%"
cd /d "%BUILDDIR%" || goto error

call :downloadfile "zstd-%ZSTD%.zip" "https://github.com/facebook/zstd/archive/refs/tags/v%ZSTD%.zip" 7897bc5d620580d9b7cd3539c44b59d78f3657d33663fe97a145e07b4ebd69a4 || goto error
call :downloadfile "shaderc-%SHADERC%.zip" "https://github.com/google/shaderc/archive/refs/tags/v%SHADERC%.zip" f9401cc5cb36c276cd1e072b6595dbd728148e8dba389e50f7339e2d388dbc08 || goto error
call :downloadfile "shaderc-glslang-%SHADERC_GLSLANG%.zip" "https://github.com/KhronosGroup/glslang/archive/%SHADERC_GLSLANG%.zip" 2b63189efad0348d88d410a5e12ec550a612e0b6ceef64624b8f45491269fb9c || goto error
call :downloadfile "shaderc-spirv-headers-%SHADERC_SPIRVHEADERS%.zip" "https://github.com/KhronosGroup/SPIRV-Headers/archive/%SHADERC_SPIRVHEADERS%.zip" d2f071e94c081f5a4606559770ebf1f7d1eac92a1def0c3e10609844aa8b69b2 || goto error
call :downloadfile "shaderc-spirv-tools-%SHADERC_SPIRVTOOLS%.zip" "https://github.com/KhronosGroup/SPIRV-Tools/archive/%SHADERC_SPIRVTOOLS%.zip" 4011be89aa73e3461c9deef73936a62c79a3097590c5135d058041cc9fb99c6f || goto error

echo Building Zstandard...
if exist "zstd-%ZSTD%" rmdir /S /Q "zstd-%ZSTD%"
"%TAR%" -xf "zstd-%ZSTD%.zip" --exclude "zstd-%ZSTD%/tests/cli-tests/bin" || goto error
cd "zstd-%ZSTD%" || goto error
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%INSTALLDIR%" -DCMAKE_INSTALL_PREFIX="%INSTALLDIR%" -DBUILD_SHARED_LIBS=ON -DZSTD_BUILD_SHARED=ON -DZSTD_BUILD_STATIC=OFF -DZSTD_BUILD_PROGRAMS=OFF -B build -G Ninja build/cmake || goto error
cmake --build build --parallel || goto error
ninja -C build install || goto error
cd .. || goto error

echo Building shaderc...
if exist "shaderc-%SHADERC%" rmdir /S /Q "shaderc-%SHADERC%"
"%TAR%" -xf "shaderc-%SHADERC%.zip" || goto error
cd "shaderc-%SHADERC%\third_party" || goto error
"%TAR%" -xf "..\..\shaderc-glslang-%SHADERC_GLSLANG%.zip" || goto error
rename "glslang-%SHADERC_GLSLANG%" "glslang" || goto error
"%TAR%" -xf "..\..\shaderc-spirv-headers-%SHADERC_SPIRVHEADERS%.zip" || goto error
rename "SPIRV-Headers-%SHADERC_SPIRVHEADERS%" "spirv-headers" || goto error
"%TAR%" -xf "..\..\shaderc-spirv-tools-%SHADERC_SPIRVTOOLS%.zip" || goto error
rename "SPIRV-Tools-%SHADERC_SPIRVTOOLS%" "spirv-tools" || goto error
cd .. || goto error
%PATCH% -p1 < "%SCRIPTDIR%\..\common\shaderc-changes.patch" || goto error
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%INSTALLDIR%" -DCMAKE_INSTALL_PREFIX="%INSTALLDIR%" -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON -DSHADERC_ENABLE_SHARED_CRT=ON -B build -G Ninja || goto error
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
