@echo off
rem Builds the PC and Android versions into dist\ (tools/dist.sh):
rem   dist\windows\OutbreakModern.exe, with the files it needs beside it
rem   dist\android\outbreak-modern.apk
setlocal
set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if not exist "%BASH%" (
    echo Git for Windows was not found at "%BASH%".
    set "RC=1"
    goto end
)
"%BASH%" "%~dp0tools\dist.sh"
set "RC=%ERRORLEVEL%"
:end
if "%RC%"=="0" (echo. & echo Done: see the dist folder.) else (echo. & echo Build failed: see the messages above.)
rem Keeps the window open when double-clicked; pass nopause from a terminal or script.
if /i not "%~1"=="nopause" pause
exit /b %RC%
