# Soak: repeated StrefaQA --scene-benchmark passes over the GPU scenes with continuous nvidia-smi telemetry.
# Answers whether (and when) the slow regime of G3-B (x2.7-4.1 physics GPU time) appears and which
# clock event reasons are active while it lasts. Procedure: docs/maintenance/GPU_CHECK.md.
#
#   pwsh scripts/profiling/soak.ps1 -Executable build\release\StrefaQA.exe -Output soak-2026-11-02
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Output,
    [ValidateRange(1, 240)][double]$Minutes = 15,
    [string[]]$Scenes = @('gpu:avalanche', 'gpu:rain', 'gpu:mixed', 'gpu:empty', 'gpu:water-tank', 'gpu:dam-break',
        'gpu:connected-vessels', 'gpu:oil-water', 'gpu:sand-water', 'gpu:hourglass', 'gpu:inverted-oil', 'gpu:oil-drop',
        'mission', 'thermal:fire', 'thermal:oil', 'thermal:quench', 'thermal:phases'),
    [ValidateRange(1, 600)][int]$Seconds = 10,
    [ValidateSet('on', 'off')][string]$Vsync = 'off',
    # A scene-pass is slow when its physics GPU median is at least this multiple of the scene's best pass
    # (the in-game GpuSlowdownMonitor uses 2x; G3-B measured 2.7-4.1x).
    [ValidateRange(1.1, 10)][double]$SlowFactor = 2.0,
    [string]$NvidiaSmi
)
$ErrorActionPreference = 'Stop'
# CSV and report numbers use '.' regardless of the Windows locale.
[Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::InvariantCulture
# "pwsh -File" passes -Scenes a,b as one string; accept comma-separated text as well as an array.
$Scenes = @($Scenes | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim(" '`"") } | Where-Object { $_ })
Import-Module (Join-Path $PSScriptRoot 'GpuTelemetry.psm1') -Force
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $Output) { throw 'Wybierz nowy katalog wyników (-Output).' }
New-Item -ItemType Directory -Path $Output | Out-Null
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Get-UnixNs([DateTimeOffset]$Time) {
    return ($Time.UtcTicks - [DateTimeOffset]::new(1970, 1, 1, 0, 0, 0, [TimeSpan]::Zero).UtcTicks) * 100
}
function Get-Number($Text) {
    if ($null -eq $Text -or $Text -eq '') { return $null }
    return [double]::Parse($Text, $invariant)
}
function Format-Number($Value, [string]$Format = '0.00') {
    if ($null -eq $Value) { return '–' }
    return ([double]$Value).ToString($Format, $invariant)
}

# --- Environment -----------------------------------------------------------------------------------------------
$smi = Find-NvidiaSmi $NvidiaSmi
$gpu = $null
if ($smi) { $gpu = (& $smi --query-gpu=name,driver_version,pcie.link.gen.max --format=csv,noheader 2>$null | Select-Object -First 1) }
$powerScheme = (& powercfg /getactivescheme 2>$null) -join ' '
$battery = Get-CimInstance -ClassName Win32_Battery -ErrorAction SilentlyContinue | Select-Object -First 1
$power = if (-not $battery) { 'brak baterii (zasilacz)' } elseif ($battery.BatteryStatus -eq 2) { 'zasilacz' } else { "bateria ($($battery.EstimatedChargeRemaining)%)" }
$arguments = @('--scene-benchmark', ($Scenes -join ','), '--seconds', [string]$Seconds, '--vsync', $Vsync)
$environment = [ordered]@{
    schema_version = 1; started_utc = [DateTimeOffset]::UtcNow.ToString('o'); minutes = $Minutes; seconds = $Seconds
    vsync = $Vsync; slow_factor = $SlowFactor; scenes = $Scenes; executable = $Executable
    executable_sha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
    arguments = $arguments; gpu = $gpu; nvidia_smi = $smi; power_source = $power; power_scheme = $powerScheme
    os = [Environment]::OSVersion.VersionString; powershell = $PSVersionTable.PSVersion.ToString()
}

# --- Passes ----------------------------------------------------------------------------------------------------
$telemetryPath = Join-Path $Output 'telemetry.csv'
$telemetry = Start-GpuTelemetry -Path $telemetryPath -NvidiaSmi $smi
$environment.telemetry_reasons_source = if ($telemetry) { $telemetry.ReasonsSource } else { $null }
Start-Sleep -Seconds 2 # idle samples before the first window opens
$soakStart = [DateTimeOffset]::UtcNow
$soakStartNs = Get-UnixNs $soakStart
$deadline = $soakStart.AddMinutes($Minutes)
# Generous per-pass limit: warm-up (<= 10 s), measurement, window and world creation per scene.
$passTimeoutMs = [int](1000 * $Scenes.Count * ($Seconds + 40))
$passes = [Collections.Generic.List[object]]::new()
$lastDuration = [TimeSpan]::Zero
try {
    for ($pass = 1; ; ++$pass) {
        $now = [DateTimeOffset]::UtcNow
        if ($pass -gt 1 -and $now + $lastDuration -gt $deadline) { break }
        $dir = Join-Path $Output ('pass-{0:D2}' -f $pass)
        New-Item -ItemType Directory -Path $dir | Out-Null
        $info = [Diagnostics.ProcessStartInfo]::new($Executable)
        foreach ($argument in $arguments + @('--profile', $dir)) { $info.ArgumentList.Add($argument) }
        $info.UseShellExecute = $false
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
        $info.WorkingDirectory = Split-Path -Parent $Executable
        $minute = ($now - $soakStart).TotalMinutes
        Write-Host ("Przebieg {0} (minuta {1:0.0})" -f $pass, $minute)
        $process = [Diagnostics.Process]::Start($info)
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $timedOut = -not $process.WaitForExit($passTimeoutMs)
        if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
        $stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $dir 'stdout.txt') -Encoding utf8
        $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $dir 'stderr.txt') -Encoding utf8
        $exit = $process.ExitCode
        $process.Dispose()
        $lastDuration = [DateTimeOffset]::UtcNow - $now
        $passes.Add([pscustomobject]@{ Pass = $pass; Minute = $minute; ExitCode = $exit; TimedOut = $timedOut
                Seconds = $lastDuration.TotalSeconds; Directory = $dir })
        if ($exit -eq 77) { throw 'StrefaQA: brak OpenGL 4.3 (kod 77); soak wymaga GPU.' }
        if ($timedOut -or $exit -ne 0) { Write-Warning "Przebieg $pass zakończył się kodem $exit$(if($timedOut){' (przekroczony czas)'}); szczegóły w $dir." }
    }
} finally {
    Start-Sleep -Seconds 1
    $environment.telemetry_rows = Stop-GpuTelemetry $telemetry
    $environment.finished_utc = [DateTimeOffset]::UtcNow.ToString('o')
    $environment.passes = $passes
    $environment | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'environment.json') -Encoding utf8
}

