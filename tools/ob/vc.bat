@echo off
rem Run one command inside the VS2022 x64 developer environment, with git core.longpaths on.
rem Used by tools/ob.sh for every cmake/ctest/MSVC step: vc.bat <command> [args...]
if not defined OB_VCVARS set "OB_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%OB_VCVARS%" >nul || exit /b 1
set GIT_CONFIG_COUNT=1
set GIT_CONFIG_KEY_0=core.longpaths
set GIT_CONFIG_VALUE_0=true
%*
exit /b %ERRORLEVEL%
