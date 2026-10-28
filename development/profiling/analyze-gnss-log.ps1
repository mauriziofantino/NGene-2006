param(
    [Parameter(Mandatory=$true)][string]$LogFile,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][double]$ReferenceLatitude,
    [Parameter(Mandatory=$true)][double]$ReferenceLongitude,
    [Parameter(Mandatory=$true)][double]$ReferenceHeight
)
$ErrorActionPreference = 'Stop'
$taskLog = (Resolve-Path -LiteralPath $LogFile).Path
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskInvariant = [Globalization.CultureInfo]::InvariantCulture
function Number($value) { [double]::Parse($value, $taskInvariant) }
function Ecef([double]$latitude,[double]$longitude,[double]$height) {
    $taskPhi = $latitude * [Math]::PI / 180
    $taskLambda = $longitude * [Math]::PI / 180
    $taskE2 = 6.6943799901413165e-3
    $taskN = 6378137 / [Math]::Sqrt(1 - $taskE2 * [Math]::Pow([Math]::Sin($taskPhi),2))
    $taskX=($taskN+$height)*[Math]::Cos($taskPhi)*[Math]::Cos($taskLambda)
    $taskY=($taskN+$height)*[Math]::Cos($taskPhi)*[Math]::Sin($taskLambda)
    $taskZ=($taskN*(1-$taskE2)+$height)*[Math]::Sin($taskPhi)
    @($taskX,$taskY,$taskZ)
}
$taskReference = Ecef $ReferenceLatitude $ReferenceLongitude $ReferenceHeight
$taskPhi = $ReferenceLatitude * [Math]::PI / 180
$taskLambda = $ReferenceLongitude * [Math]::PI / 180
$taskFixes = New-Object 'Collections.Generic.List[object]'
$taskEvents = New-Object 'Collections.Generic.List[object]'
$taskEpoch = $null
$taskLineNumber = 0
$taskLines = [IO.File]::ReadAllLines($taskLog)
foreach ($taskLine in $taskLines) {
    $taskLineNumber++
    if ($taskLine -match 'PVT COUNTER\s+(\d+)') {
        $taskEpoch = [ordered]@{ Counter=[int]$Matches[1]; RxMs=$null; GpsWeek=$null; GpsTow=$null; GpsSatellites=0; GalileoSatellites=0; Gdop=$null; Host=''; Utc=''; Solver='' }
        if ($taskLine -match '^\[([^\]]+)\]') { $taskEpoch.Host = $Matches[1] }
    }
    if ($null -ne $taskEpoch) {
        if ($taskLine -match 'PVT RESULTS \((LS|KF)\)') { $taskEpoch.Solver=$Matches[1] }
        if ($taskLine -match '# RX\s*:\s*(\d+)') { $taskEpoch.RxMs = [long]$Matches[1] }
        if ($taskLine -match '# GPS\s+WN\s*:\s*(\d+)\s+TOW\s*:\s*([\d.]+)') { $taskEpoch.GpsWeek=[int]$Matches[1]; $taskEpoch.GpsTow=Number $Matches[2] }
        if ($taskLine -match '# UTC\s+(\d+/\d+/\d+\s+[\d:.]+)') { $taskEpoch.Utc = $Matches[1] }
        if ($taskLine -match '# GPS\s*:\s*(\d+) satellites') { $taskEpoch.GpsSatellites=[int]$Matches[1] }
        if ($taskLine -match '# Galileo\s*:\s*(\d+) satellites') { $taskEpoch.GalileoSatellites=[int]$Matches[1] }
        if ($taskLine -match '# GDOP\s*:\s*([\d.]+)') { $taskEpoch.Gdop=Number $Matches[1] }
        if ($taskLine -match '# Lon:\s*([\d.+-]+)d\s+Lat:\s*([\d.+-]+)d\s+Alt:\s*([\d.+-]+)m') {
            $taskLon=Number $Matches[1]; $taskLat=Number $Matches[2]; $taskHeight=Number $Matches[3]
            $taskPosition=Ecef $taskLat $taskLon $taskHeight
            $taskDx=$taskPosition[0]-$taskReference[0]; $taskDy=$taskPosition[1]-$taskReference[1]; $taskDz=$taskPosition[2]-$taskReference[2]
            $taskEast=-[Math]::Sin($taskLambda)*$taskDx+[Math]::Cos($taskLambda)*$taskDy
            $taskNorth=-[Math]::Sin($taskPhi)*[Math]::Cos($taskLambda)*$taskDx-[Math]::Sin($taskPhi)*[Math]::Sin($taskLambda)*$taskDy+[Math]::Cos($taskPhi)*$taskDz
            $taskUp=[Math]::Cos($taskPhi)*[Math]::Cos($taskLambda)*$taskDx+[Math]::Cos($taskPhi)*[Math]::Sin($taskLambda)*$taskDy+[Math]::Sin($taskPhi)*$taskDz
            $taskFixes.Add([pscustomobject]@{
                Counter=$taskEpoch.Counter; Solver=$taskEpoch.Solver; Host=$taskEpoch.Host; Utc=$taskEpoch.Utc; RxMs=$taskEpoch.RxMs; GpsWeek=$taskEpoch.GpsWeek; GpsTow=$taskEpoch.GpsTow
                GpsSatellites=$taskEpoch.GpsSatellites; GalileoSatellites=$taskEpoch.GalileoSatellites; Gdop=$taskEpoch.Gdop
                Latitude=$taskLat; Longitude=$taskLon; Height=$taskHeight
                East=$taskEast; North=$taskNorth; Up=$taskUp; HeightError=$taskHeight-$ReferenceHeight
                Horizontal=[Math]::Sqrt($taskEast*$taskEast+$taskNorth*$taskNorth)
                Error3D=[Math]::Sqrt($taskDx*$taskDx+$taskDy*$taskDy+$taskDz*$taskDz)
            })
            $taskEpoch=$null
        }
    }
    if ($taskLine -match 'RX stopped:|stop RX:|cannot stop|tracking lost|again locked|found a problem with|Hard Receiver Restart|PVT failure') {
        $taskEvents.Add([pscustomobject]@{Line=$taskLineNumber;Message=$taskLine})
    }
}
function Statistics($points) {
    if ($points.Count -eq 0) { return $null }
    $taskStats=[ordered]@{Count=$points.Count;FirstRxMs=$points[0].RxMs;LastRxMs=$points[-1].RxMs}
    foreach ($taskAxis in @('East','North','Up','HeightError','Horizontal','Error3D')) {
        $taskValues=@($points | ForEach-Object {[double]$_.$taskAxis})
        $taskMean=($taskValues | Measure-Object -Average).Average
        $taskSquare=0.0; $taskDeviation=0.0
        foreach ($taskValue in $taskValues) { $taskSquare+=$taskValue*$taskValue; $taskDeviation+=($taskValue-$taskMean)*($taskValue-$taskMean) }
        $taskSorted=@($taskValues | Sort-Object)
        $taskStats[$taskAxis]=[ordered]@{
            Mean=$taskMean;PopulationSD=[Math]::Sqrt($taskDeviation/$taskValues.Count)
            RMS=[Math]::Sqrt($taskSquare/$taskValues.Count)
            P95NearestRank=$taskSorted[[Math]::Ceiling(.95*$taskSorted.Count)-1]
            Minimum=$taskSorted[0];Maximum=$taskSorted[-1]
        }
    }
    $taskStats
}
$taskAll=@($taskFixes.ToArray())
$taskLast60=@()
if ($taskAll.Count) { $taskLast60=@($taskAll | Where-Object {$_.RxMs -ge $taskAll[-1].RxMs-60000}) }
$taskWholeText=$taskLines -join "`n"
$taskSummary=[ordered]@{
    InputLog=$taskLog;Reference=[ordered]@{Latitude=$ReferenceLatitude;Longitude=$ReferenceLongitude;EllipsoidalHeight=$ReferenceHeight}
    Method='WGS84 ECEF difference rotated to ENU at reference. Population SD; nearest-rank P95. Positions parsed from rounded text output.'
    AllFixes=(Statistics $taskAll);Last60Seconds=(Statistics $taskLast60)
    MeasurementSolutions=(Statistics @($taskAll | Where-Object {($_.GpsSatellites+$_.GalileoSatellites) -gt 0}))
    PredictionOnlySolutions=(Statistics @($taskAll | Where-Object {($_.GpsSatellites+$_.GalileoSatellites) -eq 0}))
    TrackingLost=([regex]::Matches($taskWholeText,'tracking lost')).Count
    PllWarnings=([regex]::Matches($taskWholeText,'found a problem with PLL lock')).Count
    RecoveredLocks=([regex]::Matches($taskWholeText,'again locked')).Count
    HardRestarts=([regex]::Matches($taskWholeText,'Hard Receiver Restart')).Count
    PvtFailures=([regex]::Matches($taskWholeText,'PVT failure')).Count
    GdopRejections=([regex]::Matches($taskWholeText,'PVT results discarded due to a bad GDOP')).Count
    QueueOverflows=([regex]::Matches($taskWholeText,'RX stopped: receiver queue overflow')).Count
}
$taskAll | Export-Csv -LiteralPath (Join-Path $taskOutput 'fixes.csv') -NoTypeInformation
$taskEvents.ToArray() | Export-Csv -LiteralPath (Join-Path $taskOutput 'events.csv') -NoTypeInformation
[IO.File]::WriteAllText((Join-Path $taskOutput 'summary.json'),(ConvertTo-Json -InputObject $taskSummary -Depth 8),(New-Object Text.UTF8Encoding($false)))
[pscustomobject]$taskSummary