# --- Analysis --------------------------------------------------------------------------------------------------
$samples = @(if ($telemetry -and (Test-Path -LiteralPath $telemetryPath)) { Import-GpuTelemetry $telemetryPath })
$rows = [Collections.Generic.List[object]]::new()
foreach ($run in $passes) {
    $csv = Join-Path $run.Directory 'scene-benchmark.csv'
    if (-not (Test-Path -LiteralPath $csv)) { continue }
    foreach ($scene in Import-Csv -LiteralPath $csv) {
        $start = [long]$scene.start_utc_unix_ns
        $end = [long]$scene.end_utc_unix_ns
        $window = Get-GpuTelemetryWindow -Telemetry $samples -StartNs $start -EndNs $end
        $physics = Get-Number $scene.physics_median_ms
        $rows.Add([pscustomobject][ordered]@{
                pass = $run.Pass; scene = $scene.scene; minute = [Math]::Round(($start - $soakStartNs) / 6e10, 2)
                window_start = $start; window_end = $end
                physics_median_ms = $physics; presentation_median_ms = Get-Number $scene.presentation_median_ms
                frame_median_ms = Get-Number $scene.frame_median_ms; gpu_samples = [int]$scene.gpu_samples
                ratio = $null; slow = $false
                telemetry_samples = $window.Samples
                graphics_mhz = if ($window.Samples) { $window.GraphicsMhzMedian } else { $null }
                power_w = if ($window.Samples) { $window.PowerWMedian } else { $null }
                temperature_max_c = if ($window.Samples) { $window.TemperatureCMax } else { $null }
                pstates = if ($window.Samples) { $window.Pstates } else { '' }
                limiting_reasons = if ($window.Samples) { $window.LimitingReasons } else { '' }
                window = $window
            })
    }
}
# Baseline per scene: its best (lowest) physics median in this soak.
foreach ($group in $rows | Where-Object { $_.physics_median_ms -gt 0 } | Group-Object scene) {
    $best = ($group.Group | Measure-Object physics_median_ms -Minimum).Minimum
    foreach ($row in $group.Group) {
        $row.ratio = [Math]::Round($row.physics_median_ms / $best, 3)
        $row.slow = $row.ratio -ge $SlowFactor
    }
}
$reasonNames = @('gpu_idle', 'applications_clocks_setting', 'sw_power_cap', 'hw_slowdown', 'hw_thermal_slowdown',
    'hw_power_brake_slowdown', 'sw_thermal_slowdown', 'sync_boost')
