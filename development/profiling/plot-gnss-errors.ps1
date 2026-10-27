param(
    [Parameter(Mandatory=$true)][string[]]$CsvFiles,
    [Parameter(Mandatory=$true)][string[]]$Labels,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
if($CsvFiles.Count -ne $Labels.Count){throw 'Each CSV requires one label'}
Add-Type -AssemblyName System.Windows.Forms.DataVisualization
Add-Type -AssemblyName System.Drawing
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskPalette=@('RoyalBlue','DarkOrange','ForestGreen','DarkViolet')
function Chart($title,$x,$y){
    $taskChart=New-Object Windows.Forms.DataVisualization.Charting.Chart
    $taskChart.Width=1200;$taskChart.Height=850;$taskChart.BackColor=[Drawing.Color]::White
    $taskArea=New-Object Windows.Forms.DataVisualization.Charting.ChartArea 'errors'
    $taskArea.AxisX.Title=$x;$taskArea.AxisY.Title=$y
    $taskArea.AxisX.MajorGrid.LineColor=[Drawing.Color]::Gainsboro
    $taskArea.AxisY.MajorGrid.LineColor=[Drawing.Color]::Gainsboro
    $taskChart.ChartAreas.Add($taskArea)
    $taskChart.Titles.Add($title) | Out-Null
    $taskChart.Legends.Add((New-Object Windows.Forms.DataVisualization.Charting.Legend 'cases'))
    $taskChart
}
$taskEnu=Chart 'Errori di posizione rispetto alla ground truth WGS84' 'Est [m]' 'Nord [m]'
$taskEnu.ChartAreas[0].AxisX.Minimum=-20;$taskEnu.ChartAreas[0].AxisX.Maximum=20
$taskEnu.ChartAreas[0].AxisY.Minimum=-20;$taskEnu.ChartAreas[0].AxisY.Maximum=20
$taskHeight=Chart 'Errore di quota: sessione live e cattura RF successiva' 'Tempo dal primo campione della rispettiva sessione [s]' 'Errore quota ellissoidica [m]'
for($taskIndex=0;$taskIndex -lt $CsvFiles.Count;$taskIndex++){
    $taskRows=Import-Csv -LiteralPath $CsvFiles[$taskIndex]
    $taskScatter=New-Object Windows.Forms.DataVisualization.Charting.Series $Labels[$taskIndex]
    $taskScatter.ChartType='Point';$taskScatter.MarkerSize=5;$taskScatter.Color=[Drawing.Color]::FromName($taskPalette[$taskIndex%4])
    $taskVertical=New-Object Windows.Forms.DataVisualization.Charting.Series $Labels[$taskIndex]
    $taskVertical.ChartType='Point';$taskVertical.MarkerSize=5;$taskVertical.Color=$taskScatter.Color
    foreach($taskRow in $taskRows){
        [void]$taskScatter.Points.AddXY([double]$taskRow.East,[double]$taskRow.North)
        [void]$taskVertical.Points.AddXY(([double]$taskRow.RxMs/1000),[double]$taskRow.HeightError)
    }
    $taskEnu.Series.Add($taskScatter);$taskHeight.Series.Add($taskVertical)
}
$taskReference=New-Object Windows.Forms.DataVisualization.Charting.Series 'Ground truth'
$taskReference.ChartType='Point';$taskReference.MarkerStyle='Cross';$taskReference.MarkerSize=15;$taskReference.Color=[Drawing.Color]::Black
[void]$taskReference.Points.AddXY(0,0);$taskEnu.Series.Add($taskReference)
$taskEnu.SaveImage((Join-Path $taskOutput 'accuracy-enu.png'),'Png')
$taskHeight.SaveImage((Join-Path $taskOutput 'accuracy-height.png'),'Png')
$taskEnu.Dispose();$taskHeight.Dispose()
