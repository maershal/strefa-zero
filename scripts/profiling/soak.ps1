#Requires -Version 7.2
# Soak: repeated StrefaQA --scene-benchmark passes over the GPU scenes with continuous nvidia-smi telemetry.
# Answers whether (and when) the slow regime of G3-B (x2.7-4.1 physics GPU time) appears and which
# clock event reasons are active while it lasts. Procedure: docs/maintenance/GPU_CHECK.md.
#
#   pwsh scripts/profiling/soak.ps1 -Executable build\release\StrefaQA.exe -Output soak-2026-11-02
#   pwsh scripts/profiling/soak.ps1 ... -SingleProcess -Baseline cooled\scene-benchmark.csv
#
# Three independent criteria flag a scene measurement:
#   relative   physics GPU median >= SlowFactor x the best pass of that scene in this soak;
#   absolute   physics GPU median >= SlowFactor x the scene's median in -Baseline (a cooled reference run);
#   telemetry  most samples not in P0, or the memory clock median below 90% of the session maximum.
# Windows with hw_* or sw_thermal_slowdown active are marked "disqualified" (CLAUDE.md, measurement protocol);
# sw_power_cap is reported only, because a laptop GPU under load sits at its power limit.
# -SingleProcess runs every pass in one StrefaQA process (--loop-minutes), so a process restart (fresh driver
# state, idle gap) cannot end a slow regime between passes.
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Output,
    [ValidateRange(1, 240)][double]$Minutes = 15,
    [string[]]$Scenes = @('gpu:avalanche', 'gpu:rain', 'gpu:mixed', 'gpu:empty', 'gpu:water-tank', 'gpu:dam-break',
        'gpu:connected-vessels', 'gpu:oil-water', 'gpu:sand-water', 'gpu:hourglass', 'gpu:inverted-oil', 'gpu:oil-drop',
        'mission', 'thermal:fire', 'thermal:oil', 'thermal:quench', 'thermal:phases'),
    [ValidateRange(1, 600)][int]$Seconds = 10,
    [ValidateSet('on', 'off')][string]$Vsync = 'off',
    # The in-game GpuSlowdownMonitor uses 2x; G3-B measured 2.7-4.1x.
    [ValidateRange(1.1, 10)][double]$SlowFactor = 2.0,
    # scene-benchmark.csv of a reference run (cooled GPU, same build and settings); enables the absolute criterion.
    [string]$Baseline,
    [switch]$SingleProcess,
    [string]$NvidiaSmi
)
$ErrorActionPreference = 'Stop'
# CSV and report numbers use '.' regardless of the Windows locale; StrefaQA writes UTF-8.
[Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::InvariantCulture
$utf8 = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
# "pwsh -File" passes -Scenes a,b as one string; accept comma-separated text as well as an array.
$Scenes = @($Scenes | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim(" '`"") } | Where-Object { $_ })
Import-Module (Join-Path $PSScriptRoot 'GpuTelemetry.psm1') -Force
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $Output) { throw 'Wybierz nowy katalog wyników (-Output).' }
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
function Get-MedianValue([double[]]$Values) {
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    $sorted = [double[]]($Values | Sort-Object)
    return $sorted[[Math]::Min($sorted.Count - 1, [int][Math]::Floor($sorted.Count / 2))]
}

# Absolute reference: median physics GPU time per scene over the ok rows of the baseline run.
$baselineMs = @{}
if ($Baseline) {
    $Baseline = (Resolve-Path -LiteralPath $Baseline).Path
    foreach ($group in Import-Csv -LiteralPath $Baseline | Where-Object {
            (-not $_.PSObject.Properties['status'] -or $_.status -eq 'ok') -and (Get-Number $_.physics_median_ms) -gt 0
        } | Group-Object scene) {
        $baselineMs[$group.Name] = Get-MedianValue @($group.Group | ForEach-Object { Get-Number $_.physics_median_ms })
    }
    $missing = @($Scenes | Where-Object { -not $baselineMs.ContainsKey($_) })
    if ($missing.Count) { Write-Warning "Baza bez scen: $($missing -join ', '); dla nich tylko kryterium względne." }
}
New-Item -ItemType Directory -Path $Output | Out-Null

# --- Environment -----------------------------------------------------------------------------------------------
$smi = Find-NvidiaSmi $NvidiaSmi
$gpu = $null
if ($smi) { $gpu = (& $smi --query-gpu=name,driver_version,pcie.link.gen.max --format=csv,noheader 2>$null | Select-Object -First 1) }
$powerScheme = (& powercfg /getactivescheme 2>$null) -join ' '
$battery = Get-CimInstance -ClassName Win32_Battery -ErrorAction SilentlyContinue | Select-Object -First 1
$power = if (-not $battery) { 'brak baterii (zasilacz)' } elseif ($battery.BatteryStatus -eq 2) { 'zasilacz' } else { "bateria ($($battery.EstimatedChargeRemaining)%)" }
$arguments = @('--scene-benchmark', ($Scenes -join ','), '--seconds', [string]$Seconds, '--vsync', $Vsync)
if ($SingleProcess) { $arguments += @('--loop-minutes', [string][int][Math]::Max(1, [Math]::Ceiling($Minutes))) }
$environment = [ordered]@{
    schema_version = 2; started_utc = [DateTimeOffset]::UtcNow.ToString('o'); minutes = $Minutes; seconds = $Seconds
    vsync = $Vsync; slow_factor = $SlowFactor; single_process = [bool]$SingleProcess; baseline = $Baseline
    scenes = $Scenes; executable = $Executable
    executable_sha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
    arguments = $arguments; gpu = $gpu; nvidia_smi = $smi; power_source = $power; power_scheme = $powerScheme
    os = [Environment]::OSVersion.VersionString; powershell = $PSVersionTable.PSVersion.ToString()
}

# --- Runs ------------------------------------------------------------------------------------------------------
# One run = one StrefaQA process: a single pass (default) or every pass (-SingleProcess).
function Invoke-Benchmark([string]$Directory, [int]$TimeoutMs) {
    New-Item -ItemType Directory -Path $Directory | Out-Null
    $info = [Diagnostics.ProcessStartInfo]::new($Executable)
    foreach ($argument in $arguments + @('--profile', $Directory)) { $info.ArgumentList.Add($argument) }
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = $utf8
    $info.StandardErrorEncoding = $utf8
    $info.WorkingDirectory = Split-Path -Parent $Executable
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($TimeoutMs)
    if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
    $stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $Directory 'stdout.txt') -Encoding utf8
    $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $Directory 'stderr.txt') -Encoding utf8
    $exit = $process.ExitCode
    $process.Dispose()
    if ($exit -eq 77) { throw 'StrefaQA: brak OpenGL 4.3 (kod 77); soak wymaga GPU.' }
    # Exit code 1 with complete CSV rows means some scene is invalid or failed; its status column says which.
    if ($timedOut -or $exit -ne 0) { Write-Warning "StrefaQA zakończył się kodem $exit$(if($timedOut){' (przekroczony czas)'}); szczegóły w $Directory." }
    return [pscustomobject]@{ ExitCode = $exit; TimedOut = $timedOut }
}

