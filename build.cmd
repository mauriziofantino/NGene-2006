@echo off
setlocal
if "%~1"=="" goto build
if /I "%~1"=="clean" goto rebuild
echo Usage: build.cmd [clean]
exit /b 2
:build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-native-windows.ps1"
exit /b %errorlevel%
:rebuild
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-native-windows.ps1" -Clean
exit /b %errorlevel%
