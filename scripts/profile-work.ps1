param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet('reference','phase','projection','combined')][string[]]$Variants = @('reference','phase'),
    [ValidateRange(1,6)][int]$Repetitions = 3,
    [switch]$SkipThroughput,
    [switch]$SkipWindows,
    [ValidateSet('visible','hidden')][string]$WindowVisibility = 'visible',
    [ValidateSet('on','off')][string]$VSync = 'off',
    [ValidateRange(30,240)][int]$FrameLimit = 60,
    [switch]$GroupTimers,
    [switch]$DebugLog,
    [ValidateRange(10,3600)][int]$TimeoutSeconds = 1200,
    [string]$NvidiaSmi,
    [string[]]$ExtraArguments = @()
)
# Work variants share exactly the native process/telemetry supervision from
# profile-stabilization. Correctness must be checked separately with
# --gpu-work-check before interpreting these measurements.
$ErrorActionPreference='Stop'
if($SkipThroughput -and $SkipWindows){throw 'Select at least one phase.'}
$Variants=@($Variants | ForEach-Object {$_.ToLowerInvariant()})
if($Variants.Count -eq 0 -or @($Variants | Select-Object -Unique).Count -ne $Variants.Count){throw 'Variants must be nonempty and unique.'}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$Output=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath (Join-Path $Output 'work-schedule.csv')){throw 'Output already contains measurements; choose a fresh directory.'}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$taskSchedule=[Collections.Generic.List[object]]::new()
$taskRuns=[Collections.Generic.List[object]]::new()
$taskErrors=[Collections.Generic.List[object]]::new()
$taskStarted=[DateTimeOffset]::UtcNow
foreach($taskPhase in @('throughput','window')){
    if(($taskPhase -eq 'throughput' -and $SkipThroughput) -or ($taskPhase -eq 'window' -and $SkipWindows)){continue}
    for($taskRepeat=1;$taskRepeat -le $Repetitions;++$taskRepeat){
        $taskOrder=@($Variants)
        if($Variants.Count -eq 2 -and $taskRepeat%2 -eq 0){[array]::Reverse($taskOrder)}
        elseif($Variants.Count -gt 2){
            $taskOrder=@(for($taskIndex=0;$taskIndex -lt $Variants.Count;++$taskIndex){$Variants[($taskIndex+$taskRepeat-1)%$Variants.Count]})
        }
        foreach($taskVariant in $taskOrder){
            $taskPhaseGating=if($taskVariant -in @('phase','combined')){'on'}else{'off'}
            $taskZeroProjection=if($taskVariant -in @('projection','combined')){'on'}else{'off'}
            $taskSchedule.Add([pscustomobject]@{
                Order=$taskSchedule.Count+1;Name="$taskPhase-$taskVariant-$taskRepeat";Phase=$taskPhase
                Variant=$taskVariant;Repeat=$taskRepeat;PhaseGating=$taskPhaseGating;ZeroProjection=$taskZeroProjection
            })
        }
    }
}
$taskSchedule | Export-Csv -LiteralPath (Join-Path $Output 'work-schedule.csv') -NoTypeInformation -Encoding utf8
[ordered]@{
    schema_version=1;started_utc=$taskStarted.ToString('o');executable=$Executable
    timer_mode='elapsed';counters='on';variants=$Variants;repetitions=$Repetitions
    order_policy=$(if($Variants.Count -eq 2){'Forward/reverse alternation: the first two repetitions form ABBA.'}elseif($Variants.Count -gt 2){'Rotating Latin order; inspect work-schedule.csv for position balance.'}else{'Single variant.'})
    correctness_precondition='Run --gpu-work-check separately before performance acceptance.'
    note='Reference means C work optimizations disabled in the same current build; it is not an immutable prior release.'
    window_visibility=$WindowVisibility;vsync=$VSync;frame_limit=$FrameLimit;extra_arguments=$ExtraArguments
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Output 'work-environment.json') -Encoding utf8

