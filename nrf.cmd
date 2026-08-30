@echo off
REM nRF Connect SDK wrapper (cmd.exe). Prefer: nrf build / nrf -Project demo flash
setlocal
set "SCRIPT_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '%SCRIPT_DIR%nrf.ps1' -- %*"
exit /b %ERRORLEVEL%