$telemetryPath = Join-Path $Output 'telemetry.csv'
$telemetry = Start-GpuTelemetry -Path $telemetryPath -NvidiaSmi $smi
$environment.telemetry_reasons_source = if ($telemetry) { $telemetry.ReasonsSource } else { $null }
Start-Sleep -Seconds 2 # idle samples before the first window opens
$soakStart = [DateTimeOffset]::UtcNow
$soakStartNs = Get-UnixNs $soakStart
$deadline = $soakStart.AddMinutes($Minutes)
# Generous per-pass limit: warm-up (<= 10 s), measurement, window and world creation per scene.
$passTimeoutMs = [int](1000 * $Scenes.Count * ($Seconds + 40))
$runs = [Collections.Generic.List[object]]::new()
try {
    if ($SingleProcess) {
        Write-Host ("Jeden proces, pętla {0} min" -f $Minutes)
        $dir = Join-Path $Output 'run'
        $result = Invoke-Benchmark $dir ([int](60000 * [Math]::Ceiling($Minutes)) + $passTimeoutMs)
        $runs.Add([pscustomobject]@{ Run = 1; Pass = $null; Minute = 0.0; ExitCode = $result.ExitCode
                TimedOut = $result.TimedOut; Seconds = ([DateTimeOffset]::UtcNow - $soakStart).TotalSeconds; Directory = $dir })
    } else {
        $lastDuration = [TimeSpan]::Zero
        for ($pass = 1; ; ++$pass) {
            $now = [DateTimeOffset]::UtcNow
            if ($pass -gt 1 -and $now + $lastDuration -gt $deadline) { break }
            $minute = ($now - $soakStart).TotalMinutes
            Write-Host ("Przebieg {0} (minuta {1:0.0})" -f $pass, $minute)
            $dir = Join-Path $Output ('pass-{0:D2}' -f $pass)
            $result = Invoke-Benchmark $dir $passTimeoutMs
            $lastDuration = [DateTimeOffset]::UtcNow - $now
            $runs.Add([pscustomobject]@{ Run = $pass; Pass = $pass; Minute = $minute; ExitCode = $result.ExitCode
                    TimedOut = $result.TimedOut; Seconds = $lastDuration.TotalSeconds; Directory = $dir })
        }
    }
} finally {
    Start-Sleep -Seconds 1
    $environment.telemetry_rows = Stop-GpuTelemetry $telemetry
    $environment.finished_utc = [DateTimeOffset]::UtcNow.ToString('o')
    $environment.runs = $runs
    $environment | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'environment.json') -Encoding utf8
}

