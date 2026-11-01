param(
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][string]$StopFile,
    [ValidateRange(10,3600)][int]$MaxSeconds=1800
)
$ErrorActionPreference='Stop'
$taskRows=[Collections.Generic.List[object]]::new()
$taskPolls=[Collections.Generic.List[object]]::new()
$taskClock=[Diagnostics.Stopwatch]::StartNew()
$taskFailure=$null
while(-not (Test-Path -LiteralPath $StopFile) -and $taskClock.Elapsed.TotalSeconds -lt $MaxSeconds){
    $taskQuery=[Diagnostics.Stopwatch]::StartNew();$taskStart=[DateTimeOffset]::UtcNow.ToString('o')
    try{
        $taskEngines=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -OperationTimeoutSec 5)
        $taskEnd=[DateTimeOffset]::UtcNow.ToString('o')
        foreach($taskEngine in $taskEngines){
            if($taskEngine.UtilizationPercentage -gt 0){
                $taskRows.Add([pscustomobject]@{QueryStartUtc=$taskStart;QueryEndUtc=$taskEnd;Engine=$taskEngine.Name
                    UtilizationPercentage=$taskEngine.UtilizationPercentage;CounterTimestamp=$taskEngine.Timestamp_PerfTime})
            }
        }
        $taskPolls.Add([pscustomobject]@{StartUtc=$taskStart;EndUtc=$taskEnd;QueryMs=$taskQuery.Elapsed.TotalMilliseconds;Instances=$taskEngines.Count})
    }catch{$taskFailure=$_.Exception.Message;break}
    Start-Sleep -Milliseconds 500
}
$taskRows | Export-Csv -LiteralPath (Join-Path $Output 'gpu-engines.csv') -NoTypeInformation -Encoding utf8
$taskPolls | Export-Csv -LiteralPath (Join-Path $Output 'gpu-engine-polls.csv') -NoTypeInformation -Encoding utf8
[ordered]@{available=($taskPolls.Count -gt 0);polls=$taskPolls.Count;active_rows=$taskRows.Count;error=$taskFailure
    scope='Whole protocol, asynchronous formatted Windows counters; includes idle and initialization; engine instances contain PID/LUID, no application titles.'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Output 'gpu-engine-status.json') -Encoding utf8
