param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet('direct','staged','immutable','auto')][string[]]$StorageModes=@('direct','staged','immutable'),
    [ValidateSet(0,1,2,4)][int[]]$ReadbackPeriods=@(2,0),
    [ValidateRange(1,6)][int]$Repetitions=6,
    [switch]$SkipThroughput,[switch]$SkipWindows,
    [ValidateSet('visible','hidden')][string]$WindowVisibility='visible',
    [ValidateSet('on','off')][string]$VSync='off',
    [ValidateRange(30,240)][int]$FrameLimit=60,
    [ValidateRange(10,3600)][int]$TimeoutSeconds=180,
    [switch]$DebugLog
)
# Reuse the native process supervisor, telemetry, hashes and quoting from A/F.
$ErrorActionPreference='Stop'
$StorageModes=@($StorageModes | ForEach-Object {$_.ToLowerInvariant()})
if($SkipThroughput -and $SkipWindows){throw 'Select at least one phase.'}
if($StorageModes.Count -eq 0 -or $ReadbackPeriods.Count -eq 0){throw 'Select at least one storage mode and readback period.'}
if(@($StorageModes | Select-Object -Unique).Count -ne $StorageModes.Count -or @($ReadbackPeriods | Select-Object -Unique).Count -ne $ReadbackPeriods.Count){throw 'Conditions must be unique.'}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$Output=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath (Join-Path $Output 'transport-schedule.csv')){throw 'Choose a fresh output directory.'}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$taskConditions=@(foreach($taskPeriod in $ReadbackPeriods){foreach($taskStorage in $StorageModes){
    [pscustomobject]@{Label="$taskStorage-r$taskPeriod";Storage=$taskStorage;Period=$taskPeriod}
}})
$taskSchedule=[Collections.Generic.List[object]]::new()
$taskRuns=[Collections.Generic.List[object]]::new()
$taskErrors=[Collections.Generic.List[object]]::new()
foreach($taskPhase in @('throughput','window')){
    if(($taskPhase -eq 'throughput' -and $SkipThroughput) -or ($taskPhase -eq 'window' -and $SkipWindows)){continue}
    for($taskRepeat=1;$taskRepeat -le $Repetitions;++$taskRepeat){
        $taskOrder=@($taskConditions)
        if($taskConditions.Count -eq 2 -and $taskRepeat%2 -eq 0){[array]::Reverse($taskOrder)}
        elseif($taskConditions.Count -gt 2){$taskOrder=@(for($taskIndex=0;$taskIndex -lt $taskConditions.Count;++$taskIndex){$taskConditions[($taskIndex+$taskRepeat-1)%$taskConditions.Count]})}
        foreach($taskCondition in $taskOrder){
            $taskSchedule.Add([pscustomobject]@{Order=$taskSchedule.Count+1;Name="$taskPhase-$($taskCondition.Label)-$taskRepeat";Phase=$taskPhase;Variant=$taskCondition.Label;Storage=$taskCondition.Storage;ReadbackPeriod=$taskCondition.Period;Repeat=$taskRepeat})
        }
    }
}
$taskSchedule | Export-Csv -LiteralPath (Join-Path $Output 'transport-schedule.csv') -NoTypeInformation -Encoding utf8
[ordered]@{schema_version=1;executable=$Executable;storage_modes=$StorageModes;readback_periods=$ReadbackPeriods;repetitions=$Repetitions;timer_mode='elapsed';counters='on';phase_gating='off';zero_projection='off';order_policy='Two conditions alternate ABBA. More conditions use cyclic Latin order; a complete block needs as many repetitions as conditions. Inspect schedule for partial blocks.';correctness_precondition='Run --gpu-transport-check separately.';window_visibility=$WindowVisibility} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'transport-environment.json') -Encoding utf8
foreach($taskRun in $taskSchedule){
    $taskParameters=@{Executable=$Executable;Output=(Join-Path $Output $taskRun.Name);Modes=@('elapsed');Repetitions=1;VariantLabel=$taskRun.Variant;PhaseGating='off';ZeroProjection='off';Counters='on';TimeoutSeconds=$TimeoutSeconds;WindowVisibility=$WindowVisibility;VSync=$VSync;FrameLimit=$FrameLimit;ExtraArguments=@('--gpu-storage',$taskRun.Storage,'--gpu-readback-period',[string]$taskRun.ReadbackPeriod)}
    if($taskRun.Phase -eq 'window'){$taskParameters.SkipThroughput=$true}else{$taskParameters.SkipWindows=$true}
    if($DebugLog){$taskParameters.DebugLog=$true}
    try { & (Join-Path $PSScriptRoot 'profile-stabilization.ps1') @taskParameters }
    catch {$taskErrors.Add([pscustomobject]@{Name=$taskRun.Name;Error=$_.Exception.Message});$taskErrors | Export-Csv -LiteralPath (Join-Path $Output 'transport-errors.csv') -NoTypeInformation -Encoding utf8}
    $taskChildRuns=Join-Path (Join-Path $Output $taskRun.Name) 'runs.csv'
    if(Test-Path -LiteralPath $taskChildRuns){
        foreach($taskChild in Import-Csv -LiteralPath $taskChildRuns){
            $taskRelative=Join-Path $taskRun.Name $taskChild.Name
            $taskRuns.Add([pscustomobject]@{Name=$taskRun.Name;Order=$taskRun.Order;Phase=$taskRun.Phase;Mode=$taskChild.Mode;Variant=$taskRun.Variant;Storage=$taskRun.Storage;ReadbackPeriod=$taskRun.ReadbackPeriod;Repeat=$taskRun.Repeat;PhaseGating='off';ZeroProjection='off';ProfileDirectory=$taskRelative.Replace('\','/');StartUtc=$taskChild.StartUtc;EndUtc=$taskChild.EndUtc;ExitCode=$taskChild.ExitCode;TimedOut=$taskChild.TimedOut;LaunchError=$taskChild.LaunchError;ArgumentsJson=$taskChild.ArgumentsJson})
        }
    }else{
        $taskRuns.Add([pscustomobject]@{Name=$taskRun.Name;Order=$taskRun.Order;Phase=$taskRun.Phase;Mode='elapsed';Variant=$taskRun.Variant;Storage=$taskRun.Storage;ReadbackPeriod=$taskRun.ReadbackPeriod;Repeat=$taskRun.Repeat;PhaseGating='off';ZeroProjection='off';ProfileDirectory=$taskRun.Name;StartUtc=$null;EndUtc=$null;ExitCode=$null;TimedOut=$false;LaunchError='No child runs.csv';ArgumentsJson=$null})
    }
    $taskRuns | Export-Csv -LiteralPath (Join-Path $Output 'runs.csv') -NoTypeInformation -Encoding utf8
}
[ordered]@{ended_utc=[DateTimeOffset]::UtcNow.ToString('o');runs=$taskRuns.Count;errors=$taskErrors.Count} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Output 'transport-completed.json') -Encoding utf8
if($taskErrors.Count -gt 0 -or @($taskRuns | Where-Object {$null -eq $_.ExitCode -or $_.ExitCode -ne '0' -or $_.TimedOut -eq 'True' -or $_.LaunchError}).Count -gt 0){throw 'Some transport runs failed; records are preserved.'}
