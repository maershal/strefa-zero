param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Output,
    [switch]$IncludeWindows,
    [switch]$ShowWindows,
    [switch]$SkipThroughput,
    [ValidateRange(1,5)][int]$WindowRepetitions = 3
)
$ErrorActionPreference='Stop'
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$Output=[System.IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$taskResults=[System.Collections.Generic.List[object]]::new()
function Invoke-NeighborRun([string]$Name,[string[]]$Flags) {
    $taskOutput=Join-Path $Output $Name
    $taskArguments=@($Flags)+@('--profile',('"'+$taskOutput+'"'))
    if($ShowWindows){$taskArguments+=@('--show-test-window')}
    Write-Host "Measuring $Name"
    $taskProcess=Start-Process -FilePath $Executable -ArgumentList $taskArguments -PassThru
    $taskTimeout=-not $taskProcess.WaitForExit(1200000)
    if($taskTimeout){$taskProcess.Kill();$taskProcess.WaitForExit()}
    $taskProcess.Refresh()
    $taskResults.Add([pscustomobject]@{Name=$Name;ExitCode=$taskProcess.ExitCode;TimedOut=$taskTimeout})
    $taskResults | Export-Csv -LiteralPath (Join-Path $Output 'runs.csv') -NoTypeInformation
}
Invoke-NeighborRun 'neighbor-tests' @('--gpu-neighbor-check')
if($taskResults[-1].ExitCode -ne 0){throw 'Neighbor tests failed; see neighbor-tests'}
if(-not $SkipThroughput){Invoke-NeighborRun 'throughput' @('--gpu-neighbor-profile')}
if($IncludeWindows){
    for($taskRepeat=1;$taskRepeat -le $WindowRepetitions;++$taskRepeat){
        $taskOrder=@('linked','reused')
        if($taskRepeat%2 -eq 0){[array]::Reverse($taskOrder)}
        foreach($taskVariant in $taskOrder){
            $taskFlags=@('--mission-smoke-test','--gpu-counters','--gpu-neighbors',$taskVariant)
            $taskFlags+=@('--gpu-layout',$(if($taskVariant -eq 'linked'){'aos'}else{'soa'}))
            Invoke-NeighborRun ("window-{0}-{1}" -f $taskVariant,$taskRepeat) $taskFlags
        }
    }
}
if(@($taskResults | Where-Object {$_.ExitCode -ne 0 -or $_.TimedOut}).Count -gt 0){throw 'Some measurements failed; all completed results and exit codes retained in runs.csv'}
