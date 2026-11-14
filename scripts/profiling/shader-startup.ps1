#Requires -Version 7.2
# Z06: time to the first GPU world (shader compilation) for two builds in ABBA order.
#
#   pwsh scripts/profiling/shader-startup.ps1 -Baseline <main>\StrefaQA.exe -Candidate build\release\StrefaQA.exe `
#        -Output shader-startup-2026-11-14
#
# Every run is `StrefaQA --mission-ui-smoke-test` in a fresh profile; the numbers come from its gpu-startup.txt.
# Modes:
#   cold    driver disk cache off and empty (NVIDIA __GL_SHADER_DISK_CACHE*, Mesa MESA_SHADER_CACHE_DISABLE),
#           no program binaries: the first start on a new driver;
#   driver  the driver's own disk cache, warmed by one unmeasured run per build: the usual second start;
#   binary  --shader-cache on, warmed by one unmeasured run, driver cache off: the I05 program-binary cache.
# Writes shader-startup.csv (one row per run, GPU temperature from nvidia-smi before the run when available).
param(
    [Parameter(Mandatory = $true)][string]$Baseline,
    [Parameter(Mandatory = $true)][string]$Candidate,
    [Parameter(Mandatory = $true)][string]$Output,
    [ValidateRange(1, 20)][int]$Repeat = 3,
    [string[]]$Modes = @('cold', 'driver', 'binary'),
    [ValidateRange(0, 600)][int]$PauseSeconds = 5
)
$ErrorActionPreference = 'Stop'
$Output = [System.IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force $Output | Out-Null
$builds = [ordered]@{ A = [System.IO.Path]::GetFullPath($Baseline); B = [System.IO.Path]::GetFullPath($Candidate) }
$fields = 'programs', 'source_builds', 'cache_hits', 'parallel_compile', 'compile_ms', 'submit_ms', 'progress_ms',
    'progress_gap_ms', 'cache_load_ms', 'first_world_ms'
$smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
$rows = [System.Collections.Generic.List[object]]::new()

function Invoke-Start([string]$Build, [string]$Mode, [string]$Name, [bool]$Measured) {
    $root = Join-Path $Output "runs\$Mode-$Build-$Name"
    $driverCache = Join-Path $Output "driver-cache\$Mode-$Build"
    if ($Mode -ne 'driver') { Remove-Item -Recurse -Force $driverCache -ErrorAction SilentlyContinue }
    New-Item -ItemType Directory -Force $root, $driverCache | Out-Null
    $profile = Join-Path $root 'profile'
    $arguments = @('--mission-ui-smoke-test', '--profile', $profile)
    if ($Mode -eq 'binary') {
        $arguments += @('--shader-cache', 'on')
        # The program-binary cache lives in the profile; keep one per build across its runs.
        $shared = Join-Path $Output "binary-cache\$Build"
        New-Item -ItemType Directory -Force $shared, $profile | Out-Null
        New-Item -ItemType Junction -Path (Join-Path $profile 'shader-cache') -Target $shared | Out-Null
    }
    $env:__GL_SHADER_DISK_CACHE = if ($Mode -eq 'driver') { '1' } else { '0' }
    $env:__GL_SHADER_DISK_CACHE_PATH = $driverCache
    $env:__GL_SHADER_DISK_CACHE_SKIP_CLEANUP = '1'
    $env:MESA_SHADER_CACHE_DISABLE = if ($Mode -eq 'driver') { 'false' } else { 'true' }
    $env:MESA_SHADER_CACHE_DIR = $driverCache
    $temperature = if ($smi) { (& nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits | Select-Object -First 1) } else { '' }
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    & $builds[$Build] @arguments *> (Join-Path $root 'stdout.txt')
    $exit = $LASTEXITCODE
    $watch.Stop()
    if (-not $Measured) { return }
    $row = [ordered]@{ mode = $Mode; build = $Build; run = $Name; exit = $exit; gpu_temp_c = $temperature
        process_s = [math]::Round($watch.Elapsed.TotalSeconds, 3) }
    $startup = Join-Path $profile 'gpu-startup.txt'
    $values = @{}
    if (Test-Path $startup) {
        foreach ($line in Get-Content $startup) { $key, $value = $line -split '=', 2; $values[$key] = $value }
    }
    foreach ($field in $fields) { $row[$field] = $values[$field] }
    $rows.Add([pscustomobject]$row)
    Write-Host ("{0,-6} {1} {2}: first_world_ms={3} compile_ms={4} submit_ms={5} exit={6}" -f $Mode, $Build, $Name,
        $row.first_world_ms, $row.compile_ms, $row.submit_ms, $exit)
}

foreach ($mode in $Modes) {
    if ($mode -ne 'cold') { foreach ($build in $builds.Keys) { Invoke-Start $build $mode 'warmup' $false } }
    for ($r = 1; $r -le $Repeat; ++$r) {
        $order = if ($r % 2) { 'A', 'B', 'B', 'A' } else { 'B', 'A', 'A', 'B' }
        $index = 0
        foreach ($build in $order) {
            Start-Sleep -Seconds $PauseSeconds
            Invoke-Start $build $mode "r$r-$index" $true
            ++$index
        }
    }
}
Remove-Item Env:__GL_SHADER_DISK_CACHE, Env:__GL_SHADER_DISK_CACHE_PATH, Env:__GL_SHADER_DISK_CACHE_SKIP_CLEANUP,
    Env:MESA_SHADER_CACHE_DISABLE, Env:MESA_SHADER_CACHE_DIR -ErrorAction SilentlyContinue
$csv = Join-Path $Output 'shader-startup.csv'
$rows | Export-Csv -NoTypeInformation -Path $csv
Write-Host "Median first_world_ms per mode and build (A = baseline, B = candidate):"
$rows | Group-Object mode, build | ForEach-Object {
    $values = @($_.Group | Where-Object { $_.first_world_ms } | ForEach-Object { [double]$_.first_world_ms } | Sort-Object)
    $median = if ($values.Count) { $values[[int][math]::Floor(($values.Count - 1) / 2)] } else { 'n/a' }
    Write-Host ("  {0}: {1} ms ({2} runs)" -f $_.Name, $median, $values.Count)
}
Write-Host "CSV: $csv"
