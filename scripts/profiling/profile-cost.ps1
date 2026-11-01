param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet('off','passes','bounds','snapshots')][string[]]$CostModes=@('off','passes','bounds','snapshots'),
    [ValidateRange(1,8)][int]$Repetitions=4,
    [ValidateRange(10,3600)][int]$TimeoutSeconds=300,
    [string]$NvidiaSmi,
    [string[]]$ExtraArguments=@()
)
$ErrorActionPreference='Stop'
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$Output=[IO.Path]::GetFullPath($Output)
$CostModes=@($CostModes | ForEach-Object {$_.ToLowerInvariant()})
if($CostModes.Count -eq 0 -or @($CostModes | Select-Object -Unique).Count -ne $CostModes.Count){throw 'CostModes must be nonempty and unique.'}
if(Test-Path -LiteralPath (Join-Path $Output 'cost-schedule.csv')){throw 'Choose a fresh output directory.'}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$taskStarted=[DateTimeOffset]::UtcNow
$taskSchedule=[Collections.Generic.List[object]]::new()
$taskRuns=[Collections.Generic.List[object]]::new()
$taskErrors=[Collections.Generic.List[object]]::new()
for($taskRepeat=1;$taskRepeat -le $Repetitions;++$taskRepeat){
    $taskOrder=@(for($taskIndex=0;$taskIndex -lt $CostModes.Count;++$taskIndex){$CostModes[($taskIndex+$taskRepeat-1)%$CostModes.Count]})
    if($CostModes.Count -eq 2 -and $taskRepeat%2 -eq 0){$taskOrder=@($CostModes[1],$CostModes[0])}
    foreach($taskMode in $taskOrder){$taskSchedule.Add([pscustomobject]@{Order=$taskSchedule.Count+1;Name="cost-$taskMode-$taskRepeat";Mode=$taskMode;Repeat=$taskRepeat})}
}
$taskSchedule | Export-Csv -LiteralPath (Join-Path $Output 'cost-schedule.csv') -NoTypeInformation -Encoding utf8
[ordered]@{schema_version=1;started_utc=$taskStarted.ToString('o');executable=$Executable;modes=$CostModes;repetitions=$Repetitions
    order_policy='Rotating Latin order; N repetitions balance N modes. Two-mode schedule alternates AB/BA.'
    correctness_precondition='Run --gpu-cost-check before interpreting diagnostic measurements.'
    diagnostic_scope='off is the performance reference; passes, bounds and snapshots have separately measured overhead.'
    extra_arguments=$ExtraArguments} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Output 'cost-environment.json') -Encoding utf8
foreach($taskRun in $taskSchedule){
    $taskGroup=Join-Path $Output $taskRun.Name
    $taskParameters=@{Executable=$Executable;Output=$taskGroup;ThroughputScenario='cost';CostMode=$taskRun.Mode
        Modes=@('elapsed');Repetitions=1;SkipWindows=$true;VariantLabel=$taskRun.Mode;TimeoutSeconds=$TimeoutSeconds;ExtraArguments=$ExtraArguments}
    if($NvidiaSmi){$taskParameters.NvidiaSmi=$NvidiaSmi}
    try{& (Join-Path $PSScriptRoot 'profile-stabilization.ps1') @taskParameters}
    catch{$taskErrors.Add([pscustomobject]@{Name=$taskRun.Name;Error=$_.Exception.Message})}
    $taskChildRuns=Join-Path $taskGroup 'runs.csv'
    if(Test-Path -LiteralPath $taskChildRuns){
        foreach($taskChild in Import-Csv -LiteralPath $taskChildRuns){
            $taskRuns.Add([pscustomobject]@{Name=$taskRun.Name;Order=$taskRun.Order;Phase='throughput';Mode='elapsed';Variant=$taskRun.Mode;CostMode=$taskRun.Mode
                Repeat=$taskRun.Repeat;PhaseGating='off';ZeroProjection='off';ProfileDirectory=(Join-Path $taskRun.Name $taskChild.Name).Replace('\','/')
                StartUtc=$taskChild.StartUtc;EndUtc=$taskChild.EndUtc;Pid=$taskChild.Pid;ExitCode=$taskChild.ExitCode;TimedOut=$taskChild.TimedOut
                LaunchError=$taskChild.LaunchError;TelemetryAvailable=$taskChild.TelemetryAvailable;ArgumentsJson=$taskChild.ArgumentsJson})
        }
    }else{
        $taskRuns.Add([pscustomobject]@{Name=$taskRun.Name;Order=$taskRun.Order;Phase='throughput';Mode='elapsed';Variant=$taskRun.Mode;CostMode=$taskRun.Mode
            Repeat=$taskRun.Repeat;PhaseGating='off';ZeroProjection='off';ProfileDirectory=$taskRun.Name;StartUtc=$null;EndUtc=$null;Pid=$null;ExitCode=$null
            TimedOut=$false;LaunchError='Child harness did not produce runs.csv';TelemetryAvailable=$false;ArgumentsJson=$null})
    }
    $taskRuns | Export-Csv -LiteralPath (Join-Path $Output 'runs.csv') -NoTypeInformation -Encoding utf8
}
if($taskErrors.Count){$taskErrors | Export-Csv -LiteralPath (Join-Path $Output 'cost-errors.csv') -NoTypeInformation -Encoding utf8}
[ordered]@{started_utc=$taskStarted.ToString('o');ended_utc=[DateTimeOffset]::UtcNow.ToString('o');runs=$taskRuns.Count;errors=$taskErrors.Count} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Output 'cost-completed.json') -Encoding utf8
if($taskErrors.Count -or @($taskRuns | Where-Object {$null -eq $_.ExitCode -or $_.ExitCode -ne '0' -or $_.TimedOut -eq 'True' -or $_.LaunchError}).Count){throw 'Some cost runs failed; all partial records are preserved.'}
Write-Host "Analyze with: python scripts/analyze-stabilization.py --input `"$Output`""
