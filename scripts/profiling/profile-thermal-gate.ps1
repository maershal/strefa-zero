# ABBA measurement of the exact gas-stage gate: StrefaQA --gpu-check thermal-gate with GPU telemetry every 100 ms,
# run-spec.json (power, power plan, GPU temperature at start) and analyze-thermal-gate.py. Windows only (Win32_Battery,
# powercfg); on Linux run StrefaQA --gpu-check thermal-gate --profile <dir> directly.
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Output,
    [ValidateSet('full', 'quick')][string]$Plan = 'full',
    [Nullable[uint64]]$Seed,
    [ValidateRange(1, 600)][int]$TimeoutMinutes = 180
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'profile-thermal-gate.ps1 działa tylko na Windows (Win32_Battery, powercfg); na Linuksie uruchom StrefaQA --gpu-check thermal-gate bezpośrednio.' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $Output) { throw 'Wybierz nowy katalog; wcześniejsze wyniki zostają nietknięte.' }
[IO.Directory]::CreateDirectory($Output) | Out-Null
Import-Module (Join-Path $PSScriptRoot 'GpuTelemetry.psm1')
$smi = Find-NvidiaSmi $null
$gpu = if ($smi) { (& $smi --query-gpu=name,driver_version,temperature.gpu,clocks.gr,power.draw --format=csv,noheader) -join ';' } else { $null }
$battery = Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue | Select-Object -First 1
$powerPlan = (powercfg /getactivescheme) -join ' '
[ordered]@{
    stage = 'thermal-gate'; executable = $Executable; sha256 = (Get-FileHash $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
    flags = @('--gpu-check', 'thermal-gate'); plan = $Plan; seed = $Seed
    started_utc = [DateTime]::UtcNow.ToString('o'); gpu_at_start = $gpu; power_plan = $powerPlan
    on_ac_power = if ($battery) { $battery.BatteryStatus -eq 2 } else { $null }; battery_percent = if ($battery) { $battery.EstimatedChargeRemaining } else { $null }
} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $Output 'run-spec.json')
$sampler = Start-GpuTelemetry -Path (Join-Path $Output 'telemetry.csv') -IntervalMs 100
$env:STREFA_THERMAL_GATE_PLAN = $Plan
if ($null -ne $Seed) { $env:STREFA_BENCHMARK_SEED = "$Seed" }
try {
    $p = Start-Process -FilePath $Executable -ArgumentList @('--gpu-check', 'thermal-gate', '--profile', ('"' + $Output + '"')) -PassThru
    $null = $p.Handle
    $done = $p.WaitForExit($TimeoutMinutes * 60000)
    if (-not $done) { $p.Kill(); $p.WaitForExit() }
} finally {
    Remove-Item Env:STREFA_THERMAL_GATE_PLAN -ErrorAction SilentlyContinue
    if ($null -ne $Seed) { Remove-Item Env:STREFA_BENCHMARK_SEED -ErrorAction SilentlyContinue }
    if ($sampler) { $null = Stop-GpuTelemetry $sampler }
}
$p.Refresh(); $code = $p.ExitCode
if ($null -eq $code) { throw 'Brak kodu wyjścia procesu StrefaQA.' }
[ordered]@{ exit_code = $code; timed_out = (-not $done); finished_utc = [DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $Output 'run-result.json')
if (-not $done -or $code -ne 0) { throw 'Pomiar bramki gazu nie powiódł się; sprawdź zachowane pliki.' }
& python (Join-Path $PSScriptRoot 'analyze-thermal-gate.py') $Output
if ($LASTEXITCODE -ne 0) { throw 'analyze-thermal-gate.py zakończył się błędem.' }
Write-Host "thermal-gate $Plan : PASS"
