param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateRange(1,5)][int]$WindowRepetitions = 3,
    [switch]$SkipWindows,
    [switch]$SkipThroughput,
    [switch]$ContinueOnFailure
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Output = [System.IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$taskFailures = New-Object 'System.Collections.Generic.List[string]'
function Invoke-ProfileRun([string]$Name,[string[]]$Flags) {
    $taskOutput = Join-Path $Output $Name
    $taskArguments = @($Flags) + @('--profile',('"' + $taskOutput + '"'))
    Write-Host "Measuring $Name"
    $taskProcess = Start-Process -FilePath $Executable -ArgumentList $taskArguments -PassThru -Wait
    if ($taskProcess.ExitCode -ne 0) {
        $taskFailures.Add("$Name=$($taskProcess.ExitCode)")
        if (-not $ContinueOnFailure) { throw "Measurement $Name failed with exit code $($taskProcess.ExitCode); see $taskOutput" }
        Write-Warning "Measurement $Name failed with exit code $($taskProcess.ExitCode); retaining its data and continuing"
    }
}
if (-not $SkipThroughput) { Invoke-ProfileRun 'throughput' @('--gpu-profile') }
if (-not $SkipWindows) {
    $taskVariants = @(
        @{Name='baseline'; Flags=@('--gpu-counters')},
        @{Name='no_readback'; Flags=@('--gpu-counters','--gpu-readback-period','0')},
        @{Name='readback_4'; Flags=@('--gpu-counters','--gpu-readback-period','4')},
        @{Name='no_coupling'; Flags=@('--gpu-counters','--gpu-no-coupling')},
        @{Name='no_diagnostics'; Flags=@('--gpu-counters','--gpu-no-diagnostics')},
        @{Name='no_counters'; Flags=@()},
        @{Name='group_timers'; Flags=@('--gpu-group-timers')}
    )
    for ($taskRepeat=1; $taskRepeat -le $WindowRepetitions; ++$taskRepeat) {
        $taskOrder = @($taskVariants)
        if ($taskRepeat % 2 -eq 0) { [array]::Reverse($taskOrder) }
        foreach ($taskVariant in $taskOrder) {
            Invoke-ProfileRun ("window-{0}-{1}" -f $taskVariant.Name,$taskRepeat) (@('--mission-smoke-test') + $taskVariant.Flags)
        }
    }
}
$taskFailures | Set-Content -LiteralPath (Join-Path $Output 'failed-runs.txt')
if ($taskFailures.Count -ne 0) { throw 'Some measurements failed; see failed-runs.txt. No failed samples were silently removed.' }
