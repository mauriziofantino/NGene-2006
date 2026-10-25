@echo off
setlocal
if not exist "%~dp0ngene.exe" (
    echo ngene.exe is missing. Run build.cmd first.
    exit /b 1
)
pushd "%~dp0"
"%~dp0ngene.exe" -n %*
set "ngene_result=%errorlevel%"
popd
exit /b %ngene_result%
