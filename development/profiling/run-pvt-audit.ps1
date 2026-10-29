param([string]$PythonExe=(Join-Path $env:LOCALAPPDATA 'vcpkg/downloads/tools/python/python-3.14.2-x64-1/python.exe'),[switch]$Verify)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput=Join-Path $taskRoot 'build/icd-audit'
& $PythonExe (Join-Path $PSScriptRoot 'extract-pvt-audit.py')
if($LASTEXITCODE){throw 'Source extraction failed'}
$taskVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskVsRoot=& $taskVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$taskVsRoot){throw 'Visual Studio x64 C++ tools required'}
$taskDevCmd=Join-Path $taskVsRoot 'Common7/Tools/VsDevCmd.bat'
$taskEnvironment=& cmd /s /c ('"'+$taskDevCmd+'" -arch=x64 -host_arch=x64 >nul && set')
foreach($taskLine in $taskEnvironment){
    if($taskLine -match '^(PATH|INCLUDE|LIB|LIBPATH)=(.*)$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}
}
$taskCompiler=Join-Path $taskVsRoot 'VC/Tools/Llvm/x64/bin/clang-cl.exe'
$taskArgs=@('/nologo','/std:c11','/MT','/O2','/DNGENE_NATIVE_WINDOWS','/DWIN32_LEAN_AND_MEAN','/DNOMINMAX','/D_USE_MATH_DEFINES','/D_CRT_SECURE_NO_WARNINGS',('/FI'+(Join-Path $taskRoot 'platform/windows/native_compat.h')))
foreach($taskInclude in @('development/profiling','frontEnd','receiver','asmFolder','acquisition','tracking','pvt','utility','utilityGnss','osnma','chimera','warmStart','secure','utilityGnss/NeQuick','libraries/libfec','libraries/libusb','build/native-deps/include','build/native-sources/pthreads4w','build/native-sources/PDCursesMod-4.5.4','platform/windows','platform/windows/include')){
    $taskArgs+=('/I'+(Join-Path $taskRoot $taskInclude))
}
Push-Location $taskOutput
try {
    & $taskCompiler @taskArgs 'ngene-audit-extracted.c' (Join-Path $taskRoot 'build/windows-native/ngene_pnt_numerics.lib') '/Fe:audit-pvt.exe'
    if($LASTEXITCODE){throw 'Audit compilation failed'}
    $taskRunArgs=@();if($Verify){$taskRunArgs+='--verify'}
    & './audit-pvt.exe' @taskRunArgs | Tee-Object -Variable taskAuditLines
    $taskAuditExit=$LASTEXITCODE
    [IO.File]::WriteAllLines((Join-Path $taskOutput 'audit-results.csv'),
        [string[]]$taskAuditLines,[Text.UTF8Encoding]::new($false))
    if($taskAuditExit){throw 'Audit executable failed'}
} finally {Pop-Location}
Write-Host 'PASS/MISMATCH refers to the independent reference, not to test harness execution.'
