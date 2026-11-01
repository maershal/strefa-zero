$ErrorActionPreference='Stop'
$taskRoot='C:\Users\mrmae\Documents\Codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3'
$taskScript=Join-Path $taskRoot 'outputs\strefa-zero\scripts\profile-stabilization.ps1'
$taskOutput=Join-Path $taskRoot 'work\i04-5e-results\disabled-control'
$taskRuns=[Collections.Generic.List[object]]::new()
foreach($taskRepeat in 1..3){
    $taskOrder=if($taskRepeat%2){@('D','E0')}else{@('E0','D')}
    foreach($taskVariant in $taskOrder){
        $taskName="control-$taskVariant-$taskRepeat"
        $taskExe=if($taskVariant -eq 'D'){Join-Path $taskRoot 'outputs\StrefaZero-Windows-x64-I04_5-D\StrefaZero.exe'}else{Join-Path $taskRoot 'work\build-strefa-zero-release\runtime-070\StrefaZero.exe'}
        $taskGroup=Join-Path $taskOutput $taskName
        & $taskScript -Executable $taskExe -Output $taskGroup -Modes @('elapsed') -Repetitions 1 -SkipWindows -VariantLabel $taskVariant
        foreach($taskChild in Import-Csv -LiteralPath (Join-Path $taskGroup 'runs.csv')){
            $taskRuns.Add([pscustomobject]@{Name=$taskName;Phase='throughput';Mode='elapsed';Variant=$taskVariant;Repeat=$taskRepeat
                ProfileDirectory=(Join-Path $taskName $taskChild.Name).Replace('\','/');ExitCode=$taskChild.ExitCode;TimedOut=$taskChild.TimedOut
                Pid=$taskChild.Pid;LaunchError=$taskChild.LaunchError;ArgumentsJson=$taskChild.ArgumentsJson})
        }
        $taskRuns | Export-Csv -LiteralPath (Join-Path $taskOutput 'runs.csv') -NoTypeInformation -Encoding utf8
    }
}
