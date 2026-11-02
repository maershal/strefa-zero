#Requires -Version 7.2
# GPU telemetry for the Windows profiling scripts (NVIDIA, nvidia-smi).
#
#   Import-Module (Join-Path $PSScriptRoot 'GpuTelemetry.psm1')
#   $telemetry = Start-GpuTelemetry -Path out\telemetry.csv
#   ... run StrefaQA ...
#   Stop-GpuTelemetry $telemetry
#   $rows = Import-GpuTelemetry out\telemetry.csv
#   Get-GpuTelemetryWindow -Telemetry $rows -StartNs $row.start_utc_unix_ns -EndNs $row.end_utc_unix_ns
#
# utc_unix_ns is the sample time (nvidia-smi's local timestamp converted to UTC, ms resolution) in the same
# epoch and unit as the utc_unix_ns columns written by StrefaQA --scene-benchmark (C++ system_clock), so rows
# join by range. received_utc_unix_ns is when the line reached this process; smi_timestamp is kept verbatim.
Set-StrictMode -Version 3.0

$script:Reasons = @('gpu_idle', 'applications_clocks_setting', 'sw_power_cap', 'hw_slowdown', 'hw_thermal_slowdown',
    'hw_power_brake_slowdown', 'sw_thermal_slowdown', 'sync_boost')
$script:Header = 'utc_unix_ns,utc_iso,received_utc_unix_ns,smi_timestamp,pstate,graphics_mhz,memory_mhz,power_w,power_limit_w,temperature_c,' +
    'reasons_active_mask,' + (($script:Reasons | ForEach-Object { "reason_$_" }) -join ',') + ',reasons_source'

