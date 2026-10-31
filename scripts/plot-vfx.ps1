param([Parameter(Mandatory=$true)][string]$Folder,[string]$Output='')
$ErrorActionPreference='Stop'
$Folder=(Resolve-Path -LiteralPath $Folder).Path
if(-not $Output){$Output=$Folder}
[IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($Output))|Out-Null
Add-Type -AssemblyName System.Drawing
foreach($scene in @('mission','dam_break','sand_water','saturated')){
 $bitmap=New-Object System.Drawing.Bitmap(2880,600)
 $g=[System.Drawing.Graphics]::FromImage($bitmap)
 $font=New-Object System.Drawing.Font('Segoe UI',12)
 try{
  $g.Clear([System.Drawing.Color]::FromArgb(15,20,25))
  $column=0
  foreach($quality in @('off','low','high')){
   $im=[System.Drawing.Image]::FromFile((Join-Path $Folder "$scene-3-$quality.bmp"))
   try{
    if($im.Width -ne 960 -or $im.Height -ne 576){throw 'Expected 960 x 576 captures.'}
    $g.DrawImageUnscaled($im,$column*960,24)
    $g.DrawString("$scene / $quality",$font,[System.Drawing.Brushes]::White,$column*960+8,2)
   }finally{$im.Dispose()}
   $column++
  }
  $bitmap.Save((Join-Path $Output "$scene-comparison.png"),[System.Drawing.Imaging.ImageFormat]::Png)
 }finally{$font.Dispose();$g.Dispose();$bitmap.Dispose()}
}
