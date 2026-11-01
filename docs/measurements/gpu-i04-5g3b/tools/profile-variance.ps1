param(
    [Parameter(Mandatory=$true)][string]$BaselineExecutable,
    [string]$TriggerExecutable,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet('controls','reproduction','replay')][string]$Protocol='controls',
    [string]$ReplayPlan,
    [ValidateRange(10,3600)][int]$TimeoutSeconds=300,
    [switch]$ObserveGpuEngines,
    [string]$NvidiaSmi
)
$ErrorActionPreference='Stop'
$BaselineExecutable=(Resolve-Path -LiteralPath $BaselineExecutable).Path
if($TriggerExecutable){$TriggerExecutable=(Resolve-Path -LiteralPath $TriggerExecutable).Path}
$Output=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $Output){throw 'Choose a fresh output directory.'}
# Keep the exact immutable G1 hash explicit; no rebuilt baseline is accepted.
$taskG1='1a1f53457ca590187417818f24d42bbea93d11c7b29bc5e7d8fea7320f918d5e'
$taskG2='333287c4063fa722eb4314fd70c0f6509cc92479c7890ee1da772bf1b0443af8'
if((Get-FileHash -LiteralPath $BaselineExecutable -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskG1){throw 'Baseline is not frozen G1.'}
if($Protocol -in @('reproduction','replay')){
    if(-not $TriggerExecutable){throw 'Reproduction requires the frozen G2 trigger executable.'}
    if((Get-FileHash -LiteralPath $TriggerExecutable -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskG2){throw 'Trigger is not frozen G2.'}
}
if($Protocol -eq 'controls' -and $ObserveGpuEngines){throw 'Controls isolate window observer overhead; GPU engine worker must be OFF.'}
$taskReplay=$null
if($Protocol -eq 'replay'){
    if(-not $ReplayPlan){throw 'Replay requires a preregistered plan.'}
    $taskReplay=Get-Content -LiteralPath $ReplayPlan -Raw | ConvertFrom-Json
    if($taskReplay.schema_version -ne 1 -or $taskReplay.baseline_sha256 -ne $taskG1 -or $taskReplay.trigger_sha256 -ne $taskG2){throw 'Replay plan identity mismatch.'}
    if([IO.Path]::GetFileName($taskReplay.ctest.executable) -ne 'ctest.exe' -or (Get-FileHash -LiteralPath $taskReplay.ctest.executable -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskReplay.ctest.sha256){throw 'CTest executable mismatch.'}
    if(@($taskReplay.names).Count -ne 15 -or [string]::Join(',',@($taskReplay.names)) -ne 'pre-off-1,pre-off-2,full-qa,q-off-1,q-on-1,q-on-2,q-off-2,q-on-64,q-on-128,post-off-1,post-off-2,post-bounds,post-off-3,post-snapshots,post-off-4'){throw 'Replay schedule mismatch.'}
}
New-Item -ItemType Directory -Path $Output | Out-Null
if(-not ('StrefaVariance.WindowObserver' -as [type])){Add-Type -Path (Join-Path $PSScriptRoot 'variance-window-observer.cs')}

function Join-NativeArguments([string[]]$Arguments){
    $taskEncoded=@(foreach($taskValue in $Arguments){
        $taskBuilder=[Text.StringBuilder]::new();[void]$taskBuilder.Append('"');$taskSlashes=0
        foreach($taskCharacter in $taskValue.ToCharArray()){
            if($taskCharacter -eq '\'){$taskSlashes++;continue}
            if($taskCharacter -eq '"'){[void]$taskBuilder.Append(('\'*(2*$taskSlashes+1)));[void]$taskBuilder.Append('"')}
            else{[void]$taskBuilder.Append(('\'*$taskSlashes));[void]$taskBuilder.Append($taskCharacter)}
            $taskSlashes=0
        }
        [void]$taskBuilder.Append(('\'*(2*$taskSlashes)));[void]$taskBuilder.Append('"');$taskBuilder.ToString()
    });return [string]::Join(' ',$taskEncoded)
}
function New-NativeInfo([string]$File,[string[]]$Arguments){
    $taskInfo=[Diagnostics.ProcessStartInfo]::new();$taskInfo.FileName=$File
    $taskInfo.Arguments=Join-NativeArguments $Arguments;$taskInfo.UseShellExecute=$false;$taskInfo.CreateNoWindow=$true
    $taskInfo.RedirectStandardOutput=$true;$taskInfo.RedirectStandardError=$true
    $taskInfo.WorkingDirectory=Split-Path -Parent $BaselineExecutable
    return $taskInfo
}
$taskNvidiaPath=$null
if($NvidiaSmi){$taskNvidiaPath=(Resolve-Path -LiteralPath $NvidiaSmi).Path}
else{
    $taskNv=Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if($taskNv){$taskNvidiaPath=$taskNv.Source}
    else{
        $taskDriverStore=Join-Path $env:WINDIR 'System32\DriverStore\FileRepository'
        $taskNv=Get-ChildItem -LiteralPath $taskDriverStore -Directory -Filter 'nv*' | ForEach-Object {
            Get-ChildItem -LiteralPath $_.FullName -File -Filter 'nvidia-smi.exe' -ErrorAction SilentlyContinue
        } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        if($taskNv){$taskNvidiaPath=$taskNv.FullName}
    }
}
$taskSchedule=[Collections.Generic.List[object]]::new();$taskRuns=[Collections.Generic.List[object]]::new()
function Add-Job([string]$Name,[string]$Kind='cost',[string]$Mode='off',[bool]$Visible=$false,[bool]$Observe=$true,[int]$Idle=0){
    $taskSchedule.Add([pscustomobject]@{Order=$taskSchedule.Count+1;Name=$Name;Kind=$Kind;CostMode=$Mode;Visible=$Visible;Observe=$Observe;IdleBeforeSeconds=$Idle})
}
if($Protocol -eq 'controls'){
    Add-Job 'observer-off-1' 'cost' 'off' $false $false
    Add-Job 'observer-on-1';Add-Job 'observer-on-2'
    Add-Job 'observer-off-2' 'cost' 'off' $false $false
}elseif($Protocol -eq 'reproduction'){
    Add-Job 'validation' 'validation'
    Add-Job 'hidden-before-1';Add-Job 'hidden-before-2'
    Add-Job 'q-trigger-64' 'trigger64';Add-Job 'hidden-after-q64'
    Add-Job 'q-trigger-128' 'trigger128';Add-Job 'hidden-after-q128'
    Add-Job 'bounds' 'cost' 'bounds';Add-Job 'hidden-after-bounds'
    Add-Job 'snapshots-1' 'cost' 'snapshots';Add-Job 'hidden-after-snapshots'
    Add-Job 'passes' 'cost' 'passes';Add-Job 'hidden-before-visible'
    Add-Job 'visible-1' 'cost' 'off' $true;Add-Job 'visible-2' 'cost' 'off' $true
    Add-Job 'hidden-after-visible'
    Add-Job 'window-control' 'window' 'off' $true;Add-Job 'hidden-after-window'
    Add-Job 'hidden-after-idle' 'cost' 'off' $false $true 20
    Add-Job 'snapshots-2' 'cost' 'snapshots';Add-Job 'hidden-closing'
}else{
    Add-Job 'pre-off-1';Add-Job 'pre-off-2';Add-Job 'full-qa' 'qa' 'off' $false $false
    Add-Job 'q-off-1' 'triggerOff';Add-Job 'q-on-1' 'triggerOn';Add-Job 'q-on-2' 'triggerOn';Add-Job 'q-off-2' 'triggerOff'
    Add-Job 'q-on-64' 'trigger64';Add-Job 'q-on-128' 'trigger128'
    Add-Job 'post-off-1';Add-Job 'post-off-2';Add-Job 'post-bounds' 'cost' 'bounds';Add-Job 'post-off-3'
    Add-Job 'post-snapshots' 'cost' 'snapshots';Add-Job 'post-off-4'
}
$taskSchedule | Export-Csv -LiteralPath (Join-Path $Output 'schedule.csv') -NoTypeInformation -Encoding utf8
$taskShaderDirectory=Join-Path (Split-Path -Parent $BaselineExecutable) 'assets\shaders'
Get-ChildItem -LiteralPath $taskShaderDirectory -File -Recurse | Sort-Object FullName | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$_.FullName.Substring($taskShaderDirectory.Length+1).Replace('\','/')
} | Set-Content -LiteralPath (Join-Path $Output 'shader-sha256.txt') -Encoding utf8
[ordered]@{schema_version=1;protocol=$Protocol;baseline_executable=$BaselineExecutable;baseline_sha256=$taskG1
    trigger_executable=$TriggerExecutable;trigger_sha256=$(if($TriggerExecutable){$taskG2}else{$null})
    replay_plan=$taskReplay;started_utc=[DateTimeOffset]::UtcNow.ToString('o');powershell=$PSVersionTable.PSVersion.ToString()
    os=[Environment]::OSVersion.VersionString;observer_period_ms=250;nvidia_smi=$taskNvidiaPath;gpu_engine_worker=[bool]$ObserveGpuEngines
    settings='Cost: full/direct, groups 256, counters ON, elapsed, C OFF. G2 only a correctness/load trigger.'
    limitations='Read-only observations; outer window coordinates can be DPI virtualized. Scene export markers are approximate end times. Visible cost has no regular event/present loop. GPU engine counters are asynchronous, include initialization/idle and may retain exited PIDs.'} |
    ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $Output 'environment.json') -Encoding utf8

function Stop-OwnedNative($Child,[bool]$Tree){
    if($Tree){
        $taskKill=[Diagnostics.Process]::Start((New-NativeInfo (Join-Path $env:WINDIR 'System32\taskkill.exe') @('/PID',[string]$Child.Id,'/T','/F')))
        $taskKillOut=$taskKill.StandardOutput.ReadToEndAsync();$taskKillErr=$taskKill.StandardError.ReadToEndAsync()
        $taskKill.WaitForExit();[void]$taskKillOut.GetAwaiter().GetResult();[void]$taskKillErr.GetAwaiter().GetResult();$taskKill.Dispose()
    }
    if(-not $Child.HasExited){$Child.Kill()};$Child.WaitForExit()
}
function Invoke-VarianceRun($Run){
    if($Run.IdleBeforeSeconds){Start-Sleep -Seconds $Run.IdleBeforeSeconds}
    $taskDir=Join-Path $Output $Run.Name;New-Item -ItemType Directory -Path $taskDir | Out-Null
    $taskExe=$BaselineExecutable
    [string[]]$taskFlags=@()
    switch($Run.Kind){
        'cost'{
            foreach($taskScene in @('mission','stress_16k','stress_32k','mission_blast')){New-Item -ItemType Directory -Path (Join-Path $taskDir $taskScene) | Out-Null}
            $taskFlags=@('--gpu-cost-profile','--gpu-cost-mode',$Run.CostMode,'--gpu-timer-mode','elapsed','--gpu-counters',
                '--gpu-phase-gating','off','--gpu-zero-projection','off','--gpu-local-size','256','--gpu-reduction-size','256','--gpu-pbf-size','256')
        }
        'qa'{$taskExe=$taskReplay.ctest.executable;$taskFlags=@($taskReplay.ctest.flags)}
        'validation'{$taskFlags=@('--gpu-cost-check')}
        'window'{$taskFlags=@('--mission-smoke-test','--gpu-counters','--gl-debug','--test-vsync','off','--test-frame-limit','60')}
        default{
            $taskExe=$TriggerExecutable
            $taskFlags=@('--gpu-coupling-check','--gpu-pbf-restrict',$(if($Run.Kind -eq 'triggerOff'){'off'}else{'on'}))
            if($Run.Kind -in @('trigger64','trigger128')){$taskFlags+=@('--gpu-pbf-size',$(if($Run.Kind -eq 'trigger64'){'64'}else{'128'}))}
        }
    }
    if($Run.Visible){$taskFlags+='--show-test-window'}
    if($Run.Kind -ne 'qa'){$taskFlags+=@('--profile',$taskDir)}
    $taskLimit=if($Run.Kind -eq 'qa'){1500}else{$TimeoutSeconds}
    $taskRecord=[ordered]@{schema_version=1;name=$Run.Name;order=$Run.Order;kind=$Run.Kind;cost_mode=$Run.CostMode
        visible_requested=$Run.Visible;window_observer=$Run.Observe;executable=$taskExe
        executable_sha256=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash.ToLowerInvariant();arguments=$taskFlags;timeout_seconds=$taskLimit
        start_utc=[DateTimeOffset]::UtcNow.ToString('o');end_utc=$null;pid=$null;exit_code=$null;timed_out=$false
        launch_error=$null;telemetry_available=$false;telemetry_error=$null;observer_samples=0}
    $taskSamples=[Collections.Generic.List[object]]::new();$taskMarkers=[Collections.Generic.List[object]]::new();$taskSeen=@{}
    $taskChild=$null;$taskNvChild=$null;$taskOut=$null;$taskErr=$null;$taskNvOut=$null;$taskNvErr=$null
    $taskWatch=[Diagnostics.Stopwatch]::StartNew()
    try{
        if($taskNvidiaPath){
            try{
                $taskNvChild=[Diagnostics.Process]::Start((New-NativeInfo $taskNvidiaPath @('--query-gpu=timestamp,index,name,driver_version,pstate,temperature.gpu,utilization.gpu,utilization.memory,clocks.current.graphics,clocks.current.memory,power.draw,memory.used,memory.total','--format=csv','--loop-ms=500')))
                $taskNvOut=$taskNvChild.StandardOutput.ReadToEndAsync();$taskNvErr=$taskNvChild.StandardError.ReadToEndAsync()
                $taskRecord.telemetry_available=$true
            }catch{$taskRecord.telemetry_error=$_.Exception.Message}
        }
        Write-Host "Variance $($Run.Order): $($Run.Name)"
        $taskRunInfo=New-NativeInfo $taskExe $taskFlags
        $taskRunInfo.WorkingDirectory=if($Run.Kind -eq 'qa'){$taskReplay.ctest.working_directory}else{Split-Path -Parent $taskExe}
        $taskChild=[Diagnostics.Process]::Start($taskRunInfo)
        $taskRecord.pid=$taskChild.Id;$taskOut=$taskChild.StandardOutput.ReadToEndAsync();$taskErr=$taskChild.StandardError.ReadToEndAsync()
        while(-not $taskChild.WaitForExit(250)){
            if($taskWatch.Elapsed.TotalSeconds -gt $taskLimit){$taskRecord.timed_out=$true;Stop-OwnedNative $taskChild ($Run.Kind -eq 'qa');break}
            if($Run.Observe){
                $taskSensor=[Diagnostics.Stopwatch]::StartNew();$taskUtc=[DateTimeOffset]::UtcNow.ToString('o')
                $taskWindow=[StrefaVariance.WindowObserver]::Read($taskChild.Id)
                $taskCpu=$null;$taskWorking=$null;$taskPriority=$null
                try{$taskChild.Refresh();$taskCpu=$taskChild.TotalProcessorTime.TotalMilliseconds;$taskWorking=$taskChild.WorkingSet64;$taskPriority=[string]$taskChild.PriorityClass}catch{}
                $taskSamples.Add([pscustomobject]@{Utc=$taskUtc;ElapsedMs=$taskWatch.Elapsed.TotalMilliseconds;Pid=$taskRecord.pid
                    Windows=$taskWindow.Count;Visible=$taskWindow.Visible;Minimized=$taskWindow.Minimized
                    OuterWidth=$taskWindow.LargestOuterWidth;OuterHeight=$taskWindow.LargestOuterHeight
                    ForegroundPid=$taskWindow.ForegroundPid;NativeForeground=$taskWindow.NativeForeground
                    AcLine=$taskWindow.AcLine;BatteryPercent=$taskWindow.BatteryPercent
                    CpuTotalMs=$taskCpu;WorkingSetBytes=$taskWorking;Priority=$taskPriority;ObserverMs=$taskSensor.Elapsed.TotalMilliseconds})
            }
            if($Run.Kind -eq 'cost'){
                foreach($taskScene in @('mission','stress_16k','stress_32k','mission_blast')){
                    if(-not $taskSeen.ContainsKey($taskScene) -and (Test-Path -LiteralPath (Join-Path $taskDir "$taskScene\window-steps.csv"))){
                        $taskSeen[$taskScene]=$true;$taskMarkers.Add([pscustomobject]@{Scene=$taskScene;ObservedUtc=[DateTimeOffset]::UtcNow.ToString('o');ElapsedMs=$taskWatch.Elapsed.TotalMilliseconds})
                    }
                }
            }
        }
        $taskChild.Refresh();$taskRecord.exit_code=$taskChild.ExitCode
    }catch{$taskRecord.launch_error=$_.Exception.Message}
    finally{
        if($taskChild){
            if(-not $taskChild.HasExited){Stop-OwnedNative $taskChild ($Run.Kind -eq 'qa')}
            if($taskOut){$taskOut.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $taskDir 'process-stdout.txt') -Encoding utf8}
            if($taskErr){$taskErr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $taskDir 'process-stderr.txt') -Encoding utf8};$taskChild.Dispose()
        }
        if($taskNvChild){
            if(-not $taskNvChild.HasExited){$taskNvChild.Kill();$taskNvChild.WaitForExit()}
            $taskNvText=$taskNvOut.GetAwaiter().GetResult();$taskNvText | Set-Content -LiteralPath (Join-Path $taskDir 'nvidia-smi.csv') -Encoding utf8
            $taskNvError=$taskNvErr.GetAwaiter().GetResult();if($taskNvError){$taskRecord.telemetry_error=$taskNvError}
            if(@($taskNvText -split '\r?\n' | Where-Object {$_.Trim()}).Count -lt 2){$taskRecord.telemetry_available=$false};$taskNvChild.Dispose()
        }
        $taskRecord.end_utc=[DateTimeOffset]::UtcNow.ToString('o');$taskRecord.observer_samples=$taskSamples.Count
        $taskSamples | Export-Csv -LiteralPath (Join-Path $taskDir 'host-samples.csv') -NoTypeInformation -Encoding utf8
        $taskMarkers | Export-Csv -LiteralPath (Join-Path $taskDir 'scene-export-markers.csv') -NoTypeInformation -Encoding utf8
        $taskRecord | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskDir 'run.json') -Encoding utf8
        $taskRuns.Add([pscustomobject]@{Name=$Run.Name;Order=$Run.Order;Kind=$Run.Kind;CostMode=$Run.CostMode;Visible=$Run.Visible;Observe=$Run.Observe
            Pid=$taskRecord.pid;StartUtc=$taskRecord.start_utc;EndUtc=$taskRecord.end_utc;ExitCode=$taskRecord.exit_code;TimedOut=$taskRecord.timed_out;LaunchError=$taskRecord.launch_error})
        $taskRuns | Export-Csv -LiteralPath (Join-Path $Output 'runs.csv') -NoTypeInformation -Encoding utf8
    }
}
$taskGpuWorker=$null;$taskGpuOut=$null;$taskGpuErr=$null;$taskStop=Join-Path $Output 'observer.stop'
try{
    if($ObserveGpuEngines){
        $taskGpuWorker=[Diagnostics.Process]::Start((New-NativeInfo (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'variance-gpu-observer.ps1'),'-Output',$Output,'-StopFile',$taskStop,'-MaxSeconds',$(if($Protocol -eq 'replay'){'2400'}else{'1800'}))))
        $taskGpuOut=$taskGpuWorker.StandardOutput.ReadToEndAsync();$taskGpuErr=$taskGpuWorker.StandardError.ReadToEndAsync()
    }
    foreach($taskRun in $taskSchedule){
        Invoke-VarianceRun $taskRun
        if($Protocol -eq 'replay'){
            $taskLatest=$taskRuns[$taskRuns.Count-1]
            if($taskLatest.ExitCode -ne 0 -or $taskLatest.TimedOut -or $taskLatest.LaunchError){throw 'Replay stopped after a failed native process; partial evidence retained.'}
            if($taskRun.Kind -eq 'qa'){
                [xml]$taskJUnit=Get-Content -LiteralPath $taskReplay.ctest.junit -Raw
                $taskCases=@($taskJUnit.SelectNodes('//testcase'))
                if($taskCases.Count -ne 52 -or @($taskJUnit.SelectNodes('//testcase/failure | //testcase/error | //testcase/skipped')).Count){throw 'Full QA did not pass 52/52; refusing dependent performance interpretation.'}
            }
        }
    }
}finally{
    if($taskGpuWorker){
        [IO.File]::WriteAllText($taskStop,'stop')
        if(-not $taskGpuWorker.WaitForExit(15000)){$taskGpuWorker.Kill();$taskGpuWorker.WaitForExit()}
        $taskGpuOut.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $Output 'gpu-worker-stdout.txt') -Encoding utf8
        $taskGpuErr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $Output 'gpu-worker-stderr.txt') -Encoding utf8
        $taskGpuWorker.Dispose()
    }
}
[ordered]@{ended_utc=[DateTimeOffset]::UtcNow.ToString('o');runs=$taskRuns.Count} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Output 'completed.json') -Encoding utf8
if($taskRuns.Count -ne $taskSchedule.Count -or @($taskRuns | Where-Object {$_.ExitCode -ne 0 -or $_.TimedOut -or $_.LaunchError}).Count){throw 'Some native runs failed; all partial records are preserved.'}
