param([switch]$Clean, [switch]$CleanOnly, [switch]$DependenciesOnly, [int]$Jobs = 8)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$env:PERL_BADLANG = '0'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskDownloads = Join-Path $taskRoot 'build/native-downloads'
$taskSources = Join-Path $taskRoot 'build/native-sources'
$taskTools = Join-Path $taskRoot 'build/native-tools'
$taskPrefix = Join-Path $taskRoot 'build/native-deps'
New-Item -ItemType Directory -Force $taskDownloads,$taskSources,$taskTools,$taskPrefix | Out-Null

function Get-NativeArchive($Name, $Url, $Hash) {
    $taskFile = Join-Path $taskDownloads $Name
    if (!(Test-Path -LiteralPath $taskFile)) { Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $taskFile }
    if ((Get-FileHash -LiteralPath $taskFile -Algorithm SHA256).Hash -ne $Hash) { throw "Checksum mismatch: $Name" }
    return $taskFile
}
function Invoke-NativeTool($Program, $Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Native tool failed ($LASTEXITCODE): $Program" }
}

$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskVsRoot = & $taskVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVsRoot) { throw 'Visual Studio C++ x64 tools and Windows SDK are required.' }
$taskDevCmd = Join-Path $taskVsRoot 'Common7/Tools/VsDevCmd.bat'
$taskDeveloperEnvironment = & cmd /s /c ('"' + $taskDevCmd + '" -arch=x64 -host_arch=x64 >nul && set')
foreach ($taskLine in $taskDeveloperEnvironment) {
    if ($taskLine -match '^(PATH|INCLUDE|LIB|LIBPATH|WindowsSdkDir|WindowsSDKVersion|VCINSTALLDIR|VCToolsInstallDir)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
    }
}
$taskClang = Join-Path $taskVsRoot 'VC/Tools/Llvm/x64/bin/clang-cl.exe'
$taskCmake = Join-Path $taskVsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$taskNinja = Join-Path $taskVsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
if (!(Test-Path -LiteralPath $taskClang)) { throw 'Install the Visual Studio C++ Clang tools component (clang-cl).' }

if ($CleanOnly) {
    $taskBuild = Join-Path $taskRoot 'build/windows-native'
    if (Test-Path -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt')) {
        Invoke-NativeTool $taskCmake @('--build',$taskBuild,'--target','clean')
    }
    $taskExecutable = Join-Path $taskRoot 'ngene.exe'
    if (Test-Path -LiteralPath $taskExecutable) { Remove-Item -LiteralPath $taskExecutable -Force }
    Write-Host 'Clean completed. Configuration, recordings and dependency cache preserved.'
    return
}

$taskSslArchive = Get-NativeArchive 'openssl.tar.gz' 'https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz' '603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a'
$taskPdcArchive = Get-NativeArchive 'pdcurses.tar.gz' 'https://codeload.github.com/Bill-Gray/PDCursesMod/tar.gz/refs/tags/v4.5.4' 'd5efc7f2b7107abe382bdf8bac0a9bfd8e716facbca2bb9cf12dfeb8e1122c4b'
$taskPthreadArchive = Get-NativeArchive 'pthreads4w-native.tar.gz' 'https://codeload.github.com/jwinarske/pthreads4w/tar.gz/44daa2441137b90477b449663abe9755b2c9a16b' '82dc760207f0c3f492fe4ffe2c081741bcbbd7a54d71153a54cf2971d7d64f54'
$taskHackrfArchive = Get-NativeArchive 'hackrf-v2026.01.3.tar.gz' 'https://codeload.github.com/greatscottgadgets/hackrf/tar.gz/refs/tags/v2026.01.3' '48238f3a21189fa8cbe67838584cc045b9e5433767db8c9c30c1da02a1489f2c'
$taskPerlArchive = Get-NativeArchive 'strawberry.zip' 'https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/download/SP_54231_64bit/strawberry-perl-5.42.3.1-64bit-portable.zip' '6a081a811781c30aca51dbc036afd93092af91e3297901f02c17043795a10690'
$taskTar = Join-Path $env:SystemRoot 'System32/tar.exe'
if (!(Test-Path (Join-Path $taskSources 'openssl-3.5.9/Configure'))) { Invoke-NativeTool $taskTar @('-xf',$taskSslArchive,'-C',$taskSources) }
if (!(Test-Path (Join-Path $taskSources 'PDCursesMod-4.5.4/curses.h'))) { Invoke-NativeTool $taskTar @('-xf',$taskPdcArchive,'-C',$taskSources) }
if (!(Test-Path (Join-Path $taskSources 'hackrf-2026.01.3/host/libhackrf/src/hackrf.c'))) { Invoke-NativeTool $taskTar @('-xf',$taskHackrfArchive,'-C',$taskSources) }
$taskPthreadSource = Join-Path $taskSources 'pthreads4w'
if (!(Test-Path (Join-Path $taskPthreadSource 'pthread.c'))) {
    New-Item -ItemType Directory -Force $taskPthreadSource | Out-Null
    Invoke-NativeTool $taskTar @('-xf',$taskPthreadArchive,'-C',$taskPthreadSource,'--strip-components=1')
}
$taskPerlRoot = Join-Path $taskTools 'perl'
$taskPerl = Join-Path $taskPerlRoot 'perl/bin/perl.exe'
if (!(Test-Path -LiteralPath $taskPerl)) {
    New-Item -ItemType Directory -Force $taskPerlRoot | Out-Null
    Invoke-NativeTool $taskTar @('-xf',$taskPerlArchive,'-C',$taskPerlRoot,'perl')
}

