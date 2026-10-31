$ErrorActionPreference='Stop'
$taskRoot='C:\Users\mrmae\Documents\Codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3'
$taskScript=Join-Path $taskRoot 'outputs\strefa-zero\scripts\profile-stabilization.ps1'
$taskOutput=Join-Path $taskRoot 'work\i04-5g-results\windows'
$taskRuns=[Collections.Generic.List[object]]::new()
foreach($taskRepeat in 1..2){
    $taskOrder=if($taskRepeat -eq 1){@('q','g256','g128','g64')}else{@('g64','g128','g256','q')}
    foreach($taskVariant in $taskOrder){
        $taskName="$taskVariant-$taskRepeat"
        $taskExe=if($taskVariant -eq 'q'){Join-Path $taskRoot 'outputs\StrefaZero-Windows-x64-I04_5-Q\StrefaZero.exe'}else{Join-Path $taskRoot 'outputs\StrefaZero-Windows-x64-I04_5-G\StrefaZero.exe'}
        [string[]]$taskExtra=@()
        if($taskVariant -ne 'q'){$taskExtra=@('--gpu-pbf-size',$taskVariant.Substring(1))}
        $taskGroup=Join-Path $taskOutput $taskName
        & $taskScript -Executable $taskExe -Output $taskGroup -SkipThroughput -WindowVisibility visible -Modes @('elapsed') -Repetitions 1 -DebugLog -VariantLabel $taskVariant -TimeoutSeconds 120 -ExtraArguments $taskExtra
        foreach($taskChild in Import-Csv -LiteralPath (Join-Path $taskGroup 'runs.csv')){
            $taskRuns.Add([pscustomobject]@{Name=$taskName;Phase='window';Mode='elapsed';Variant=$taskVariant;Repeat=$taskRepeat
                ProfileDirectory=(Join-Path $taskName $taskChild.Name).Replace('\','/');ExitCode=$taskChild.ExitCode;TimedOut=$taskChild.TimedOut
                Pid=$taskChild.Pid;LaunchError=$taskChild.LaunchError;ArgumentsJson=$taskChild.ArgumentsJson})
        }
        $taskRuns | Export-Csv -LiteralPath (Join-Path $taskOutput 'runs.csv') -NoTypeInformation -Encoding utf8
    }
}
