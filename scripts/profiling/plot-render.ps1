param([string]$InputFolder,[string]$OutputFile)
Add-Type -AssemblyName System.Drawing
$names=@('phases','sand','immersion','mission')
$versions=@('reference','A','basic','standard','C-high')
$canvas=New-Object Drawing.Bitmap (400*5),(268*4)
$graphics=[Drawing.Graphics]::FromImage($canvas)
$graphics.Clear([Drawing.Color]::FromArgb(18,24,28))
$font=New-Object Drawing.Font 'Segoe UI',11
for($y=0;$y -lt $names.Count;$y++){
 for($x=0;$x -lt $versions.Count;$x++){
  $image=[Drawing.Image]::FromFile((Join-Path $InputFolder ($names[$y]+'-'+$versions[$x]+'.bmp')))
  $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
  $graphics.DrawImage($image,($x*400),($y*268+23),400,240)
  $graphics.DrawString(($names[$y]+' / '+$versions[$x]),$font,[Drawing.Brushes]::White,($x*400+6),($y*268+2))
  $image.Dispose()
 }
}
$canvas.Save($OutputFile,[Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose();$font.Dispose();$canvas.Dispose()
