$ErrorActionPreference='Stop'
$taskRoot='C:\Users\mrmae\Documents\Codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3'
$taskScript=Join-Path $taskRoot 'outputs\strefa-zero\scripts\profile-stabilization.ps1'
$taskExe=Join-Path $taskRoot 'outputs\StrefaZero-Windows-x64-I04_5-G\StrefaZero.exe'
foreach($taskVariant in @('default','explicit')){
    [string[]]$taskExtra=@()
    if($taskVariant -eq 'explicit'){$taskExtra=@('--gpu-local-size','256','--gpu-reduction-size','256','--gpu-pbf-size','256')}
    & $taskScript -Executable $taskExe -Output (Join-Path $taskRoot "work\i04-5g2-results\condition-control\$taskVariant") -ThroughputScenario cost -CostMode off -SkipWindows -Modes @('elapsed') -Repetitions 1 -VariantLabel $taskVariant -ExtraArguments $taskExtra -TimeoutSeconds 120
}