$columns = @('pass', 'scene', 'minute', 'physics_median_ms', 'presentation_median_ms', 'frame_median_ms', 'gpu_samples',
    'ratio', 'slow', 'telemetry_samples', 'graphics_mhz', 'power_w', 'temperature_max_c', 'pstates', 'limiting_reasons')
foreach ($reason in $reasonNames) {
    $columns += @{ n = "reason_$reason"; e = [scriptblock]::Create("if (`$_.telemetry_samples) { `$_.window.Reason_$reason }") }
}
$rows | Select-Object $columns | Export-Csv -LiteralPath (Join-Path $Output 'soak-summary.csv') -NoTypeInformation -Encoding utf8

# Telemetry pooled over all slow vs all other scene windows.
function Get-Pooled([object[]]$Selected) {
    $inside = @(foreach ($row in $Selected) {
            $samples | Where-Object { $_.utc_unix_ns -ge $row.window_start -and $_.utc_unix_ns -le $row.window_end }
        })
    if ($inside.Count -eq 0) { return $null }
    return Get-GpuTelemetryWindow -Telemetry $inside -StartNs ([long]::MinValue) -EndNs ([long]::MaxValue)
}
$slowRows = @($rows | Where-Object slow)
$fastRows = @($rows | Where-Object { -not $_.slow -and $null -ne $_.ratio })
$slowPool = Get-Pooled $slowRows
$fastPool = Get-Pooled $fastRows

