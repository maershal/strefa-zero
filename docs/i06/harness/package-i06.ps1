param([string]$TaskRoot)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$source=Join-Path $TaskRoot 'outputs\strefa-zero'
function Get-PackageFiles([string]$Root){
    @(Get-ChildItem -LiteralPath $Root -Force | ForEach-Object {
        if($_.PSIsContainer){if($_.Name -ne '.git'){Get-ChildItem -LiteralPath $_.FullName -Recurse -File -Force}}
        else{$_}
    } | Sort-Object FullName)
}
$sourceFiles=Get-PackageFiles $source
$hashRows=@($sourceFiles | Where-Object {$_.FullName -ne (Join-Path $source 'SHA256SUMS.txt')} | ForEach-Object {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()+'  '+$_.FullName.Substring($source.Length+1).Replace('\','/')
})
[IO.File]::WriteAllText((Join-Path $source 'SHA256SUMS.txt'),[string]::Join("`n",$hashRows)+"`n",[Text.UTF8Encoding]::new($false))
Write-Host 'Source manifest complete'
$zipRows=@()
foreach($item in @(@('StrefaZero-Windows-x64-I06','StrefaZero-Windows-x64-I06'),@('StrefaZero-source-I06','strefa-zero'))){
    $folder=Join-Path $TaskRoot ('outputs\'+$item[1])
    $zipPath=Join-Path $TaskRoot ('outputs\'+$item[0]+'.zip')
    $stream=[IO.File]::Open($zipPath,[IO.FileMode]::Create)
    $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
    try {
        foreach($file in (Get-PackageFiles $folder)){
            $entry=$item[1]+'/'+$file.FullName.Substring($folder.Length+1).Replace('\','/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$entry,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally {$archive.Dispose();$stream.Dispose()}
    $zipRows+=(Get-FileHash -Algorithm SHA256 -LiteralPath $zipPath).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($zipPath)
    Write-Host "Archived $zipPath"
}
[IO.File]::WriteAllText((Join-Path $TaskRoot 'outputs\StrefaZero-I06-SHA256SUMS.txt'),[string]::Join("`n",$zipRows)+"`n",[Text.UTF8Encoding]::new($false))
