param(
    [Parameter(Mandatory=$true)][string]$LogFile,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [double]$SampleRateHz = 0,
    [ValidateRange(1,10000)][int]$WindowMs = 100
)
$ErrorActionPreference = 'Stop'
$taskSource = (Resolve-Path -LiteralPath $LogFile).Path
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
$taskLines = [IO.File]::ReadAllLines($taskSource)
$taskInvariant = [Globalization.CultureInfo]::InvariantCulture
if ($SampleRateHz -eq 0) {
    foreach ($taskLine in $taskLines) {
        if ($taskLine -match 'Front end:.*\bfs ([\d.]+) Hz') {
            $SampleRateHz = [double]::Parse($Matches[1], $taskInvariant)
            break
        }
    }
}
if ([double]::IsNaN($SampleRateHz) -or [double]::IsInfinity($SampleRateHz) -or $SampleRateHz -le 0) {
    throw 'A positive SampleRateHz is required when the log has no frontend sampling rate.'
}
$taskEvents = [Collections.Generic.List[object]]::new()
$taskInputEvents = [Collections.Generic.List[object]]::new()
for ($taskIndex = 0; $taskIndex -lt $taskLines.Length; $taskIndex++) {
    $taskLine = $taskLines[$taskIndex]
    if ($taskLine -match 'LIBUSB_TRANSFER_|sample discontinuity|USB sample buffer full|RX stopped:|USB errors found|Hard Receiver Restart') {
        $taskInputEvents.Add([pscustomobject]@{Line=$taskIndex+1; Message=$taskLine})
    }
    if ($taskLine -notmatch 'Channel\s+(\d+)\s+(\S+)\s+(.+?) at samples? (\d+)(.*)$') { continue }
    $taskChannel = [int]$Matches[1]
    $taskPrn = $Matches[2]
    $taskDescription = $Matches[3]
    $taskSample = [long]$Matches[4]
    $taskKind = switch -Regex ($taskDescription) {
        '^tracking lost' { 'lost'; break }
        '^tracking holdover' { 'holdover'; break }
        '^tracking recovered' { 'recovered'; break }
        '^found a problem with' { 'lock-warning'; break }
        '^tracking starts' { 'tracking-start'; break }
        '^again locked' { 'lock-return'; break }
        default { $null }
    }
    if ($null -eq $taskKind) { continue }
    $taskEvents.Add([pscustomobject]@{
        Line=$taskIndex+1; Channel=$taskChannel; Prn=$taskPrn; Kind=$taskKind
        Sample=$taskSample; SignalMs=1000.0*$taskSample/$SampleRateHz; Message=$taskLine
    })
}
# Group each transition kind on the common sample timeline, never per-channel
# tracking counters or host timestamps. Anchor at the first event: a chain of
# neighbouring events must not inflate the requested simultaneity window.
$taskGroups = [Collections.Generic.List[object]]::new()
foreach ($taskKind in @('lost','holdover','recovered','lock-warning','tracking-start','lock-return')) {
    $taskSorted = @($taskEvents | Where-Object Kind -eq $taskKind | Sort-Object Sample,Line)
    $taskStart = 0
    while ($taskStart -lt $taskSorted.Count) {
        $taskEnd = $taskStart+1
        while ($taskEnd -lt $taskSorted.Count -and
               $taskSorted[$taskEnd].SignalMs-$taskSorted[$taskStart].SignalMs -le $WindowMs) { $taskEnd++ }
        $taskMembers = @($taskSorted[$taskStart..($taskEnd-1)])
        $taskChannels = @($taskMembers.Channel | Sort-Object -Unique)
        if ($taskChannels.Count -ge 2) {
            $taskGroups.Add([pscustomobject]@{
                Kind=$taskKind; StartSignalMs=$taskMembers[0].SignalMs
                SpanMs=$taskMembers[-1].SignalMs-$taskMembers[0].SignalMs
                DistinctChannels=$taskChannels.Count; Channels=$taskChannels -join ','
                Prns=(@($taskMembers.Prn | Sort-Object -Unique) -join ',')
                Lines=($taskMembers.Line -join ',')
            })
        }
        $taskStart=$taskEnd
    }
}
New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskEvents | Export-Csv -LiteralPath (Join-Path $taskOutput 'transitions.csv') -NoTypeInformation
$taskGroups | Sort-Object StartSignalMs,Kind | Export-Csv -LiteralPath (Join-Path $taskOutput 'simultaneous.csv') -NoTypeInformation
$taskInputEvents | Export-Csv -LiteralPath (Join-Path $taskOutput 'input-events.csv') -NoTypeInformation
$taskSummary = [pscustomobject]@{
    Log=$taskSource; SampleRateHz=$SampleRateHz; WindowMs=$WindowMs
    TrackingLosses=@($taskEvents | Where-Object Kind -eq 'lost').Count
    Holdovers=@($taskEvents | Where-Object Kind -eq 'holdover').Count
    Recoveries=@($taskEvents | Where-Object Kind -eq 'recovered').Count
    SimultaneousLossGroups=@($taskGroups | Where-Object Kind -eq 'lost').Count
    InputEvents=$taskInputEvents.Count
    Limitation='Clustering is evidence of timing, not causation. Input events retain log line order; unknown missing samples cannot be inferred. No inference about channels absent from the log.'
}
$taskSummary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'summary.json')
$taskSummary