# --- Analysis --------------------------------------------------------------------------------------------------
$samples = @(if ($telemetry -and (Test-Path -LiteralPath $telemetryPath)) { Import-GpuTelemetry $telemetryPath })
$sessionMemoryMax = if ($samples.Count) { ($samples | Where-Object { $null -ne $_.memory_mhz } | Measure-Object memory_mhz -Maximum).Maximum } else { $null }
$disqualifyingReasons = @('hw_slowdown', 'hw_thermal_slowdown', 'hw_power_brake_slowdown', 'sw_thermal_slowdown')
$rows = [Collections.Generic.List[object]]::new()
foreach ($run in $runs) {
    $csv = Join-Path $run.Directory 'scene-benchmark.csv'
    if (-not (Test-Path -LiteralPath $csv)) { continue }
    foreach ($scene in Import-Csv -LiteralPath $csv) {
        $start = [long]$scene.start_utc_unix_ns
        $end = [long]$scene.end_utc_unix_ns
        $window = Get-GpuTelemetryWindow -Telemetry $samples -StartNs $start -EndNs $end
        $physics = Get-Number $scene.physics_median_ms
        $reference = if ($baselineMs.ContainsKey($scene.scene)) { $baselineMs[$scene.scene] } else { $null }
        $telemetrySlow = $false
        $disqualified = $false
        if ($window.Samples) {
            $telemetrySlow = $window.NonP0Share -ge 0.5 -or ($null -ne $sessionMemoryMax -and $window.MemoryMhzMedian -lt 0.9 * $sessionMemoryMax)
            $disqualified = @($disqualifyingReasons | Where-Object { $window."Reason_$_" -gt 0 }).Count -gt 0
        }
        $rows.Add([pscustomobject][ordered]@{
                pass = if ($null -ne $run.Pass) { $run.Pass } else { [int]$scene.pass }
                scene = $scene.scene; status = $scene.status
                minute = if ($start -gt 0) { [Math]::Round(($start - $soakStartNs) / 6e10, 2) } else { $null }
                window_start = $start; window_end = $end
                physics_median_ms = $physics; presentation_median_ms = Get-Number $scene.presentation_median_ms
                frame_median_ms = Get-Number $scene.frame_median_ms; physics_samples = [int]$scene.physics_samples
                budget_60fps = Get-Number $scene.budget_60fps
                ratio = $null; slow_relative = $false
                baseline_ms = $reference; baseline_ratio = $null; slow_absolute = $false
                slow_telemetry = $telemetrySlow; slow = $false; disqualified = $disqualified
                telemetry_samples = $window.Samples
                graphics_mhz = if ($window.Samples) { $window.GraphicsMhzMedian } else { $null }
                memory_mhz = if ($window.Samples) { $window.MemoryMhzMedian } else { $null }
                non_p0_share = if ($window.Samples) { $window.NonP0Share } else { $null }
                power_w = if ($window.Samples) { $window.PowerWMedian } else { $null }
                temperature_max_c = if ($window.Samples) { $window.TemperatureCMax } else { $null }
                pstates = if ($window.Samples) { $window.Pstates } else { '' }
                limiting_reasons = if ($window.Samples) { $window.LimitingReasons } else { '' }
                window = $window
            })
    }
}
# Only valid measurements (status ok) enter the performance criteria.
$valid = @($rows | Where-Object { $_.status -eq 'ok' -and $_.physics_median_ms -gt 0 })
foreach ($group in $valid | Group-Object scene) {
    $best = ($group.Group | Measure-Object physics_median_ms -Minimum).Minimum
    foreach ($row in $group.Group) {
        $row.ratio = [Math]::Round($row.physics_median_ms / $best, 3)
        $row.slow_relative = $row.ratio -ge $SlowFactor
        if ($row.baseline_ms) {
            $row.baseline_ratio = [Math]::Round($row.physics_median_ms / $row.baseline_ms, 3)
            $row.slow_absolute = $row.baseline_ratio -ge $SlowFactor
        }
        $row.slow = $row.slow_relative -or $row.slow_absolute
    }
}
$reasonNames = @('gpu_idle', 'applications_clocks_setting', 'sw_power_cap', 'hw_slowdown', 'hw_thermal_slowdown',
    'hw_power_brake_slowdown', 'sw_thermal_slowdown', 'sync_boost')