if (-not ('StrefaTelemetry.Sampler' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;

namespace StrefaTelemetry {
// Owns one "nvidia-smi --loop-ms" process; each stdout line becomes one normalised CSV row.
public sealed class Sampler : IDisposable {
    private readonly Process process_;
    private readonly StreamWriter writer_;
    private readonly string source_;
    private readonly int fields_;
    private readonly object gate_ = new object();
    private readonly StringBuilder errors_ = new StringBuilder();
    public int Rows { get; private set; }
    public int Rejected { get; private set; }
    public string Errors { get { lock (gate_) return errors_.ToString(); } }

    public Sampler(string smi, string query, int intervalMs, string path, string header, string source, int fields) {
        source_ = source;
        fields_ = fields;
        writer_ = new StreamWriter(path, false, new UTF8Encoding(false));
        writer_.NewLine = "\n";
        writer_.WriteLine(header);
        writer_.Flush();
        var info = new ProcessStartInfo(smi, "--query-gpu=" + query + " --format=csv,noheader,nounits --loop-ms="
                                                 + intervalMs.ToString(CultureInfo.InvariantCulture));
        info.UseShellExecute = false;
        info.CreateNoWindow = true;
        info.RedirectStandardOutput = true;
        info.RedirectStandardError = true;
        process_ = new Process();
        process_.StartInfo = info;
        process_.OutputDataReceived += OnLine;
        process_.ErrorDataReceived += (s, e) => { if (e.Data != null) lock (gate_) errors_.AppendLine(e.Data); };
        process_.Start();
        process_.BeginOutputReadLine();
        process_.BeginErrorReadLine();
    }

    private static long UnixNanoseconds(DateTimeOffset time) {
        return (time.UtcTicks - new DateTimeOffset(1970, 1, 1, 0, 0, 0, TimeSpan.Zero).UtcTicks) * 100;
    }
    private static string Value(string text) {
        text = text.Trim();
        if (text == "Active") return "1";
        if (text == "Not Active") return "0";
        if (text.StartsWith("[") || text == "N/A") return "";
        return text;
    }
    private void OnLine(object sender, DataReceivedEventArgs e) {
        var received = DateTimeOffset.UtcNow;
        if (string.IsNullOrWhiteSpace(e.Data)) return;
        var parts = e.Data.Split(',');
        lock (gate_) {
            if (parts.Length != fields_) {
                ++Rejected;
                errors_.AppendLine(e.Data);
                return;
            }
            // nvidia-smi stamps the sample in local time; stdout reaches us up to one interval later.
            DateTime local;
            var sampled = DateTime.TryParseExact(parts[0].Trim(), "yyyy/MM/dd HH:mm:ss.fff", CultureInfo.InvariantCulture,
                                                 DateTimeStyles.AssumeLocal, out local)
                                  ? new DateTimeOffset(local).ToUniversalTime()
                                  : received;
            var row = new StringBuilder();
            row.Append(UnixNanoseconds(sampled).ToString(CultureInfo.InvariantCulture)).Append(',');
            row.Append(sampled.UtcDateTime.ToString("o", CultureInfo.InvariantCulture)).Append(',');
            row.Append(UnixNanoseconds(received).ToString(CultureInfo.InvariantCulture)).Append(',');
            row.Append(parts[0].Trim());
            for (int i = 1; i < parts.Length; ++i) row.Append(',').Append(Value(parts[i]));
            row.Append(',').Append(source_);
            writer_.WriteLine(row.ToString());
            writer_.Flush();
            ++Rows;
        }
    }
    public bool HasExited { get { return process_.HasExited; } }
    public void Dispose() {
        try {
            if (!process_.HasExited) process_.Kill();
            process_.WaitForExit(5000);
            process_.WaitForExit(); // drains the asynchronous readers
        } catch (InvalidOperationException) {
        }
        lock (gate_) writer_.Dispose();
        process_.Dispose();
    }
}
}
'@
}

# -NvidiaSmi, then PATH, then the newest driver-store copy (as in profile-variance.ps1).
function Find-NvidiaSmi([string]$NvidiaSmi) {
    if ($NvidiaSmi) { return (Resolve-Path -LiteralPath $NvidiaSmi).Path }
    $command = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    $store = Join-Path $env:WINDIR 'System32\DriverStore\FileRepository'
    if (-not (Test-Path -LiteralPath $store)) { return $null }
    $found = Get-ChildItem -LiteralPath $store -Directory -Filter 'nv*' | ForEach-Object {
        Get-ChildItem -LiteralPath $_.FullName -File -Filter 'nvidia-smi.exe' -ErrorAction SilentlyContinue
    } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

# Drivers before the 2023 rename only know clocks_throttle_reasons.*; newer ones accept both names.
function Get-ReasonsPrefix([string]$Smi) {
    $help = (& $Smi --help-query-gpu 2>$null) -join "`n"
    if ($help -match 'clocks_event_reasons\.') { return 'clocks_event_reasons' }
    return 'clocks_throttle_reasons'
}

function Start-GpuTelemetry {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$NvidiaSmi,
        [ValidateRange(100, 10000)][int]$IntervalMs = 500
    )
    $smi = Find-NvidiaSmi $NvidiaSmi
    if (-not $smi) {
        Write-Warning 'Brak nvidia-smi: pomiar bez telemetrii GPU.'
        return $null
    }
    $prefix = Get-ReasonsPrefix $smi
    $fields = @('timestamp', 'pstate', 'clocks.current.graphics', 'clocks.current.memory', 'power.draw',
        'enforced.power.limit', 'temperature.gpu', "$prefix.active") + ($script:Reasons | ForEach-Object { "$prefix.$_" })
    $Path = [IO.Path]::GetFullPath($Path)
    $sampler = [StrefaTelemetry.Sampler]::new($smi, ($fields -join ','), $IntervalMs, $Path, $script:Header, $prefix,
        $fields.Count)
    [pscustomobject]@{
        Sampler = $sampler; Path = $Path; NvidiaSmi = $smi; ReasonsSource = $prefix; IntervalMs = $IntervalMs
        StartedUtc = [DateTimeOffset]::UtcNow.ToString('o')
    }
}

# Returns the number of rows; warns when nvidia-smi rejected the query or produced nothing.
function Stop-GpuTelemetry {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][AllowNull()]$Telemetry)
    if (-not $Telemetry) { return 0 }
    $sampler = $Telemetry.Sampler
    $exited = $sampler.HasExited
    $sampler.Dispose()
    if ($sampler.Rows -lt 2) { Write-Warning "Telemetria GPU: $($sampler.Rows) próbek. $($sampler.Errors)" }
    elseif ($exited) { Write-Warning "nvidia-smi zakończył się przed końcem pomiaru. $($sampler.Errors)" }
    if ($sampler.Rejected) { Write-Warning "Telemetria GPU: odrzucono $($sampler.Rejected) wierszy o złej liczbie pól." }
    return $sampler.Rows
}

