param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$Folder=(Resolve-Path -LiteralPath $Folder).Path
$font=New-Object Drawing.Font('Segoe UI',16)
try {
 foreach($scene in @('cold','fire','oil','quench')) {
  $canvas=New-Object Drawing.Bitmap(1920,1240)
  $graphics=[Drawing.Graphics]::FromImage($canvas)
  try {
   $graphics.Clear([Drawing.Color]::FromArgb(20,24,32))
   $i=0
   foreach($stage in @('off','heat','heat_gas','full')) {
    $picture=[Drawing.Image]::FromFile((Join-Path $Folder "$scene-$stage.bmp"))
    try {
     $x=($i%2)*960;$y=[math]::Floor($i/2)*620
     $graphics.DrawString("$scene / $stage",$font,[Drawing.Brushes]::White,[single]($x+12),[single]($y+8))
     $graphics.DrawImage($picture,[int]$x,[int]($y+44),960,576)
    } finally {$picture.Dispose()}
    $i++
   }
   $canvas.Save((Join-Path $Folder "$scene-comparison.png"),[Drawing.Imaging.ImageFormat]::Png)
  } finally {$graphics.Dispose();$canvas.Dispose()}
 }
} finally {$font.Dispose()}
