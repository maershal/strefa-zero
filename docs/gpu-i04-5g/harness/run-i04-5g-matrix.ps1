$ErrorActionPreference='Stop'
$taskRoot='C:\Users\mrmae\Documents\Codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3'
& (Join-Path $taskRoot 'outputs\strefa-zero\scripts\profile-pbf.ps1') -Executable (Join-Path $taskRoot 'work\build-strefa-zero-release\runtime-070\StrefaZero.exe') -BaselineExecutable (Join-Path $taskRoot 'outputs\StrefaZero-Windows-x64-I04_5-Q\StrefaZero.exe') -Output (Join-Path $taskRoot 'work\i04-5g-results\matrix')
