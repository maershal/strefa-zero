param([string]$Results)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms.DataVisualization
Add-Type -AssemblyName System.Drawing
function New-Plot([string]$Title,[string]$YTitle,[double]$Maximum){
    $chart=New-Object System.Windows.Forms.DataVisualization.Charting.Chart
    $chart.Width=1050;$chart.Height=550;$chart.BackColor=[Drawing.Color]::White
    $area=New-Object System.Windows.Forms.DataVisualization.Charting.ChartArea 'main'
    $area.AxisY.Minimum=0;$area.AxisY.Maximum=$Maximum;$area.AxisY.Title=$YTitle
    $area.AxisX.MajorGrid.Enabled=$false;$area.AxisY.MajorGrid.LineColor=[Drawing.ColorTranslator]::FromHtml('#e4e8ee')
    $area.AxisX.LabelStyle.Font=New-Object Drawing.Font('Segoe UI',13)
    $area.AxisY.LabelStyle.Font=New-Object Drawing.Font('Segoe UI',12)
    $area.AxisY.TitleFont=New-Object Drawing.Font('Segoe UI',13)
    $chart.ChartAreas.Add($area)
    $titleItem=$chart.Titles.Add($Title);$titleItem.Font=New-Object Drawing.Font('Segoe UI',17,[Drawing.FontStyle]::Bold)
    $series=New-Object System.Windows.Forms.DataVisualization.Charting.Series 'data'
    $series.ChartType=[System.Windows.Forms.DataVisualization.Charting.SeriesChartType]::Column
    $series.IsValueShownAsLabel=$true;$series.LabelFormat='0.000';$series.Font=New-Object Drawing.Font('Segoe UI',14)
    $chart.Series.Add($series)
    return ,$chart
}
$blocks=Get-Content -LiteralPath (Join-Path $Results 'abba-summary.json') -Raw | ConvertFrom-Json
$plot=New-Plot 'I07: dodatkowy koszt pełnego profilu balanced' 'Przyrost GPU [ms / tick]' 0.36
$plot.ChartAreas[0].AxisY.Interval=.05;$plot.ChartAreas[0].AxisY.LabelStyle.Format='0.00'
$footer=$plot.Titles.Add('Misja 3096 cząstek; GTX 1650; różnica średnich median w blokach A-B-B-A')
$footer.Docking=[System.Windows.Forms.DataVisualization.Charting.Docking]::Bottom
$footer.Font=New-Object Drawing.Font('Segoe UI',10)
foreach($block in $blocks){
    $value=[double]$block.scenes.mission.delta_ms
    $index=$plot.Series[0].Points.AddXY(('Blok '+$block.block),$value)
    $color=if($value -le .25){'#238b45'}else{'#cb7034'}
    $plot.Series[0].Points[$index].Color=[Drawing.ColorTranslator]::FromHtml($color)
}
$limit=New-Object System.Windows.Forms.DataVisualization.Charting.StripLine
$limit.IntervalOffset=.25;$limit.BorderColor=[Drawing.Color]::Firebrick;$limit.BorderWidth=2
$limit.BorderDashStyle=[System.Windows.Forms.DataVisualization.Charting.ChartDashStyle]::Dash
$limit.Text='Cel +0,25 ms/tick';$limit.Font=New-Object Drawing.Font('Segoe UI',12)
$plot.ChartAreas[0].AxisY.StripLines.Add($limit)
$plot.SaveImage((Join-Path $Results 'gpu-budget.png'),[System.Windows.Forms.DataVisualization.Charting.ChartImageFormat]::Png);$plot.Dispose()
$quality=Get-Content -LiteralPath (Join-Path $Results 'quality-summary.json') -Raw | ConvertFrom-Json
$plot=New-Plot 'I07: mniej bliskich par w zbiorniku wody' 'Udział cząstek z sąsiadem bliżej niż 0,5 komórki [%]' 0.7
$plot.ChartAreas[0].AxisY.Interval=.1;$plot.ChartAreas[0].AxisY.LabelStyle.Format='0.0'
$footer=$plot.Titles.Add('Scena water_tank; średnia checkpointów od 600 do 1200 ticków (120 Hz)')
$footer.Docking=[System.Windows.Forms.DataVisualization.Charting.Docking]::Bottom
$footer.Font=New-Object Drawing.Font('Segoe UI',10)
foreach($name in @('legacy-a','legacy-b','balanced')){
    $value=100*[double]$quality.quality.$name.'4'.cluster_fraction_mean
    $index=$plot.Series[0].Points.AddXY($name,$value)
    $color=if($name -eq 'balanced'){'#426fd4'}else{'#8c9cad'}
    $plot.Series[0].Points[$index].Color=[Drawing.ColorTranslator]::FromHtml($color)
}
$plot.SaveImage((Join-Path $Results 'clustering.png'),[System.Windows.Forms.DataVisualization.Charting.ChartImageFormat]::Png);$plot.Dispose()
Write-Host 'Two standalone plots created from measured CSV summaries.'
