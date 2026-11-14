@echo off
setlocal

set "UNINSTALL_SCRIPT=%~dp0uninstall.ps1"
if not exist "%UNINSTALL_SCRIPT%" (
    echo Could not find uninstall.ps1 next to this script.
    pause
    exit /b 1
)

choice /C YN /N /M "Remove WowType for this Windows user? [Y/N] "
if errorlevel 2 exit /b 0

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%UNINSTALL_SCRIPT%"
if errorlevel 1 goto failed

echo.
echo WowType was removed. Translation settings and API key were kept.
pause
exit /b 0

:failed
echo.
echo WowType uninstall failed. See the PowerShell error above.
pause
exit /b 1
