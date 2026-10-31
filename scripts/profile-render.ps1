param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet('quality','benchmark','windows')][string]$Suite='quality',
    [ValidateSet('basic','standard','high')][string]$Quality='basic'
)
$ErrorActionPreference='Stop'
$Executable=(Resolve-Path -LiteralPath $Executable).Path
if(Test-Path -LiteralPath $Output){throw 'Choose a new output directory; earlier trials are preserved.'}
$Output=[IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($Output) | Out-Null
[ordered]@{schema=1;iteration='I08';suite=$Suite;render_size=@(960,576);diagnostic_size=@(1600,960);warmup=120;samples=240;blocks=3;order=@('reference','candidate','candidate','reference');water_median_budget_ms=0.30;physics_profile='legacy';automatic_runtime_switching=$false} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $Output 'protocol.json')
function Invoke-RenderTrial([string]$Name,[string[]]$Flags,[int]$Timeout){
    $folder=Join-Path $Output $Name
    [IO.Directory]::CreateDirectory($folder) | Out-Null
    [ordered]@{name=$Name;executable=$Executable;sha256=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant();flags=$Flags;started_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'run-spec.json')
    $process=Start-Process -FilePath $Executable -ArgumentList (@($Flags)+@('--profile',('"'+$folder+'"'))) -PassThru
    $timedOut=-not $process.WaitForExit($Timeout)
    if($timedOut){$process.Kill();$process.WaitForExit()}
    $process.Refresh()
    [ordered]@{exit_code=$process.ExitCode;timed_out=$timedOut;pid=$process.Id;finished_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'run-result.json')
    Write-Host "$Name : $($process.ExitCode)"
    if($timedOut -or $process.ExitCode -ne 0){throw "Trial failed: $Name; artifacts preserved."}
}
if($Suite -eq 'quality'){Invoke-RenderTrial 'quality' @('--gpu-render-check','--gl-debug') 240000}
elseif($Suite -eq 'benchmark'){Invoke-RenderTrial 'benchmark' @('--gpu-render-benchmark') 1200000}
else {
    foreach($repeat in 1..3){
        $size=if($repeat -eq 2){@('--test-window-size','1600','960')}else{@()}
        Invoke-RenderTrial "off-$repeat" (@('--mission-smoke-test','--show-test-window','--gpu-counters','--gl-debug','--gpu-render-quality',$Quality,'--test-vsync','off','--test-unpaced')+$size) 120000
        Invoke-RenderTrial "on-$repeat" (@('--mission-smoke-test','--show-test-window','--gpu-counters','--gl-debug','--gpu-render-quality',$Quality,'--test-vsync','on')+$size) 120000
    }
}