$columns = @('pass', 'scene', 'status', 'minute', 'physics_median_ms', 'presentation_median_ms', 'frame_median_ms',
    'physics_samples', 'budget_60fps', 'ratio', 'slow_relative', 'baseline_ms', 'baseline_ratio', 'slow_absolute',
    'slow_telemetry', 'slow', 'disqualified', 'telemetry_samples', 'graphics_mhz', 'memory_mhz', 'non_p0_share', 'power_w',
    'temperature_max_c', 'pstates', 'limiting_reasons')
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
$fastRows = @($valid | Where-Object { -not $_.slow })
$slowPool = Get-Pooled $slowRows
$fastPool = Get-Pooled $fastRows
function Get-FirstLine([string]$Name, [object[]]$Flagged, [string]$RatioProperty) {
    if ($Flagged.Count -eq 0) { return "- ${Name}: **nie wystąpił**." }
    $first = $Flagged | Sort-Object window_start | Select-Object -First 1
    $text = "- ${Name}: **wystąpił** — pierwszy raz w minucie $(Format-Number $first.minute '0.0') (przebieg $($first.pass), scena ``$($first.scene)``), $($Flagged.Count) z $($rows.Count) pomiarów scen"
    if ($RatioProperty) {
        $ratios = $Flagged | Measure-Object $RatioProperty -Minimum -Maximum
        $text += ", ×$(Format-Number $ratios.Minimum)–$(Format-Number $ratios.Maximum)"
    }
    return "$text."
}

