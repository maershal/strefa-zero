# Z01: StrefaQA --gpu-check cost-model with GPU telemetry, then analyze-cost-model.py on the result.
#   pwsh scripts/profiling/profile-cost-model.ps1 -Executable build/release/StrefaQA.exe -Output <new dir> [-Plan full]
# -Plan full (default): synthetic passes + particle sweep, 3 blocks in random order; with the cool-down gate of the
#  tool (waits until the CPU probe recovers) it takes about an hour on the GTX 1650 laptop.
# -Plan part1: synthetic passes only; run it once with the NVIDIA driver option "Threaded optimization" set to Auto and
#  once set to Off (NVIDIA Control Panel > Manage 3D settings > Program settings > StrefaQA.exe), and pass the setting
#  as -ThreadedOptimization so the run records it. The tool cannot read or change the driver option.
# -Seed repeats the order of an earlier run (STREFA_BENCHMARK_SEED). The run records AC power, the power plan and the
# starting GPU temperature; the protocol in CLAUDE.md asks for AC power and a GPU idle for >= 5 min before the start.
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Output,
    [ValidateSet('full', 'part1', 'quick')][string]$Plan = 'full',
    [ValidateSet('auto', 'off', 'unknown')][string]$ThreadedOptimization = 'unknown',
    [Nullable[uint64]]$Seed,
    [ValidateRange(1, 600)][int]$TimeoutMinutes = 300
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $Output) { throw 'Choose a new directory; earlier results are preserved.' }
[IO.Directory]::CreateDirectory($Output) | Out-Null
Import-Module (Join-Path $PSScriptRoot 'GpuTelemetry.psm1')
$smi = Find-NvidiaSmi $null
$gpu = if ($smi) { (& $smi --query-gpu=name,driver_version,temperature.gpu,clocks.gr,power.draw --format=csv,noheader) -join ';' } else { $null }
$battery = Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue | Select-Object -First 1
$powerPlan = (powercfg /getactivescheme) -join ' '
[ordered]@{
    stage = 'Z01'; executable = $Executable; sha256 = (Get-FileHash $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
    flags = @('--gpu-check', 'cost-model'); plan = $Plan; threaded_optimization = $ThreadedOptimization; seed = $Seed
    started_utc = [DateTime]::UtcNow.ToString('o'); gpu_at_start = $gpu; power_plan = $powerPlan
    on_ac_power = if ($battery) { $battery.BatteryStatus -eq 2 } else { $null }; battery_percent = if ($battery) { $battery.EstimatedChargeRemaining } else { $null }
} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $Output 'run-spec.json')
$sampler = Start-GpuTelemetry -Path (Join-Path $Output 'telemetry.csv') -IntervalMs 100
$env:STREFA_COST_MODEL_PLAN = $Plan
if ($null -ne $Seed) { $env:STREFA_BENCHMARK_SEED = "$Seed" }
try {
    $p = Start-Process -FilePath $Executable -ArgumentList @('--gpu-check', 'cost-model', '--profile', ('"' + $Output + '"')) -PassThru
    $null = $p.Handle
    $done = $p.WaitForExit($TimeoutMinutes * 60000)
    if (-not $done) { $p.Kill(); $p.WaitForExit() }
} finally {
    Remove-Item Env:STREFA_COST_MODEL_PLAN -ErrorAction SilentlyContinue
    if ($null -ne $Seed) { Remove-Item Env:STREFA_BENCHMARK_SEED -ErrorAction SilentlyContinue }
    if ($sampler) { $null = Stop-GpuTelemetry $sampler }
}
$p.Refresh(); $code = $p.ExitCode
if ($null -eq $code) { throw 'Native process exit status unavailable' }
[ordered]@{ exit_code = $code; timed_out = (-not $done); finished_utc = [DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $Output 'run-result.json')
if (-not $done -or $code -ne 0) { throw 'Cost model run failed; inspect the preserved files.' }
$python = if ($IsWindows) { 'python' } else { 'python3' } # python3.exe on Windows is often the Store stub
& $python (Join-Path $PSScriptRoot 'analyze-cost-model.py') $Output
if ($LASTEXITCODE -ne 0) { throw 'analyze-cost-model.py failed' }
Write-Host "cost-model $Plan : PASS"