if (!(Test-Path (Join-Path $taskPrefix 'lib/libcrypto.lib')) -or
    !(Test-Path (Join-Path $taskPrefix 'lib/libssl.lib')) -or
    !(Test-Path (Join-Path $taskPrefix 'include/openssl/ssl.h'))) {
    Push-Location (Join-Path $taskSources 'openssl-3.5.9')
    try {
        $taskShortPrefix = (& cmd /c ('for %I in ("' + $taskPrefix + '") do @echo %~fsI')).Trim().Replace('\','/')
        Invoke-NativeTool $taskPerl @('Configure','VC-WIN64A','no-shared','no-tests','no-asm','no-apps',('--prefix=' + $taskShortPrefix),'--libdir=lib')
        Invoke-NativeTool 'nmake.exe' @('/NOLOGO')
        Invoke-NativeTool 'nmake.exe' @('/NOLOGO','install_sw')
    } finally { Pop-Location }
}
if ($DependenciesOnly) { return }
$taskBuild = Join-Path $taskRoot 'build/windows-native'
Invoke-NativeTool $taskCmake @('-S',$taskRoot,'-B',$taskBuild,'-G','Ninja',('-DCMAKE_MAKE_PROGRAM=' + $taskNinja),('-DCMAKE_C_COMPILER=' + $taskClang),'-DCMAKE_BUILD_TYPE=Release','-DNGENE_BUILD_PLATFORM_CHECKS=ON',('-DNGENE_NATIVE_DEPS_DIR=' + $taskPrefix),('-DNGENE_NATIVE_SOURCES_DIR=' + $taskSources))
$taskBuildArguments = @('--build',$taskBuild,'--parallel',"$Jobs")
if ($Clean) { $taskBuildArguments += '--clean-first' }
Invoke-NativeTool $taskCmake $taskBuildArguments
Invoke-NativeTool (Join-Path (Split-Path $taskCmake) 'ctest.exe') @('--test-dir',$taskBuild,'--output-on-failure')

# Portable configuration: copy missing files only, preserving local settings.
$ConfigurationSource = Join-Path $taskRoot 'sampleConfigurationFiles'
foreach ($taskFolder in @('configuration','otherData')) {
    $taskOrigin = Join-Path $ConfigurationSource $taskFolder
    if (!(Test-Path -LiteralPath $taskOrigin -PathType Container)) { throw "Missing configuration folder: $taskOrigin" }
    $taskDestination = Join-Path $taskRoot $taskFolder
    New-Item -ItemType Directory -Force $taskDestination | Out-Null
    foreach ($taskItem in Get-ChildItem -LiteralPath $taskOrigin -Recurse -File) {
        $taskRelative = $taskItem.FullName.Substring($taskOrigin.TrimEnd('\','/').Length).TrimStart('\','/')
        $taskTarget = Join-Path $taskDestination $taskRelative
        if (!(Test-Path -LiteralPath $taskTarget)) {
            New-Item -ItemType Directory -Force (Split-Path $taskTarget) | Out-Null
            Copy-Item -LiteralPath $taskItem.FullName -Destination $taskTarget
        }
    }
}
Copy-Item -LiteralPath (Join-Path $taskBuild 'ngene.exe') -Destination (Join-Path $taskRoot 'ngene.exe') -Force
Write-Host "Executable: $taskRoot/ngene.exe"
Write-Host "Configuration: $taskRoot/configuration/GNSS_RX.conf"
Write-Host 'Start: run.cmd    Recording: run.cmd --input recordings\native-ab.raw'
