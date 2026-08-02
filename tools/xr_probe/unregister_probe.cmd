@echo off
rem See register_probe.cmd for why this wrapper exists.
setlocal
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0unregister_probe.ps1"
echo.
echo The logs are kept. They are in %%LOCALAPPDATA%%\xrprobe\ -- send the .txt and the .jsonl.
pause
