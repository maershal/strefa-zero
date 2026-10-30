param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet('elapsed','timestamp','off')][string[]]$Modes = @('elapsed','timestamp','off'),
    [ValidateRange(1,6)][int]$Repetitions = 3,
    [ValidateSet('stabilization','cost')][string]$ThroughputScenario = 'stabilization',
    [ValidateSet('off','passes','bounds','snapshots')][string]$CostMode = 'off',
    [switch]$SkipThroughput,
    [switch]$SkipWindows,
    [ValidateSet('visible','hidden')][string]$WindowVisibility = 'visible',
    [ValidateSet('on','off')][string]$VSync = 'off',
    [ValidateRange(30,240)][int]$FrameLimit = 60,
    [ValidateSet('on','off')][string]$Counters = 'on',
    [ValidateSet('on','off')][string]$PhaseGating = 'off',
    [ValidateSet('on','off')][string]$ZeroProjection = 'off',
    [ValidatePattern('^([a-zA-Z][a-zA-Z0-9_-]{0,31})?$')][string]$VariantLabel = '',
    [switch]$GroupTimers,
    [switch]$DebugLog,
    [ValidateRange(10,3600)][int]$TimeoutSeconds = 1200,
    [string]$NvidiaSmi,
    [string[]]$ExtraArguments = @()
)
# Native Windows supervision is intentional: completion of a WSL launcher is not
# evidence that the GUI process exited. Compatible with Windows PowerShell 5.1.
$ErrorActionPreference = 'Stop'
$ThroughputScenario=$ThroughputScenario.ToLowerInvariant()
$CostMode=$CostMode.ToLowerInvariant()
if($ThroughputScenario -eq 'cost' -and (-not $SkipWindows -or $GroupTimers -or $Modes.Count -ne 1 -or $Modes[0] -ne 'elapsed')){throw 'Cost diagnostics require SkipWindows, elapsed only and no group timers.'}
if($ThroughputScenario -ne 'cost' -and $CostMode -ne 'off'){throw 'Select the cost throughput scenario before requesting a diagnostic mode.'}
if([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT){
    throw 'Run this harness in native Windows PowerShell 5.1 or PowerShell 7.'
}
if($SkipThroughput -and $SkipWindows){throw 'Select at least one measurement phase.'}
$Modes=@($Modes | ForEach-Object {$_.ToLowerInvariant()})
$VSync=$VSync.ToLowerInvariant()
$WindowVisibility=$WindowVisibility.ToLowerInvariant()
$Counters=$Counters.ToLowerInvariant()
$PhaseGating=$PhaseGating.ToLowerInvariant()
$ZeroProjection=$ZeroProjection.ToLowerInvariant()
$VariantLabel=$VariantLabel.ToLowerInvariant()
if($Modes.Count -eq 0 -or @($Modes | Select-Object -Unique).Count -ne $Modes.Count){throw 'Modes must be nonempty and unique.'}
if($GroupTimers -and $Counters -eq 'off'){throw 'GroupTimers enables collection; it cannot be combined with Counters off.'}
# Fail closed: no additional action/diagnostics/timer/counter/visibility switch
# may silently override the labels recorded by this harness.
$taskExtraValues=@{
    '--gpu-neighbor-reads'=@('full','narrow','sorted')
    '--gpu-storage'=@('direct','staged','immutable','auto')
    '--gpu-readback-period'=@('0','1','2','4')
    '--gpu-readback-mode'=@('auto','legacy','stream','staging','persistent')
    '--gpu-coupling'=@('reference','local','combined')
    '--gpu-neighbors'=@('linked','sorted','reused')
    '--gpu-layout'=@('aos','soa')
    '--gpu-local-size'=@('64','128','256')
    '--gpu-reduction-size'=@('64','128','256')
}
$taskSeenExtras=@{}
for($taskExtraIndex=0;$taskExtraIndex -lt $ExtraArguments.Count;++$taskExtraIndex){
    $taskArgument=$ExtraArguments[$taskExtraIndex]
    if($taskSeenExtras.ContainsKey($taskArgument)){throw "Duplicate ExtraArguments option: $taskArgument"}
    $taskSeenExtras[$taskArgument]=$true
    if($taskArgument -ceq '--gpu-no-coupling'){continue}
    if($taskExtraValues.Keys -cnotcontains $taskArgument){
        throw "ExtraArguments only supports independent storage/readback/coupling/neighborhood/layout/group-size options; use a harness parameter for $taskArgument."
    }
    if($taskExtraIndex+1 -ge $ExtraArguments.Count){throw "Missing value for $taskArgument."}
    $taskExtraValue=$ExtraArguments[++$taskExtraIndex]
    if($taskExtraValues[$taskArgument] -cnotcontains $taskExtraValue){throw "Unsupported value '$taskExtraValue' for $taskArgument."}
}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$Output=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath (Join-Path $Output 'schedule.csv')){throw 'Output already contains a schedule; choose a fresh directory.'}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$taskRuns=[Collections.Generic.List[object]]::new()
$taskSchedule=[Collections.Generic.List[object]]::new()
$taskStartedUtc=[DateTimeOffset]::UtcNow

