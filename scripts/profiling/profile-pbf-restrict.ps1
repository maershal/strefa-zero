param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$BaselineExecutable,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet(6,12)][int]$Repetitions=6,
    [ValidateRange(10,3600)][int]$TimeoutSeconds=300
)
$ErrorActionPreference='Stop'
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$BaselineExecutable=(Resolve-Path -LiteralPath $BaselineExecutable).Path
$Output=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $Output){throw 'Choose a fresh output directory.'}
New-Item -ItemType Directory -Path $Output | Out-Null
$taskVariants=@('g1','g2off','g2on')
$taskOrders=@('0,1,2','1,2,0','2,0,1','2,1,0','1,0,2','0,2,1')
$taskSchedule=[Collections.Generic.List[object]]::new()
$taskRuns=[Collections.Generic.List[object]]::new()
$taskErrors=[Collections.Generic.List[object]]::new()
for($taskRepeat=1;$taskRepeat -le $Repetitions;++$taskRepeat){
    foreach($taskIndex in $taskOrders[($taskRepeat-1)%6].Split(',')){
        $taskVariant=$taskVariants[[int]$taskIndex]
        $taskSchedule.Add([pscustomobject]@{Order=$taskSchedule.Count+1;Name="$taskVariant-$taskRepeat";Variant=$taskVariant;Repeat=$taskRepeat})
    }
}
$taskSchedule | Export-Csv -LiteralPath (Join-Path $Output 'schedule.csv') -NoTypeInformation -Encoding utf8
[ordered]@{schema_version=1;executable=$Executable;baseline_executable=$BaselineExecutable;repetitions=$Repetitions
    order_policy='All six permutations; balanced positions and directed neighbors within each three-run block.'
    settings='general/PBF/reduction 256; cost probes OFF; full/direct; C OFF'
    correctness_precondition='Run binding faults, liquid, mission, hot variants and Q before interpreting measurements.'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Output 'restrict-environment.json') -Encoding utf8
foreach($taskRun in $taskSchedule){
    $taskExe=if($taskRun.Variant -eq 'g1'){$BaselineExecutable}else{$Executable}
    [string[]]$taskExtra=@('--gpu-local-size','256','--gpu-reduction-size','256','--gpu-pbf-size','256')
    if($taskRun.Variant -ne 'g1'){$taskExtra+=@('--gpu-pbf-restrict',$(if($taskRun.Variant -eq 'g2on'){'on'}else{'off'}))}
    $taskGroup=Join-Path $Output $taskRun.Name
    try{
        & (Join-Path $PSScriptRoot 'profile-stabilization.ps1') -Executable $taskExe -Output $taskGroup -ThroughputScenario cost -CostMode off `
            -Modes @('elapsed') -Repetitions 1 -SkipWindows -VariantLabel $taskRun.Variant -TimeoutSeconds $TimeoutSeconds -ExtraArguments $taskExtra
    }catch{$taskErrors.Add([pscustomobject]@{Name=$taskRun.Name;Error=$_.Exception.Message})}
    $taskChildFile=Join-Path $taskGroup 'runs.csv'
    if(Test-Path -LiteralPath $taskChildFile){
        foreach($taskChild in Import-Csv -LiteralPath $taskChildFile){
            $taskRuns.Add([pscustomobject]@{Name=$taskRun.Name;Order=$taskRun.Order;Phase='throughput';Mode='elapsed';Variant=$taskRun.Variant;Repeat=$taskRun.Repeat
                ProfileDirectory=(Join-Path $taskRun.Name $taskChild.Name).Replace('\','/');Pid=$taskChild.Pid
                ExitCode=$taskChild.ExitCode;TimedOut=$taskChild.TimedOut;LaunchError=$taskChild.LaunchError;ArgumentsJson=$taskChild.ArgumentsJson})
        }
    }else{$taskErrors.Add([pscustomobject]@{Name=$taskRun.Name;Error='Child harness did not produce runs.csv'})}
    $taskRuns | Export-Csv -LiteralPath (Join-Path $Output 'runs.csv') -NoTypeInformation -Encoding utf8
}
if($taskErrors.Count){$taskErrors | Export-Csv -LiteralPath (Join-Path $Output 'errors.csv') -NoTypeInformation -Encoding utf8}
if($taskErrors.Count -or $taskRuns.Count -ne $taskSchedule.Count -or @($taskRuns | Where-Object {$_.ExitCode -ne '0' -or $_.TimedOut -eq 'True' -or $_.LaunchError}).Count){throw 'Some PBF restrict runs failed; partial records are preserved.'}
