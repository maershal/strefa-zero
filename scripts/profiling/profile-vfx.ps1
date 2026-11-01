param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [Parameter(Mandatory=$true)][string]$Output,
 [ValidateSet('quality','benchmark','windows','cpu','legacy')][string]$Suite='quality',
 [ValidateSet('off','low','high')][string]$Quality='low'
)
$ErrorActionPreference='Stop'
$Executable=(Resolve-Path -LiteralPath $Executable).Path
if(Test-Path -LiteralPath $Output){throw 'Choose a new output directory; earlier trials are preserved.'}
$Output=[IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($Output)|Out-Null
[ordered]@{schema=1;iteration='I09';suite=$Suite;quality=$Quality;capacity=16384;render_size=@(960,576);diagnostic_size=@(1600,960);warmup=120;samples=240;blocks=3;order=@('off','candidate','candidate','off');additional_gpu_budget_ms=.25;gpu_updates_per_frame=2;physics_profile='legacy';render_quality='basic';automatic_quality_switching=$false}|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 -LiteralPath (Join-Path $Output 'protocol.json')
function Invoke-Vfx([string]$Name,[string[]]$Flags,[int]$Timeout){
 $folder=Join-Path $Output $Name
 [IO.Directory]::CreateDirectory($folder)|Out-Null
 [ordered]@{executable=$Executable;sha256=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant();flags=$Flags;started_utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'run-spec.json')
 $p=Start-Process -FilePath $Executable -ArgumentList (@($Flags)+@('--profile',('"'+$folder+'"'))) -PassThru
 $null=$p.Handle
 $timedOut=-not $p.WaitForExit($Timeout)
 if($timedOut){$p.Kill();$p.WaitForExit()}
 $p.Refresh()
 if($null -eq $p.ExitCode){throw "Native exit status is unavailable: $Name"}
 [ordered]@{exit_code=$p.ExitCode;timed_out=$timedOut;pid=$p.Id;finished_utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'run-result.json')
 Write-Host "$Name : $($p.ExitCode)"
 if($timedOut -or $p.ExitCode -ne 0){throw "Trial failed: $Name; artifacts preserved."}
}
switch($Suite){
 'quality' {Invoke-Vfx 'quality' @('--gpu-vfx-check','--gl-debug') 900000}
 'benchmark' {Invoke-Vfx 'benchmark' @('--gpu-vfx-benchmark') 1800000}
 'cpu' {Invoke-Vfx 'cpu' @('--smoke-test','--gl-debug','--gpu-vfx-quality',$Quality) 180000}
 'legacy' {Invoke-Vfx 'legacy' @('--smoke-test','--opengl33','--gl-debug','--gpu-vfx-quality',$Quality) 180000}
 'windows' {foreach($repeat in 1..3){
   Invoke-Vfx "off-$repeat" @('--mission-smoke-test','--show-test-window','--gpu-counters','--gl-debug','--gpu-vfx-quality',$Quality,'--test-vsync','off','--test-unpaced') 180000
   Invoke-Vfx "on-$repeat" @('--mission-smoke-test','--show-test-window','--gpu-counters','--gl-debug','--gpu-vfx-quality',$Quality,'--test-vsync','on') 180000
 }}
}