function Get-HardwareInfo {
    $taskHardware=[ordered]@{
        recorded_utc=[DateTimeOffset]::UtcNow.ToString('o')
        local_utc_offset=[DateTimeOffset]::Now.Offset.ToString()
        os_version=[Environment]::OSVersion.VersionString
        process_architecture=$(if([Environment]::Is64BitProcess){'x64'}else{'x86'})
        processor_count=[Environment]::ProcessorCount
        powershell_version=$PSVersionTable.PSVersion.ToString()
        time_zone=[TimeZoneInfo]::Local.Id
        processor=@();video_controllers=@();display_settings=@();errors=@()
    }
    foreach($taskQuery in @(
        @('Win32_Processor','processor',@('Name','Manufacturer','NumberOfCores','NumberOfLogicalProcessors')),
        @('Win32_VideoController','video_controllers',@('Name','DriverVersion','DriverDate','AdapterRAM')),
        @('Win32_DesktopMonitor','display_settings',@('Name','ScreenWidth','ScreenHeight'))
    )){
        try {$taskHardware[$taskQuery[1]]=@(Get-CimInstance -ClassName $taskQuery[0] | Select-Object -Property $taskQuery[2])}
        catch {$taskHardware.errors+=($_.Exception.Message)}
    }
    return $taskHardware
}

