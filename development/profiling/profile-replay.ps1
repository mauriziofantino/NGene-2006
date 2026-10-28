param(
    [Parameter(Mandatory=$true)][string]$InputFile,
    [ValidateSet('sige','hackrf')][string]$Frontend = 'sige',
    [string]$Executable,
    [ValidateRange(0,64)][Nullable[int]]$GpsChannels,
    [ValidateRange(0,64)][Nullable[int]]$GalileoChannels,
    [ValidateRange(0,1)][Nullable[int]]$EgnosChannels,
    [ValidateRange(0,1)][Nullable[int]]$PvtMethod,
    [ValidateRange(1,20)][int]$Repetitions = 3,
    [ValidateRange(1,3600)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$Executable) { $Executable = Join-Path $taskRoot 'ngene.exe' }
$taskExe = (Resolve-Path -LiteralPath $Executable).Path
$taskInput = (Resolve-Path -LiteralPath $InputFile).Path
$taskSession = Join-Path $taskRoot ('build/profile-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $taskSession | Out-Null
$taskResults = @()
for ($taskRun=1; $taskRun -le $Repetitions; $taskRun++) {
    $taskDirectory = Join-Path $taskSession "run-$taskRun"
    New-Item -ItemType Directory -Path $taskDirectory | Out-Null
    Copy-Item -LiteralPath $taskExe -Destination (Join-Path $taskDirectory 'ngene.exe')
    Copy-Item -LiteralPath (Join-Path $taskRoot 'configuration') -Destination $taskDirectory -Recurse
    Copy-Item -LiteralPath (Join-Path $taskRoot 'otherData') -Destination $taskDirectory -Recurse
    $taskRxConfig = Join-Path $taskDirectory 'configuration/GNSS_RX.conf'
    $taskRxText = [IO.File]::ReadAllText($taskRxConfig)
    $taskOverrides = @{
        'Number of supported GPS channels' = $GpsChannels
        'Number of supported Galileo channels' = $GalileoChannels
        'Number of supported EGNOS channels' = $EgnosChannels
    }
    if ($null -ne $GpsChannels -or $null -ne $GalileoChannels -or $null -ne $EgnosChannels) {
        if ($taskRxText -notmatch '\[Automatic Configuration\]\s*1\s') {
            throw 'Channel overrides require Automatic Configuration = 1.'
        }
        foreach ($taskTag in $taskOverrides.Keys) {
            if ($null -ne $taskOverrides[$taskTag]) {
                $taskPattern = '(\[' + [regex]::Escape($taskTag) + '\][ \t]*\r?\n)[ \t]*\d+'
                if ($taskRxText -notmatch $taskPattern) { throw "Missing channel tag: $taskTag" }
                $taskRxText = [regex]::Replace($taskRxText, $taskPattern, ('${1}' + $taskOverrides[$taskTag]))
            }
        }
        [IO.File]::WriteAllText($taskRxConfig, $taskRxText)
    }
    # Keep all generated logs local even if the user's configuration has Dump Path.
    if ($null -ne $PvtMethod) {
        $taskPvtPath = Join-Path $taskDirectory 'configuration/PVT.conf'
        $taskPvtText = [IO.File]::ReadAllText($taskPvtPath)
        $taskPvtPattern = '(\[PVT method\][ \t]*\r?\n)[ \t]*\d+'
        if ($taskPvtText -notmatch $taskPvtPattern) { throw 'PVT method tag missing' }
        [IO.File]::WriteAllText($taskPvtPath,[regex]::Replace($taskPvtText,$taskPvtPattern,('${1}'+$PvtMethod)))
    }
    $taskLogConfig = Join-Path $taskDirectory 'configuration/Logs.conf'
    $taskText = [IO.File]::ReadAllText($taskLogConfig)
    $taskText = [regex]::Replace($taskText, '(?m)^\s*\[Dump Path\]\s*\r?\n[^\r\n]*', '# Dump Path omitted for isolated replay')
    [IO.File]::WriteAllText($taskLogConfig, $taskText)
    $taskWatch = [Diagnostics.Stopwatch]::StartNew()
    $taskProcess = New-Object Diagnostics.Process
    $taskProcess.StartInfo.FileName = Join-Path $taskDirectory 'ngene.exe'
    $taskProcess.StartInfo.WorkingDirectory = $taskDirectory
    $taskProcess.StartInfo.Arguments = '-t --frontend ' + $Frontend + ' --input "' + $taskInput + '"'
    $taskProcess.StartInfo.UseShellExecute = $false
    $taskProcess.StartInfo.CreateNoWindow = $true
    $taskProcess.StartInfo.RedirectStandardOutput = $true
    $taskProcess.StartInfo.RedirectStandardError = $true
    [void]$taskProcess.Start()
    $taskOutput = $taskProcess.StandardOutput.ReadToEndAsync()
    $taskError = $taskProcess.StandardError.ReadToEndAsync()
    $taskTimedOut = !$taskProcess.WaitForExit($TimeoutSeconds * 1000)
    if ($taskTimedOut) {
        $taskProcess.Kill()
        $taskProcess.WaitForExit()
    }
    $taskWatch.Stop()
    [IO.File]::WriteAllText((Join-Path $taskDirectory 'stdout.log'), $taskOutput.Result)
    [IO.File]::WriteAllText((Join-Path $taskDirectory 'stderr.log'), $taskError.Result)
    $taskResult = [pscustomobject]@{
        Run = $taskRun; Frontend = $Frontend; Input = $taskInput; PvtMethodOverride = $PvtMethod
        InputBytes = (Get-Item -LiteralPath $taskInput).Length
        ExecutableSha256 = (Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash
        WallSeconds = $taskWatch.Elapsed.TotalSeconds
        CpuSeconds = $taskProcess.TotalProcessorTime.TotalSeconds
        ExitCode = $taskProcess.ExitCode; Directory = $taskDirectory
    }
    $taskResults += $taskResult
    $taskProcess.Dispose()
    ConvertTo-Json -InputObject $taskResults | Set-Content -LiteralPath (Join-Path $taskSession 'results.json')
    $taskResult
    if ($taskTimedOut) { throw "Replay timeout; diagnostics: $taskDirectory" }
    if ($taskResult.ExitCode -ne 0) { throw "Replay failed: $taskDirectory" }
}
Write-Host "Profiles and logs: $taskSession"
