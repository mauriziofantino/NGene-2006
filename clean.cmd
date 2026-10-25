@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-native-windows.ps1" -CleanOnly
exit /b %errorlevel%