# --- Report (Polish) -------------------------------------------------------------------------------------------
$report = [Collections.Generic.List[string]]::new()
$report.Add('# Soak GPU — raport')
$report.Add('')
$report.Add("- GPU: $(if ($gpu) { $gpu } else { 'nieznany (brak nvidia-smi)' })")
$report.Add("- Zasilanie: $power; plan: $($powerScheme.Trim())")
$report.Add("- Czas: $(Format-Number $Minutes '0.#') min, przebiegów: $($passes.Count), scen na przebieg: $($Scenes.Count), pomiar $Seconds s/scenę, VSync $Vsync")
$report.Add("- Telemetria: $(if ($telemetry) { "$($environment.telemetry_rows) próbek co $($telemetry.IntervalMs) ms ($($telemetry.ReasonsSource))" } else { 'brak' })")
$report.Add("- Próg wolnego reżimu: mediana fizyki GPU ≥ $(Format-Number $SlowFactor '0.0') × najlepszy przebieg tej sceny")
$failed = @($passes | Where-Object { $_.ExitCode -ne 0 -or $_.TimedOut })
if ($failed.Count) { $report.Add("- **Nieudane przebiegi:** $(($failed | ForEach-Object Pass) -join ', ') (pomiar niepełny)") }
$report.Add('')
$report.Add('## Wynik')
$report.Add('')
if ($slowRows.Count -eq 0) {
    $maxRatio = ($rows | Where-Object { $null -ne $_.ratio } | Measure-Object ratio -Maximum).Maximum
    $report.Add("Wolny reżim **nie wystąpił** (największy stosunek do najlepszego przebiegu: ×$(Format-Number $maxRatio)).")
} else {
    $first = $slowRows | Sort-Object window_start | Select-Object -First 1
    $ratios = $slowRows | Measure-Object ratio -Minimum -Maximum
    $report.Add("Wolny reżim **wystąpił**: pierwszy raz w minucie $(Format-Number $first.minute '0.0') (przebieg $($first.pass), scena ``$($first.scene)``), " +
        "$($slowRows.Count) z $($rows.Count) pomiarów scen, ×$(Format-Number $ratios.Minimum)–$(Format-Number $ratios.Maximum) względem najlepszego przebiegu.")
}
$report.Add('')
$report.Add('## Powody ograniczenia zegara')
$report.Add('')
$report.Add('| Okna | Próbki | Zegar GPU (MHz) | Moc (W) | Limit (W) | Temp. max (°C) | P-stan | Aktywne powody (udział próbek) |')
$report.Add('| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |')
foreach ($entry in @(@('wolne', $slowPool), @('pozostałe', $fastPool))) {
    $pool = $entry[1]
    if (-not $pool) { $report.Add("| $($entry[0]) | 0 | – | – | – | – | – | – |"); continue }
    $active = @(foreach ($reason in $reasonNames) {
            $share = $pool."Reason_$reason"
            if ($share -gt 0) { "$reason $(Format-Number (100 * $share) '0')%" }
        }) -join ', '
    $report.Add("| $($entry[0]) | $($pool.Samples) | $(Format-Number $pool.GraphicsMhzMedian '0') | $(Format-Number $pool.PowerWMedian '0.0') | $(Format-Number $pool.PowerLimitW '0') | $(Format-Number $pool.TemperatureCMax '0') | $($pool.Pstates) | $(if ($active) { $active } else { 'brak' }) |")
}
$report.Add('')
$report.Add('## Przebiegi (tabela do opisu PR)')
$report.Add('')
$report.Add('| Przebieg | Minuta | Wolne sceny | Mediana ×najlepszy | Zegar GPU (MHz) | Moc (W) | Temp. max (°C) | Powody ograniczenia |')
$report.Add('| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |')
$passRows = @($passes | Select-Object -First 10)
foreach ($run in $passRows) {
    $inPass = @($rows | Where-Object pass -eq $run.Pass)
    $pool = Get-Pooled $inPass
    $ratios = [double[]]@($inPass | Where-Object { $null -ne $_.ratio } | ForEach-Object ratio | Sort-Object)
    $median = if ($ratios.Count) { $ratios[[Math]::Min($ratios.Count - 1, [int][Math]::Floor($ratios.Count / 2))] } else { $null }
    $reasons = if ($pool -and $pool.LimitingReasons) { $pool.LimitingReasons } elseif ($pool) { 'brak' } else { '–' }
    $report.Add("| $($run.Pass) | $(Format-Number $run.Minute '0.0') | $(@($inPass | Where-Object slow).Count)/$($inPass.Count) | ×$(Format-Number $median) | $(if ($pool) { Format-Number $pool.GraphicsMhzMedian '0' } else { '–' }) | $(if ($pool) { Format-Number $pool.PowerWMedian '0.0' } else { '–' }) | $(if ($pool) { Format-Number $pool.TemperatureCMax '0' } else { '–' }) | $reasons |")
}
if ($passes.Count -gt 10) { $report.Add(''); $report.Add("Pokazano 10 z $($passes.Count) przebiegów; pełne dane w soak-summary.csv.") }
$report.Add('')
$report.Add('Surowe dane (telemetry.csv, pass-*/, soak-summary.csv) trafiają do repozytorium strefa-zero-measurements, nie do tego repozytorium.')
$report | Set-Content -LiteralPath (Join-Path $Output 'soak-report.md') -Encoding utf8
Write-Host ''
$report | ForEach-Object { Write-Host $_ }