# Rows with numeric columns converted ([long] utc_unix_ns, [double] clocks/power/temperature, [int] reasons).
function Import-GpuTelemetry {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)
    $number = { param($text) if ($text -eq '') { $null } else { [double]::Parse($text, [Globalization.CultureInfo]::InvariantCulture) } }
    Import-Csv -LiteralPath $Path | ForEach-Object {
        $row = [ordered]@{
            utc_unix_ns = [long]$_.utc_unix_ns; utc_iso = $_.utc_iso; pstate = $_.pstate
            graphics_mhz = & $number $_.graphics_mhz; memory_mhz = & $number $_.memory_mhz
            power_w = & $number $_.power_w; power_limit_w = & $number $_.power_limit_w
            temperature_c = & $number $_.temperature_c; reasons_active_mask = $_.reasons_active_mask
            reasons_source = $_.reasons_source
        }
        foreach ($reason in $script:Reasons) { $row["reason_$reason"] = if ($_."reason_$reason" -eq '1') { 1 } else { 0 } }
        [pscustomobject]$row
    }
}

function Get-Median([double[]]$Values) {
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    $sorted = [double[]]($Values | Sort-Object)
    return $sorted[[Math]::Min($sorted.Count - 1, [int][Math]::Floor($sorted.Count * 0.5))]
}

# Summary of the samples with StartNs <= utc_unix_ns <= EndNs. Reason fractions are the share of samples
# in which that reason was active; LimitingReasons lists those above zero, except gpu_idle.
function Get-GpuTelemetryWindow {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Telemetry,
        [Parameter(Mandatory = $true)][long]$StartNs,
        [Parameter(Mandatory = $true)][long]$EndNs
    )
    $inside = @($Telemetry | Where-Object { $_.utc_unix_ns -ge $StartNs -and $_.utc_unix_ns -le $EndNs })
    $result = [ordered]@{ Samples = $inside.Count }
    if ($inside.Count -eq 0) { return [pscustomobject]$result }
    $values = { param($name) [double[]]@($inside | Where-Object { $null -ne $_.$name } | ForEach-Object { $_.$name }) }
    $result.GraphicsMhzMedian = Get-Median (& $values 'graphics_mhz')
    $result.MemoryMhzMedian = Get-Median (& $values 'memory_mhz')
    $result.PowerWMedian = Get-Median (& $values 'power_w')
    $result.PowerLimitW = Get-Median (& $values 'power_limit_w')
    $temperatures = & $values 'temperature_c'
    $result.TemperatureCMax = if ($temperatures.Count) { ($temperatures | Measure-Object -Maximum).Maximum } else { $null }
    $result.Pstates = (@($inside | ForEach-Object { $_.pstate } | Sort-Object -Unique) -join '/')
    $result.NonP0Share = [Math]::Round(@($inside | Where-Object { $_.pstate -ne 'P0' }).Count / $inside.Count, 3)
    $limiting = @()
    foreach ($reason in $script:Reasons) {
        $share = (@($inside | Where-Object { $_."reason_$reason" -eq 1 }).Count) / $inside.Count
        $result["Reason_$reason"] = [Math]::Round($share, 3)
        if ($share -gt 0 -and $reason -ne 'gpu_idle') { $limiting += $reason }
    }
    $result.LimitingReasons = $limiting -join '+'
    return [pscustomobject]$result
}

Export-ModuleMember -Function Start-GpuTelemetry, Stop-GpuTelemetry, Import-GpuTelemetry, Get-GpuTelemetryWindow, Find-NvidiaSmi
