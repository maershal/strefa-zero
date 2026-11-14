#Requires -Version 7.2
# Z06: time to the first GPU world (shader compilation) for two builds in ABBA order.
#
#   pwsh scripts/profiling/shader-startup.ps1 -Baseline <main>\StrefaQA.exe -Candidate build\release\StrefaQA.exe `
#        -Output shader-startup-2026-11-14
#
# Every run is `StrefaQA --mission-ui-smoke-test` in a fresh profile (default settings: 1280x800 window, VSync on);
# the numbers come from its gpu-startup.txt. Modes:
#   cold    driver disk cache off and empty (NVIDIA __GL_SHADER_DISK_CACHE*, Mesa MESA_SHADER_CACHE_DISABLE),
#           no program binaries: the first start on a new driver;
#   driver  the driver's own disk cache, warmed by one unmeasured run per build: the usual second start;
#   binary  --shader-cache on, warmed by one unmeasured run, driver cache off: the I05 program-binary cache.
# Runs on Windows and on Linux (pwsh; the binary mode shares its cache through a junction or a symbolic link).
# Measurement protocol (CLAUDE.md): the script idles -IdleSeconds (default 300) before the first run so the GPU
# starts cooled, and with nvidia-smi it records GPU telemetry (GpuTelemetry.psm1) for the whole session. Each row
# gets the temperature before the run and the clock-event reasons seen during it; a run with hw_* or
# sw_thermal_slowdown active is marked disqualified and left out of the medians (sw_power_cap is only reported).
# The full telemetry query every 100 ms adds about 50 ms to a warm (~85 ms) start on GTX 1650 (Z06 report); compare
# A and B only within one series, or use -TelemetryIntervalMs 500 when absolute warm-start times matter.
# Writes shader-startup.csv (one row per run), telemetry.csv and summary.txt.
param(
    [Parameter(Mandatory = $true)][string]$Baseline,
    [Parameter(Mandatory = $true)][string]$Candidate,
    [Parameter(Mandatory = $true)][string]$Output,
    [ValidateRange(1, 20)][int]$Repeat = 3,
    [string[]]$Modes = @('cold', 'driver', 'binary'),
    [ValidateRange(0, 600)][int]$PauseSeconds = 15,
    [ValidateRange(0, 3600)][int]$IdleSeconds = 300,
    [ValidateRange(100, 10000)][int]$TelemetryIntervalMs = 100
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'GpuTelemetry.psm1') -Force
$Output = [System.IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force $Output | Out-Null
$builds = [ordered]@{ A = [System.IO.Path]::GetFullPath($Baseline); B = [System.IO.Path]::GetFullPath($Candidate) }
$fields = 'programs', 'source_builds', 'cache_hits', 'parallel_compile', 'compile_ms', 'submit_ms', 'progress_ms',
    'progress_gap_ms', 'progress_frames', 'cache_load_ms', 'first_world_ms'
$disqualifying = @('hw_slowdown', 'hw_thermal_slowdown', 'hw_power_brake_slowdown', 'sw_thermal_slowdown')
$smi = Find-NvidiaSmi $null
$rows = [System.Collections.Generic.List[object]]::new()

function Get-UnixNs { return [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() * 1000000L }
function Get-Temperature {
    if (-not $smi) { return '' }
    return (& $smi --query-gpu=temperature.gpu --format=csv,noheader,nounits | Select-Object -First 1)
}

function Invoke-Start([string]$Build, [string]$Mode, [string]$Name, [bool]$Measured) {
    $root = Join-Path $Output 'runs' "$Mode-$Build-$Name"
    $driverCache = Join-Path $Output 'driver-cache' "$Mode-$Build"
    if ($Mode -ne 'driver') { Remove-Item -Recurse -Force $driverCache -ErrorAction SilentlyContinue }
    New-Item -ItemType Directory -Force $root, $driverCache | Out-Null
    $profileDir = Join-Path $root 'profile'
    $arguments = @('--mission-ui-smoke-test', '--profile', $profileDir)
    if ($Mode -eq 'binary') {
        $arguments += @('--shader-cache', 'on')
        # The program-binary cache lives in the profile; keep one per build across its runs.
        $shared = Join-Path $Output 'binary-cache' $Build
        New-Item -ItemType Directory -Force $shared, $profileDir | Out-Null
        $link = if ($IsWindows) { 'Junction' } else { 'SymbolicLink' }
        New-Item -ItemType $link -Path (Join-Path $profileDir 'shader-cache') -Target $shared | Out-Null
    }
    $env:__GL_SHADER_DISK_CACHE = if ($Mode -eq 'driver') { '1' } else { '0' }
    $env:__GL_SHADER_DISK_CACHE_PATH = $driverCache
    $env:__GL_SHADER_DISK_CACHE_SKIP_CLEANUP = '1'
    $env:MESA_SHADER_CACHE_DISABLE = if ($Mode -eq 'driver') { 'false' } else { 'true' }
    $env:MESA_SHADER_CACHE_DIR = $driverCache
    $temperature = Get-Temperature
    $startNs = Get-UnixNs
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    & $builds[$Build] @arguments *> (Join-Path $root 'stdout.txt')
    $exit = $LASTEXITCODE
    $watch.Stop()
    $endNs = Get-UnixNs
    if (-not $Measured) { return }
    $row = [ordered]@{ mode = $Mode; build = $Build; run = $Name; exit = $exit; gpu_temp_c = $temperature
        process_s = [math]::Round($watch.Elapsed.TotalSeconds, 3); start_utc_unix_ns = $startNs; end_utc_unix_ns = $endNs }
    $startup = Join-Path $profileDir 'gpu-startup.txt'
    $values = @{}
    if (Test-Path $startup) {
        foreach ($line in Get-Content $startup) { $key, $value = $line -split '=', 2; $values[$key] = $value }
    }
    foreach ($field in $fields) { $row[$field] = $values[$field] }
    $rows.Add([pscustomobject]$row)
    Write-Host ("{0,-6} {1} {2}: first_world_ms={3} compile_ms={4} progress_gap_ms={5} exit={6} temp={7}" -f $Mode,
        $Build, $Name, $row.first_world_ms, $row.compile_ms, $row.progress_gap_ms, $exit, $temperature)
}

$telemetryPath = Join-Path $Output 'telemetry.csv'
$telemetry = if ($smi) { Start-GpuTelemetry -Path $telemetryPath -NvidiaSmi $smi -IntervalMs $TelemetryIntervalMs } else { $null }
$idleTemperature = ''
try {
    if ($IdleSeconds -gt 0) {
        Write-Host "Bezczynność $IdleSeconds s przed pierwszym przebiegiem (GPU $(Get-Temperature) °C)..."
        Start-Sleep -Seconds $IdleSeconds
    }
    $idleTemperature = Get-Temperature
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
} finally {
    if ($telemetry) { Stop-GpuTelemetry $telemetry | Out-Null }
    Remove-Item Env:__GL_SHADER_DISK_CACHE, Env:__GL_SHADER_DISK_CACHE_PATH, Env:__GL_SHADER_DISK_CACHE_SKIP_CLEANUP,
        Env:MESA_SHADER_CACHE_DISABLE, Env:MESA_SHADER_CACHE_DIR -ErrorAction SilentlyContinue
}

# Clock-event reasons per run from the telemetry samples inside its process window.
$samples = @(if ($telemetry -and (Test-Path -LiteralPath $telemetryPath)) { Import-GpuTelemetry $telemetryPath })
foreach ($row in $rows) {
    $window = if ($samples.Count) {
        Get-GpuTelemetryWindow -Telemetry $samples -StartNs $row.start_utc_unix_ns -EndNs $row.end_utc_unix_ns
    } else { $null }
    $hasSamples = $window -and $window.Samples
    $row | Add-Member telemetry_samples $(if ($window) { $window.Samples } else { 0 })
    $row | Add-Member gpu_temp_max_c $(if ($hasSamples) { $window.TemperatureCMax } else { '' })
    $row | Add-Member limiting_reasons $(if ($hasSamples) { $window.LimitingReasons } else { '' })
    $row | Add-Member disqualified $([bool]($hasSamples -and @($disqualifying | Where-Object { $window."Reason_$_" -gt 0 }).Count))
}
$csv = Join-Path $Output 'shader-startup.csv'
$rows | Export-Csv -Path $csv

function Get-TrueMedian([double[]]$Values) {
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    $sorted = [double[]]($Values | Sort-Object)
    $middle = [int][math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2) { return $sorted[$middle] }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2
}
$summary = [System.Collections.Generic.List[string]]::new()
$summary.Add("GPU przed pierwszym przebiegiem (po $IdleSeconds s bezczynności): $idleTemperature °C; telemetria: " +
    $(if ($samples.Count) { "$($samples.Count) próbek co $TelemetryIntervalMs ms" } else { 'brak (bez nvidia-smi)' }))
$disqualified = @($rows | Where-Object disqualified)
$summary.Add("Przebiegi zdyskwalifikowane (hw_* albo sw_thermal_slowdown): $($disqualified.Count) z $($rows.Count)" +
    $(if ($disqualified.Count) { ': ' + (($disqualified | ForEach-Object { "$($_.mode) $($_.build) $($_.run)" }) -join ', ') } else { '' }))
$capped = @($rows | Where-Object { $_.limiting_reasons -match 'sw_power_cap' }).Count
$summary.Add("Przebiegi z sw_power_cap (tylko raportowane): $capped z $($rows.Count)")
$summary.Add('Mediana (min–max) bez przebiegów zdyskwalifikowanych; A = baseline, B = candidate:')
$rows | Where-Object { -not $_.disqualified } | Group-Object mode, build | ForEach-Object {
    $line = "  $($_.Name), $($_.Count) przebiegów:"
    foreach ($field in 'first_world_ms', 'compile_ms', 'progress_gap_ms', 'progress_frames') {
        $values = [double[]]@($_.Group | Where-Object { $_.$field } | ForEach-Object { [double]$_.$field })
        if (-not $values.Count) { continue }
        $stats = $values | Measure-Object -Minimum -Maximum
        $line += (" {0}={1:0.#} ({2:0.#}–{3:0.#})" -f $field, (Get-TrueMedian $values), $stats.Minimum, $stats.Maximum)
    }
    $summary.Add($line)
}
$summary | Set-Content -Encoding utf8 (Join-Path $Output 'summary.txt')
$summary | ForEach-Object { Write-Host $_ }
Write-Host "CSV: $csv"
