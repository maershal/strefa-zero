$ErrorActionPreference='Stop'
$taskRoot='C:\Users\mrmae\Documents\Codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3'
$taskScript=Join-Path $taskRoot 'outputs\strefa-zero\scripts\profile-stabilization.ps1'
$taskOutput=Join-Path $taskRoot 'work\i04-5g2-results\windows'
$taskRuns=[Collections.Generic.List[object]]::new()
foreach($taskRepeat in 1..3){
    $taskOrder=if($taskRepeat -eq 2){@('on','off')}else{@('off','on')}
    foreach($taskMode in $taskOrder){
        $taskVariant="g2$taskMode";$taskName="$taskVariant-$taskRepeat"
        $taskExe=Join-Path $taskRoot 'outputs\StrefaZero-Windows-x64-I04_5-G2\StrefaZero.exe'
        [string[]]$taskExtra=@('--gpu-pbf-restrict',$taskMode,'--gpu-local-size','256','--gpu-reduction-size','256','--gpu-pbf-size','256')
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