foreach($taskRun in $taskSchedule){
    $taskGroup=Join-Path $Output $taskRun.Name
    $taskParameters=@{
        Executable=$Executable;Output=$taskGroup;Modes=@('elapsed');Repetitions=1
        PhaseGating=$taskRun.PhaseGating;ZeroProjection=$taskRun.ZeroProjection;VariantLabel=$taskRun.Variant
        WindowVisibility=$WindowVisibility;VSync=$VSync;FrameLimit=$FrameLimit;Counters='on'
        TimeoutSeconds=$TimeoutSeconds;ExtraArguments=$ExtraArguments
    }
    if($taskRun.Phase -eq 'window'){$taskParameters.SkipThroughput=$true}else{$taskParameters.SkipWindows=$true}
    if($GroupTimers){$taskParameters.GroupTimers=$true}
    if($DebugLog){$taskParameters.DebugLog=$true}
    if($NvidiaSmi){$taskParameters.NvidiaSmi=$NvidiaSmi}
    try { & (Join-Path $PSScriptRoot 'profile-stabilization.ps1') @taskParameters }
    catch {
        $taskErrors.Add([pscustomobject]@{Name=$taskRun.Name;Error=$_.Exception.Message})
        $taskErrors | Export-Csv -LiteralPath (Join-Path $Output 'work-errors.csv') -NoTypeInformation -Encoding utf8
    }
    $taskChildRuns=Join-Path $taskGroup 'runs.csv'
    if(Test-Path -LiteralPath $taskChildRuns){
        foreach($taskChild in Import-Csv -LiteralPath $taskChildRuns){
            $taskProfile=Join-Path $taskRun.Name $taskChild.Name
            $taskRuns.Add([pscustomobject]@{
                Name=$taskRun.Name;Order=$taskRun.Order;Phase=$taskRun.Phase;Mode=$taskChild.Mode
                Variant=$taskRun.Variant;Repeat=$taskRun.Repeat;PhaseGating=$taskRun.PhaseGating;ZeroProjection=$taskRun.ZeroProjection
                ProfileDirectory=$taskProfile.Replace('\','/');StartUtc=$taskChild.StartUtc;EndUtc=$taskChild.EndUtc;Pid=$taskChild.Pid
                ExitCode=$taskChild.ExitCode;TimedOut=$taskChild.TimedOut;LaunchError=$taskChild.LaunchError
                TelemetryAvailable=$taskChild.TelemetryAvailable;ArgumentsJson=$taskChild.ArgumentsJson
            })
        }
    }else{
        # Keep failed setup visible instead of silently deleting a scheduled variant.
        $taskRuns.Add([pscustomobject]@{
            Name=$taskRun.Name;Order=$taskRun.Order;Phase=$taskRun.Phase;Mode='elapsed'
            Variant=$taskRun.Variant;Repeat=$taskRun.Repeat;PhaseGating=$taskRun.PhaseGating;ZeroProjection=$taskRun.ZeroProjection
            ProfileDirectory=$taskRun.Name;StartUtc=$null;EndUtc=$null;Pid=$null
            ExitCode=$null;TimedOut=$false;LaunchError='Child harness did not produce runs.csv'
            TelemetryAvailable=$false;ArgumentsJson=$null
        })
    }
    $taskRuns | Export-Csv -LiteralPath (Join-Path $Output 'runs.csv') -NoTypeInformation -Encoding utf8
}
[ordered]@{started_utc=$taskStarted.ToString('o');ended_utc=[DateTimeOffset]::UtcNow.ToString('o');runs=$taskRuns.Count;errors=$taskErrors.Count} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Output 'work-completed.json') -Encoding utf8
Write-Host "Analyze work variants with: python scripts/analyze-stabilization.py --input `"$Output`""
if($taskErrors.Count -gt 0 -or @($taskRuns | Where-Object {$null -eq $_.ExitCode -or $_.ExitCode -ne '0' -or $_.TimedOut -eq 'True' -or $_.LaunchError}).Count -gt 0){
    throw 'Some work measurements failed; all completed/partial records are preserved.'
}
