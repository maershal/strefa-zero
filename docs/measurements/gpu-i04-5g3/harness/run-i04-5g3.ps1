$ErrorActionPreference='Stop'
$taskScript='C:\users\mrmae\documents\codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\outputs\strefa-zero\scripts\profile-variance.ps1'
$taskBase='C:\users\mrmae\documents\codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\outputs\StrefaZero-Windows-x64-I04_5-G\StrefaZero.exe'
$taskTrigger='C:\users\mrmae\documents\codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\outputs\StrefaZero-Windows-x64-I04_5-G2\StrefaZero.exe'
& $taskScript -BaselineExecutable $taskBase -Output 'C:\users\mrmae\documents\codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\work\i04-5g3-results\controls' -Protocol controls -TimeoutSeconds 180
& $taskScript -BaselineExecutable $taskBase -TriggerExecutable $taskTrigger -Output 'C:\users\mrmae\documents\codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\work\i04-5g3-results\reproduction' -Protocol reproduction -ObserveGpuEngines -TimeoutSeconds 300
