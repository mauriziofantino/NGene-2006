param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$OutputDirectory){$OutputDirectory=Join-Path $taskRoot 'build/diagnostic-tools'}
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $taskOutput | Out-Null
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
foreach($taskInclude in @('frontEnd','receiver','asmFolder','acquisition','tracking','pvt','utility','utilityGnss','osnma','chimera','warmStart','secure','utilityGnss/NeQuick','libraries/libfec','libraries/libusb','build/native-deps/include','build/native-sources/pthreads4w','build/native-sources/PDCursesMod-4.5.4','platform/windows','platform/windows/include')){
    $taskArgs+=('/I'+(Join-Path $taskRoot $taskInclude))
}
Push-Location $taskOutput
try {
    & $taskCompiler @taskArgs (Join-Path $PSScriptRoot 'check-hackrf-ring.c') '/Fe:check-hackrf-ring.exe'
    if($LASTEXITCODE){throw 'Ring check compilation failed'}
    # Compile the actual channel window/index functions, without copying their
    # implementation into the harness or adding instrumentation to the main.
    $taskChannelSource = Join-Path $taskRoot 'receiver/channelManager.c'
    $taskChannelText = [IO.File]::ReadAllText($taskChannelSource)
    $taskExtracted = foreach ($taskFunction in @('GetSamplePointersForChannel','GetNumberOfUsedSamples','CleanBufferOffsetForAllChannels')) {
        $taskMatch = [regex]::Match($taskChannelText, '(?m)^(?:void|unsigned int) ' + $taskFunction + '\(')
        if (!$taskMatch.Success) { throw "Missing production function: $taskFunction" }
        $taskBrace = $taskChannelText.IndexOf('{', $taskMatch.Index)
        $taskDepth = 1
        $taskEnd = $taskBrace + 1
        while ($taskDepth -gt 0 -and $taskEnd -lt $taskChannelText.Length) {
            if ($taskChannelText[$taskEnd] -eq '{') { $taskDepth++ }
            if ($taskChannelText[$taskEnd] -eq '}') { $taskDepth-- }
            $taskEnd++
        }
        if ($taskDepth -ne 0) { throw "Unclosed production function: $taskFunction" }
        $taskChannelText.Substring($taskMatch.Index, $taskEnd - $taskMatch.Index)
    }
    [IO.File]::WriteAllText((Join-Path $taskOutput 'channel-timeline-production.inc'), ($taskExtracted -join "`n`n"))
    Get-FileHash -LiteralPath $taskChannelSource -Algorithm SHA256 | ConvertTo-Json | Set-Content (Join-Path $taskOutput 'channel-source.json')
    & $taskCompiler @taskArgs ('/I' + $taskOutput) '/DNGENE_CHECK_CHANNEL_TIMELINE' (Join-Path $PSScriptRoot 'check-hackrf-ring.c') '/Fe:check-input-channel-timeline.exe'
    if($LASTEXITCODE){throw 'Input/channel timeline check compilation failed'}
    $taskSources=@('asmFolder/fft.c','asmFolder/fft_sse2.c','asmFolder/hackrfInput.c','asmFolder/wideInput.c','platform/windows/native_compat.c') | ForEach-Object {Join-Path $taskRoot $_}
    & $taskCompiler @taskArgs (Join-Path $PSScriptRoot 'bench-fft-trials.c') @taskSources '/Fe:bench-fft-trials.exe' '/link' 'ws2_32.lib'
    if($LASTEXITCODE){throw 'FFT benchmark compilation failed'}
    & $taskCompiler @taskArgs (Join-Path $PSScriptRoot 'probe-gps-code-phase.c') '/Fe:probe-gps-code-phase.exe'
    if($LASTEXITCODE){throw 'Code-phase probe compilation failed'}
} finally { Pop-Location }
Write-Host "Diagnostic tools: $taskOutput"
