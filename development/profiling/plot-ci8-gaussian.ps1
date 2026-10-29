param([Parameter(Mandatory=$true)][string]$SummaryFile,
      [Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms.DataVisualization
$taskData=Get-Content -Raw -LiteralPath $SummaryFile | ConvertFrom-Json
$taskStats=$taskData.Statistics
$taskMu=@([double]$taskStats.MeanI,[double]$taskStats.MeanQ)
$taskSigma=@([Math]::Sqrt($taskStats.RmsI*$taskStats.RmsI-$taskMu[0]*$taskMu[0]),
             [Math]::Sqrt($taskStats.RmsQ*$taskStats.RmsQ-$taskMu[1]*$taskMu[1]))
$taskChart=New-Object System.Windows.Forms.DataVisualization.Charting.Chart
$taskChart.Width=1200;$taskChart.Height=850
$taskChart.BackColor=[Drawing.Color]::White
$taskLegend=New-Object System.Windows.Forms.DataVisualization.Charting.Legend
$taskLegend.Docking='Top';$taskChart.Legends.Add($taskLegend)
foreach($taskPanel in @('density','tails')) {
    $taskArea=New-Object System.Windows.Forms.DataVisualization.Charting.ChartArea $taskPanel
    $taskArea.AxisX.Title='ADC counts after subtracting each component mean'
    $taskArea.AxisX.Minimum=-130;$taskArea.AxisX.Maximum=130
    $taskArea.AxisY.Title='Probability per ADC count'
    $taskArea.AxisX.MajorGrid.LineColor=[Drawing.Color]::Gainsboro
    $taskArea.AxisY.MajorGrid.LineColor=[Drawing.Color]::Gainsboro
    if($taskPanel -eq 'tails') {
        $taskArea.AxisY.IsLogarithmic=$true
        $taskArea.AxisY.Minimum=1e-8;$taskArea.AxisY.Maximum=.1
    }
    $taskChart.ChartAreas.Add($taskArea)
    for($taskC=0;$taskC -lt 3;$taskC++) {
        $taskName=@('I','Q','Gaussian fit (measured sigma)')[$taskC]
        $taskSeries=New-Object System.Windows.Forms.DataVisualization.Charting.Series ($taskPanel+$taskName)
        $taskSeries.ChartType='Line';$taskSeries.ChartArea=$taskPanel
        $taskSeries.LegendText=$taskName;$taskSeries.IsVisibleInLegend=($taskPanel -eq 'density')
        $taskSeries.BorderWidth=2
        $taskSeries.Color=@([Drawing.Color]::SteelBlue,[Drawing.Color]::DarkOrange,[Drawing.Color]::Black)[$taskC]
        if($taskC -eq 2){$taskSeries.BorderDashStyle='Dash'}
        for($taskX=-128;$taskX -le 127;$taskX++) {
            if($taskC -lt 2) {
                $taskY=[double]$taskData.Histogram[$taskC][$taskX+128]/($taskStats.Bytes/2)
                $taskCentred=$taskX-$taskMu[$taskC]
            } else {
                $taskCentred=$taskX;$taskS=$taskStats.CentredRms
                $taskY=[Math]::Exp(-.5*$taskX*$taskX/($taskS*$taskS))/($taskS*[Math]::Sqrt(2*[Math]::PI))
            }
            if($taskY -gt 0){[void]$taskSeries.Points.AddXY($taskCentred,$taskY)}
        }
        $taskChart.Series.Add($taskSeries)
    }
}
$taskTitle=$taskChart.Titles.Add('HackRF: measured I/Q distribution, centred without amplitude rescaling')
$taskTitle.Font=New-Object Drawing.Font('Segoe UI',13)
$taskChart.SaveImage([IO.Path]::GetFullPath($OutputFile),'Png')
$taskChart.Dispose()
Write-Output $OutputFile
