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
function Invoke-ReadbackRun([string]$Name,[string[]]$Flags) {
    $taskOutput=Join-Path $Output $Name
    $taskArguments=@($Flags)+@('--profile',('"'+$taskOutput+'"'))
    if($ShowWindows){$taskArguments+=@('--show-test-window')}
    Write-Host "Measuring $Name"
    $taskProcess=Start-Process -FilePath $Executable -ArgumentList $taskArguments -PassThru
    if(-not $taskProcess.WaitForExit(120000)){$taskProcess.Kill();throw "Timed out: $Name (120 s); incomplete data retained"}
    $taskProcess.Refresh()
    if($taskProcess.ExitCode -ne 0){throw "Measurement $Name failed: $($taskProcess.ExitCode); see $taskOutput"}
}
Invoke-ReadbackRun 'transport-tests' @('--gpu-readback-check')
if(-not $SkipThroughput){Invoke-ReadbackRun 'throughput' @('--gpu-readback-profile')}
if($IncludeWindows){
    $taskVariants=@(
        @{Name='legacy';Mode='legacy';Period='1'},
        @{Name='auto';Mode='auto';Period='2'},
        @{Name='staging';Mode='staging';Period='2'}
    )
    for($taskRepeat=1;$taskRepeat -le $WindowRepetitions;++$taskRepeat){
        $taskOrder=@($taskVariants)
        if($taskRepeat%2 -eq 0){[array]::Reverse($taskOrder)}
        foreach($taskVariant in $taskOrder){
            Invoke-ReadbackRun ("window-{0}-{1}" -f $taskVariant.Name,$taskRepeat) @('--mission-smoke-test','--gpu-counters','--gpu-readback-mode',$taskVariant.Mode,'--gpu-readback-period',$taskVariant.Period)
        }
    }
}
