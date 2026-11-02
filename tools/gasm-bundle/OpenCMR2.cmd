@echo off
rem OpenCMR2 @VERSION@ on gasm-run @GASM_VERSION@. Double-click to play; OpenCMR2.cmd --help for the options.
rem The work is done by opencmr2.ps1 (game data picker, saved location, gasm-run command line).
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0opencmr2.ps1" %*
set rc=%errorlevel%
rem Keep the window open on errors when started by double-click (OPENCMR2_NO_PAUSE=1 skips this).
if not "%rc%"=="0" if not "%OPENCMR2_NO_PAUSE%"=="1" pause
exit /b %rc%