# --- Report (Polish) -------------------------------------------------------------------------------------------
$passNumbers = @($rows | ForEach-Object pass | Sort-Object -Unique)
$report = [Collections.Generic.List[string]]::new()
$report.Add('# Soak GPU — raport')
$report.Add('')
$report.Add("- GPU: $(if ($gpu) { $gpu } else { 'nieznany (brak nvidia-smi)' })")
$report.Add("- Zasilanie: $power; plan: $($powerScheme.Trim())")
$report.Add("- Czas: $(Format-Number $Minutes '0.#') min, przebiegów: $($passNumbers.Count), scen na przebieg: $($Scenes.Count), pomiar $Seconds s/scenę, VSync $Vsync, $(if ($SingleProcess) { 'jeden proces (--loop-minutes)' } else { 'proces na przebieg' })")
$report.Add("- Telemetria: $(if ($telemetry) { "$($environment.telemetry_rows) próbek co $($telemetry.IntervalMs) ms ($($telemetry.ReasonsSource)), maks. zegar pamięci w sesji $(Format-Number $sessionMemoryMax '0') MHz" } else { 'brak' })")
$report.Add("- Baza bezwzględna: $(if ($Baseline) { "$Baseline ($($baselineMs.Count) scen)" } else { 'brak (-Baseline)' })")
$invalid = @($rows | Where-Object { $_.status -ne 'ok' })
if ($invalid.Count) { $report.Add("- **Pomiary nieważne lub nieudane:** $($invalid.Count) (kolumna status; pominięte w kryteriach wydajności)") }
$failed = @($runs | Where-Object { $_.TimedOut -or ($_.ExitCode -ne 0 -and $_.ExitCode -ne 1) })
if ($failed.Count) { $report.Add("- **Przerwane uruchomienia StrefaQA:** $($failed.Count) (pomiar niepełny)") }
$report.Add('')
$report.Add('## Wynik')
$report.Add('')
$report.Add((Get-FirstLine "Względnie (mediana fizyki GPU ≥ $(Format-Number $SlowFactor '0.0') × najlepszy przebieg sceny)" @($rows | Where-Object slow_relative) 'ratio'))
if ($Baseline) {
    $report.Add((Get-FirstLine "Bezwzględnie (≥ $(Format-Number $SlowFactor '0.0') × baza)" @($rows | Where-Object slow_absolute) 'baseline_ratio'))
} else {
    $report.Add('- Bezwzględnie: nie sprawdzono (brak -Baseline).')
}
$report.Add((Get-FirstLine 'Telemetrycznie (większość próbek poza P0 albo zegar pamięci poniżej 90% maksimum sesji)' @($rows | Where-Object slow_telemetry) $null))
$disqualifiedRows = @($rows | Where-Object disqualified)
$report.Add("- Okna dyskwalifikujące (aktywne hw_* albo sw_thermal_slowdown): $($disqualifiedRows.Count) z $($rows.Count); nie nadają się do porównań A/B (protokół pomiaru w CLAUDE.md). sw_power_cap jest tylko raportowany.")
$report.Add('')
$report.Add('Kryterium telemetryczne oznacza też lekkie sceny, w których sterownik sam obniża P-stan; rozstrzyga zestawienie z kryteriami wydajności.')
$report.Add('')
$report.Add('## Powody ograniczenia zegara')
$report.Add('')
$report.Add('| Okna | Próbki | Zegar GPU (MHz) | Zegar pamięci (MHz) | Moc (W) | Limit (W) | Temp. max (°C) | P-stan | Aktywne powody (udział próbek) |')
$report.Add('| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |')
foreach ($entry in @(@('wolne', $slowPool), @('pozostałe', $fastPool))) {
    $pool = $entry[1]
    if (-not $pool) { $report.Add("| $($entry[0]) | 0 | – | – | – | – | – | – | – |"); continue }
    $active = @(foreach ($reason in $reasonNames) {
            $share = $pool."Reason_$reason"
            if ($share -gt 0) { "$reason $(Format-Number (100 * $share) '0')%" }
        }) -join ', '
    $report.Add("| $($entry[0]) | $($pool.Samples) | $(Format-Number $pool.GraphicsMhzMedian '0') | $(Format-Number $pool.MemoryMhzMedian '0') | $(Format-Number $pool.PowerWMedian '0.0') | $(Format-Number $pool.PowerLimitW '0') | $(Format-Number $pool.TemperatureCMax '0') | $($pool.Pstates) | $(if ($active) { $active } else { 'brak' }) |")
}
$report.Add('')
$report.Add('## Przebiegi (tabela do opisu PR)')
$report.Add('')
$report.Add('| Przebieg | Minuta | Wolne sceny | Mediana ×najlepszy | Mediana ×baza | Zegar GPU (MHz) | Moc (W) | Temp. max (°C) | Powody ograniczenia |')
$report.Add('| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |')
foreach ($pass in @($passNumbers | Select-Object -First 10)) {
    $inPass = @($rows | Where-Object pass -eq $pass)
    $pool = Get-Pooled $inPass
    $median = Get-MedianValue @($inPass | Where-Object { $null -ne $_.ratio } | ForEach-Object ratio)
    $baseMedian = Get-MedianValue @($inPass | Where-Object { $null -ne $_.baseline_ratio } | ForEach-Object baseline_ratio)
    $minute = ($inPass | Where-Object { $null -ne $_.minute } | Measure-Object minute -Minimum).Minimum
    $reasons = if ($pool -and $pool.LimitingReasons) { $pool.LimitingReasons } elseif ($pool) { 'brak' } else { '–' }
    $report.Add("| $pass | $(Format-Number $minute '0.0') | $(@($inPass | Where-Object slow).Count)/$($inPass.Count) | ×$(Format-Number $median) | $(if ($null -ne $baseMedian) { '×' + (Format-Number $baseMedian) } else { '–' }) | $(if ($pool) { Format-Number $pool.GraphicsMhzMedian '0' } else { '–' }) | $(if ($pool) { Format-Number $pool.PowerWMedian '0.0' } else { '–' }) | $(if ($pool) { Format-Number $pool.TemperatureCMax '0' } else { '–' }) | $reasons |")
}
if ($passNumbers.Count -gt 10) { $report.Add(''); $report.Add("Pokazano 10 z $($passNumbers.Count) przebiegów; pełne dane w soak-summary.csv.") }
$report.Add('')
$report.Add('Surowe dane (telemetry.csv, pass-*/ lub run/, soak-summary.csv) trafiają do repozytorium strefa-zero-measurements, nie do tego repozytorium.')
$report | Set-Content -LiteralPath (Join-Path $Output 'soak-report.md') -Encoding utf8
Write-Host ''
$report | ForEach-Object { Write-Host $_ }
