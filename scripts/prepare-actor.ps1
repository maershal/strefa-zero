param([string]$SourcePath, [string]$OutputPath)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path -Parent $PSScriptRoot
if (!$SourcePath) { $SourcePath = Join-Path $taskProject 'assets/actors/soldier_pl.png' }
if (!$OutputPath) { $OutputPath = Join-Path $taskProject 'assets/actors/soldier_pl.bmp' }
Add-Type -AssemblyName System.Drawing
$taskBitmap = [System.Drawing.Bitmap]::new([System.IO.Path]::GetFullPath($SourcePath))
try {
    $taskStream = [System.IO.File]::Create([System.IO.Path]::GetFullPath($OutputPath))
    $taskWriter = [System.IO.BinaryWriter]::new($taskStream)
    try {
        $taskBytes = [uint32]($taskBitmap.Width * $taskBitmap.Height * 4)
        # BITMAPV4HEADER with explicit alpha mask, BGRA bytes and top-down rows.
        $taskWriter.Write([uint16]19778); $taskWriter.Write([uint32](122 + $taskBytes))
        $taskWriter.Write([uint32]0); $taskWriter.Write([uint32]122)
        $taskWriter.Write([uint32]108); $taskWriter.Write([int]$taskBitmap.Width); $taskWriter.Write([int](-$taskBitmap.Height))
        $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]32); $taskWriter.Write([uint32]3)
        $taskWriter.Write($taskBytes); $taskWriter.Write([int]2835); $taskWriter.Write([int]2835)
        $taskWriter.Write([uint32]0); $taskWriter.Write([uint32]0)
        foreach($taskMask in @([uint32]16711680,[uint32]65280,[uint32]255,[uint32]4278190080)) { $taskWriter.Write($taskMask) }
        $taskWriter.Write([uint32]1934772034)
        for($taskIndex=0;$taskIndex -lt 12;$taskIndex++) { $taskWriter.Write([uint32]0) }
        if($taskStream.Position -ne 122) { throw 'Invalid BMP header size' }
        for($taskY=0;$taskY -lt $taskBitmap.Height;$taskY++) {
            for($taskX=0;$taskX -lt $taskBitmap.Width;$taskX++) {
                $taskPixel=$taskBitmap.GetPixel($taskX,$taskY)
                $taskWriter.Write([byte]$taskPixel.B); $taskWriter.Write([byte]$taskPixel.G)
                $taskWriter.Write([byte]$taskPixel.R); $taskWriter.Write([byte]$taskPixel.A)
            }
        }
    } finally { $taskWriter.Dispose() }
} finally { $taskBitmap.Dispose() }