$taskNvidiaPath=$null
if($NvidiaSmi){$taskNvidiaPath=(Resolve-Path -LiteralPath $NvidiaSmi).Path}
else {
    $taskNvidia=Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if($taskNvidia){$taskNvidiaPath=$taskNvidia.Source}
    else {
        $taskDriverStore=Join-Path $env:WINDIR 'System32\DriverStore\FileRepository'
        if(Test-Path -LiteralPath $taskDriverStore){
            $taskNvidiaCandidate=Get-ChildItem -LiteralPath $taskDriverStore -Directory -Filter 'nv*' | ForEach-Object {
                Get-ChildItem -LiteralPath $_.FullName -File -Filter 'nvidia-smi.exe' -ErrorAction SilentlyContinue
            } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
            if($taskNvidiaCandidate){$taskNvidiaPath=$taskNvidiaCandidate.FullName}
        }
    }
}
$taskEnvironment=[ordered]@{
    schema_version=1;started_utc=$taskStartedUtc.ToString('o')
    executable=$Executable;executable_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $Executable).Hash.ToLowerInvariant()
    output=$Output;hardware=(Get-HardwareInfo)
    requested_modes=$Modes;repetitions=$Repetitions;window_visibility=$WindowVisibility
    requested_vsync=$VSync;requested_frame_limit=$FrameLimit;counters=$Counters;group_timers=[bool]$GroupTimers
    debug_log=[bool]$DebugLog;extra_arguments=$ExtraArguments
    phase_gating=$PhaseGating;zero_projection=$ZeroProjection;variant_label=$VariantLabel;throughput_scenario=$ThroughputScenario;cost_mode=$CostMode
    focus_policy='Unforced: no focus stealing or OS/driver setting changes; use actual per-frame focus flags.'
    benchmark_scope='Separate unpaced compute runner, 120 warmup and 240 measured ticks per scene; validation/drains identified by the runner.'
    window_scope='Mission smoke runs for at least 900 frames and 1700 ticks. Runtime frame data and final validation/preview capture must be analyzed separately.'
    order_policy=$(if($Modes.Count -eq 2){'Forward/reverse alternating; first two repetitions form ABBA.'}elseif($Modes.Count -eq 3){'Cyclic Latin order ABC/BCA/CAB; three repetitions balance every position.'}else{'Single selected mode.'})
    nvidia_smi=$taskNvidiaPath
    telemetry_scope='Whole native process including initialization, warmup, runtime and final validation; correlate timestamps before attributing a migration or power change.'
}
$taskEnvironment | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $Output 'environment.json') -Encoding utf8
$taskShaderDirectory=Join-Path (Split-Path -Parent $Executable) 'assets\shaders'
if(Test-Path -LiteralPath $taskShaderDirectory){
    Get-ChildItem -LiteralPath $taskShaderDirectory -File -Recurse | Sort-Object FullName | ForEach-Object {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()+'  '+$_.FullName.Substring($taskShaderDirectory.Length+1).Replace('\','/')
    } | Set-Content -LiteralPath (Join-Path $Output 'shader-sha256.txt') -Encoding utf8
}

foreach($taskPhase in @('throughput','window')){
    if(($taskPhase -eq 'throughput' -and $SkipThroughput) -or ($taskPhase -eq 'window' -and $SkipWindows)){continue}
    for($taskRepeat=1;$taskRepeat -le $Repetitions;++$taskRepeat){
        $taskOrder=@($Modes)
        if($Modes.Count -eq 2 -and $taskRepeat%2 -eq 0){[array]::Reverse($taskOrder)}
        elseif($Modes.Count -eq 3){
            $taskOrder=@(for($taskIndex=0;$taskIndex -lt $Modes.Count;++$taskIndex){$Modes[($taskIndex+$taskRepeat-1)%$Modes.Count]})
        }
        foreach($taskMode in $taskOrder){
            $taskName=if($VariantLabel){"$taskPhase-$VariantLabel-$taskMode-$taskRepeat"}else{"$taskPhase-$taskMode-$taskRepeat"}
            $taskSchedule.Add([pscustomobject]@{Order=$taskSchedule.Count+1;Name=$taskName;Phase=$taskPhase;Mode=$taskMode;Repeat=$taskRepeat;Variant=$VariantLabel})
        }
    }
}
$taskSchedule | Export-Csv -LiteralPath (Join-Path $Output 'schedule.csv') -NoTypeInformation -Encoding utf8

function Join-NativeArguments([string[]]$Arguments) {
    # Windows CRT parsing: quote each argument, double backslashes before quotes
    # and before the closing quote. ProcessStartInfo never invokes a shell.
    $taskEncoded=@(foreach($taskValue in $Arguments){
        $taskBuilder=[Text.StringBuilder]::new()
        [void]$taskBuilder.Append('"')
        $taskSlashes=0
        foreach($taskCharacter in $taskValue.ToCharArray()){
            if($taskCharacter -eq '\'){$taskSlashes++;continue}
            if($taskCharacter -eq '"'){
                [void]$taskBuilder.Append(('\'*(2*$taskSlashes+1)))
                [void]$taskBuilder.Append('"')
            }else{
                [void]$taskBuilder.Append(('\'*$taskSlashes))
                [void]$taskBuilder.Append($taskCharacter)
            }
            $taskSlashes=0
        }
        [void]$taskBuilder.Append(('\'*(2*$taskSlashes)))
        [void]$taskBuilder.Append('"')
        $taskBuilder.ToString()
    })
    return [string]::Join(' ',$taskEncoded)
}

function New-NativeStartInfo([string]$File,[string[]]$Arguments) {
    $taskInfo=[Diagnostics.ProcessStartInfo]::new()
    $taskInfo.FileName=$File
    $taskInfo.UseShellExecute=$false
    $taskInfo.CreateNoWindow=$true
    $taskInfo.WorkingDirectory=Split-Path -Parent $Executable
    $taskInfo.Arguments=Join-NativeArguments $Arguments
    return $taskInfo
}

function Invoke-StabilizationRun($Run) {
    $taskRunOutput=Join-Path $Output $Run.Name
    if(Test-Path -LiteralPath $taskRunOutput){throw "Run directory already exists: $taskRunOutput"}
    New-Item -ItemType Directory -Path $taskRunOutput | Out-Null
    $taskFlags=@($(if($Run.Phase -eq 'window'){'--mission-smoke-test'}else{'--gpu-stabilization-profile'}),'--gpu-timer-mode',$Run.Mode)
    if($Run.Phase -eq 'throughput' -and $ThroughputScenario -eq 'cost'){$taskFlags=@('--gpu-cost-profile','--gpu-cost-mode',$CostMode,'--gpu-timer-mode',$Run.Mode)}
    $taskFlags+=@('--gpu-phase-gating',$PhaseGating,'--gpu-zero-projection',$ZeroProjection)
    if($Counters -eq 'on'){$taskFlags+='--gpu-counters'}
    if($GroupTimers){$taskFlags+='--gpu-group-timers'}
    if($DebugLog){$taskFlags+='--gl-debug'}
    if($Run.Phase -eq 'window'){
        $taskFlags+=@('--test-vsync',$VSync,'--test-frame-limit',[string]$FrameLimit)
        if($WindowVisibility -eq 'visible'){$taskFlags+='--show-test-window'}
    }
    $taskFlags+=@($ExtraArguments)+@('--profile',$taskRunOutput)
    $taskRecord=[ordered]@{
        schema_version=1;name=$Run.Name;order=$Run.Order;phase=$Run.Phase;mode=$Run.Mode;repeat=$Run.Repeat
        variant_label=$VariantLabel;phase_gating=$PhaseGating;zero_projection=$ZeroProjection;throughput_scenario=$ThroughputScenario;cost_mode=$CostMode
        executable=$Executable;arguments=$taskFlags;profile=$taskRunOutput
        start_utc=[DateTimeOffset]::UtcNow.ToString('o');end_utc=$null;pid=$null;exit_code=$null;timed_out=$false
        telemetry_available=$false;telemetry_error=$null;launch_error=$null
    }
    $taskTelemetry=$null;$taskTelemetryStdout=$null;$taskTelemetryStderr=$null
    $taskProcess=$null;$taskStdout=$null;$taskStderr=$null
    try {
        if($taskNvidiaPath){
            try {
                $taskInfo=New-NativeStartInfo $taskNvidiaPath @('--query-gpu=timestamp,index,name,driver_version,pstate,temperature.gpu,utilization.gpu,utilization.memory,clocks.current.graphics,clocks.current.memory,power.draw,memory.used,memory.total','--format=csv','--loop-ms=500')
                $taskInfo.RedirectStandardOutput=$true;$taskInfo.RedirectStandardError=$true
                $taskTelemetry=[Diagnostics.Process]::Start($taskInfo)
                $taskTelemetryStdout=$taskTelemetry.StandardOutput.ReadToEndAsync()
                $taskTelemetryStderr=$taskTelemetry.StandardError.ReadToEndAsync()
                $taskRecord.telemetry_available=$true
            }catch{$taskRecord.telemetry_error=$_.Exception.Message}
        }
        $taskRunInfo=New-NativeStartInfo $Executable $taskFlags
        $taskRunInfo.RedirectStandardOutput=$true;$taskRunInfo.RedirectStandardError=$true
        Write-Host "Measuring $($Run.Name)"
        $taskProcess=[Diagnostics.Process]::Start($taskRunInfo)
        $taskRecord.pid=$taskProcess.Id
        $taskStdout=$taskProcess.StandardOutput.ReadToEndAsync()
        $taskStderr=$taskProcess.StandardError.ReadToEndAsync()
        if(-not $taskProcess.WaitForExit($TimeoutSeconds*1000)){
            $taskRecord.timed_out=$true
            $taskProcess.Kill()
            $taskProcess.WaitForExit()
        }
        $taskProcess.Refresh()
        $taskRecord.exit_code=$taskProcess.ExitCode
    }catch{$taskRecord.launch_error=$_.Exception.Message}
    finally {
        $taskRecord.end_utc=[DateTimeOffset]::UtcNow.ToString('o')
        if($taskProcess){
            if(-not $taskProcess.HasExited){$taskProcess.Kill();$taskProcess.WaitForExit()}
            if($taskStdout){$taskStdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $taskRunOutput 'process-stdout.txt') -Encoding utf8}
            if($taskStderr){$taskStderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $taskRunOutput 'process-stderr.txt') -Encoding utf8}
            $taskProcess.Dispose()
        }
        if($taskTelemetry){
            if(-not $taskTelemetry.HasExited){$taskTelemetry.Kill();$taskTelemetry.WaitForExit()}
            $taskTelemetry.Refresh()
            $taskTelemetryText=$taskTelemetryStdout.GetAwaiter().GetResult()
            $taskTelemetryText | Set-Content -LiteralPath (Join-Path $taskRunOutput 'nvidia-smi.csv') -Encoding utf8
            $taskTelemetryError=$taskTelemetryStderr.GetAwaiter().GetResult()
            if($taskTelemetryError){$taskRecord.telemetry_error=$taskTelemetryError}
            if(@($taskTelemetryText -split '\r?\n' | Where-Object {$_.Trim()}).Count -lt 2){$taskRecord.telemetry_available=$false}
            $taskTelemetry.Dispose()
        }
        $taskRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskRunOutput 'run.json') -Encoding utf8
        $taskRuns.Add([pscustomobject]@{
            Name=$Run.Name;Order=$Run.Order;Phase=$Run.Phase;Mode=$Run.Mode;Repeat=$Run.Repeat
            Variant=$VariantLabel;PhaseGating=$PhaseGating;ZeroProjection=$ZeroProjection
            StartUtc=$taskRecord.start_utc;EndUtc=$taskRecord.end_utc;Pid=$taskRecord.pid
            ExitCode=$taskRecord.exit_code;TimedOut=$taskRecord.timed_out;LaunchError=$taskRecord.launch_error
            TelemetryAvailable=$taskRecord.telemetry_available
            ArgumentsJson=ConvertTo-Json -InputObject $taskFlags -Compress
        })
        $taskRuns | Export-Csv -LiteralPath (Join-Path $Output 'runs.csv') -NoTypeInformation -Encoding utf8
    }
}

foreach($taskRun in $taskSchedule){Invoke-StabilizationRun $taskRun}
[pscustomobject]@{started_utc=$taskStartedUtc.ToString('o');ended_utc=[DateTimeOffset]::UtcNow.ToString('o');runs=$taskRuns.Count} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Output 'completed.json') -Encoding utf8
Write-Host "Analyze recorded runtime data with: python scripts/analyze-stabilization.py --input `"$Output`""
if(@($taskRuns | Where-Object {$null -eq $_.ExitCode -or $_.ExitCode -ne 0 -or $_.TimedOut -or $_.LaunchError}).Count -gt 0){
    throw 'Some native runs failed or were skipped; all exit codes and completed results are retained in runs.csv.'
}
