$ErrorActionPreference='Stop'
$taskRoot='C:\Users\mrmae\Documents\Codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3'
$taskExe=Join-Path $taskRoot 'work\build-strefa-zero-release\runtime-070\StrefaZero.exe'
$taskScript=Join-Path $taskRoot 'outputs\strefa-zero\scripts\profile-hot.ps1'
& $taskScript -Executable $taskExe -Output (Join-Path $taskRoot 'work\i04-5d-results\windows-comparison') -Variants @('full','narrow') -Repetitions 3 -SkipThroughput -DebugLog -TimeoutSeconds 120
& $taskScript -Executable $taskExe -Output (Join-Path $taskRoot 'work\i04-5d-results\window-sorted-diagnostic') -Variants @('sorted') -Repetitions 1 -SkipThroughput -DebugLog -TimeoutSeconds 120
