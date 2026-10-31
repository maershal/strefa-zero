param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Output,[ValidateSet('quality','benchmark','windows')][string]$Suite='quality',[ValidateSet('off','low','high')][string]$Quality='low')
$ErrorActionPreference='Stop'
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$Output=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $Output){throw 'Output exists. Preserve previous measurements.'}
[IO.Directory]::CreateDirectory($Output)|Out-Null
[ordered]@{iteration='I11';suite=$Suite;render_size=@(960,576);quality=$Quality;warmup=120;samples=240;blocks=3;extra_budget_ms=0.35;rebuild_peak_budget_ms=1.0;window_seconds=30}|ConvertTo-Json|Set-Content -Encoding UTF8 -LiteralPath (Join-Path $Output 'protocol.json')
function Invoke-LightingTrial([string]$Name,[string[]]$Flags,[int]$Timeout){
 $folder=Join-Path $Output $Name;[IO.Directory]::CreateDirectory($folder)|Out-Null
 [ordered]@{executable=$Executable;sha256=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash;flags=$Flags;started_utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'run-spec.json')
 $p=Start-Process -FilePath $Executable -ArgumentList (@($Flags)+@('--profile',('"'+$folder+'"'))) -PassThru
 $null=$p.Handle;$timedout=-not $p.WaitForExit($Timeout);if($timedout){$p.Kill();$p.WaitForExit()};$p.Refresh()
 [ordered]@{exit_code=$p.ExitCode;timed_out=$timedout;finished_utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'run-result.json')
 if($timedout -or $null -eq $p.ExitCode -or $p.ExitCode -ne 0){throw "Trial failed: $Name"}
}
if($Suite -eq 'quality'){Invoke-LightingTrial 'quality' @('--gpu-lighting-check','--gl-debug') 900000}
elseif($Suite -eq 'benchmark'){Invoke-LightingTrial 'benchmark' @('--gpu-lighting-benchmark') 3600000}
else{foreach($view in @('mission','laboratory')){foreach($repeat in 1..3){foreach($vsync in @('off','on')){
 $mode=if($view -eq 'mission'){'--mission-smoke-test'}else{'--gpu-smoke-test'}
 $flags=@($mode,'--show-test-window','--gpu-counters','--gpu-lighting-quality',$Quality,'--gpu-vfx-quality','low','--test-seconds','30','--test-vsync',$vsync)
 if($vsync -eq 'off'){$flags+='--test-unpaced'}
 Invoke-LightingTrial "$view-$vsync-$repeat" $flags 180000
}}}}
