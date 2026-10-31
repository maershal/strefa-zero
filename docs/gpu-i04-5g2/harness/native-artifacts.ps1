param([Parameter(Mandatory=$true)][string]$SpecFile)
$ErrorActionPreference='Stop'
$taskSpec=[IO.File]::ReadAllText($SpecFile) | ConvertFrom-Json
if($taskSpec.action -eq 'copy'){
    $taskCopyInfo=[Diagnostics.ProcessStartInfo]::new()
    $taskCopyInfo.FileName=Join-Path ([Environment]::GetFolderPath('Windows')) 'System32\robocopy.exe'
    $taskCopyInfo.Arguments='"'+$taskSpec.source+'" "'+$taskSpec.destination+'" /E /COPY:DAT /DCOPY:T /R:1 /W:1 /XF *.bmp *.pyc /XD __pycache__ /NFL /NDL /NJH /NJS /NP'
    $taskCopyInfo.UseShellExecute=$false;$taskCopyInfo.CreateNoWindow=$true
    $taskCopyInfo.RedirectStandardOutput=$true;$taskCopyInfo.RedirectStandardError=$true
    $taskCopyProcess=[Diagnostics.Process]::Start($taskCopyInfo)
    $taskCopyOut=$taskCopyProcess.StandardOutput.ReadToEndAsync();$taskCopyError=$taskCopyProcess.StandardError.ReadToEndAsync()
    $taskCopyProcess.WaitForExit();$taskCopyCode=$taskCopyProcess.ExitCode
    if($taskCopyCode -ge 8){throw "Robocopy failed: $taskCopyCode $($taskCopyError.Result)"}
    Write-Output 'Artifact tree copied'
    exit 0
}
$taskHasher=[Security.Cryptography.SHA256]::Create()
function Get-ArtifactHash([string]$Path){
    $taskStream=[IO.File]::OpenRead($Path)
    try{return [BitConverter]::ToString($taskHasher.ComputeHash($taskStream)).Replace('-','').ToLowerInvariant()}
    finally{$taskStream.Dispose()}
}
try{
    if($taskSpec.action -eq 'manifest'){
        $taskText=[Text.StringBuilder]::new()
        $taskPaths=@($taskSpec.paths)
        if(-not $taskSpec.paths){
            $taskPrefix=[IO.Path]::GetFullPath($taskSpec.root).TrimEnd([char]92)
            $taskOutput=[IO.Path]::GetFullPath($taskSpec.output)
            $taskPaths=@([IO.Directory]::GetFiles($taskPrefix,'*',[IO.SearchOption]::AllDirectories) | Where-Object {$_ -ne $taskOutput} |
                ForEach-Object {$_.Substring($taskPrefix.Length+1).Replace('\','/')} | Sort-Object)
        }
        foreach($taskRelative in $taskPaths){
            $taskFile=Join-Path $taskSpec.root $taskRelative
            [void]$taskText.Append((Get-ArtifactHash $taskFile)+'  '+$taskRelative+"`n")
        }
        [IO.File]::WriteAllText($taskSpec.output,$taskText.ToString(),[Text.UTF8Encoding]::new($false))
        Write-Output "Manifest generated: $($taskPaths.Count) files"
    }elseif($taskSpec.action -eq 'verify'){
        $taskCount=0
        foreach($taskLine in [IO.File]::ReadAllLines($taskSpec.manifest)){
            if(-not $taskLine){continue}
            $taskParts=$taskLine -split '  ',2
            if($taskParts.Count -ne 2 -or (Get-ArtifactHash (Join-Path $taskSpec.root $taskParts[1])) -ne $taskParts[0]){throw "Artifact checksum mismatch: $taskLine"}
            ++$taskCount
        }
        Write-Output "Manifest verified: $taskCount files"
    }else{throw 'Unknown artifact action'}
}finally{$taskHasher.Dispose()}
