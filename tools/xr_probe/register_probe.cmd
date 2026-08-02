@echo off
rem Double-click wrapper. The .ps1 next to this file does the work; this exists because a script
rem that arrived inside a zip carries the internet mark, and PowerShell then asks "do you want to
rem run this?" before it will start -- which a tester should not have to argue with. -ExecutionPolicy
rem Bypass applies to this invocation only and changes nothing on the machine.
rem
rem Registering an implicit API layer for an elevated game needs the machine-wide key, so this
rem re-launches itself elevated if it is not already.
setlocal
net session >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator rights ^(the OpenXR loader ignores per-user layers for an elevated game^)...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0register_probe.ps1" -Scope Machine
echo.
echo Play for a minute, then run unregister_probe.cmd and send what is in %%LOCALAPPDATA%%\xrprobe\.
pause
