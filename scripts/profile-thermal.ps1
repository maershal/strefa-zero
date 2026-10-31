param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Output,[ValidateSet('quality','benchmark','window')][string]$Suite='quality')
$ErrorActionPreference='Stop'
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$Output=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $Output){throw 'Choose a new directory; earlier results are preserved.'}
[IO.Directory]::CreateDirectory($Output)|Out-Null
$flags=switch($Suite){
 'quality' {@('--gpu-thermal-check','--gl-debug')}
 'benchmark' {@('--gpu-thermal-benchmark')}
 'window' {@('--gpu-smoke-test','--gpu-thermal','on','--show-test-window','--gl-debug')}
}
[ordered]@{iteration='I10';executable=$Executable;sha256=(Get-FileHash $Executable -Algorithm SHA256).Hash.ToLowerInvariant();flags=$flags;started_utc=[DateTime]::UtcNow.ToString('o');grid=@(320,192);tick_hz=120;render_hz_in_benchmark=60;blocks=3;warmup_ticks=120;samples=240;order=@('off','candidate','candidate','off');hot_budget_ms=.70;cold_budget_ms=.05;default_enabled=$false}|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 (Join-Path $Output 'run-spec.json')
$p=Start-Process -FilePath $Executable -ArgumentList (@($flags)+@('--profile',('"'+$Output+'"'))) -PassThru
$null=$p.Handle
$done=$p.WaitForExit(1800000)
if(-not $done){$p.Kill();$p.WaitForExit()}
$p.Refresh();$code=$p.ExitCode
if($null -eq $code){throw 'Native process exit status unavailable'}
[ordered]@{exit_code=$code;timed_out=(-not $done);finished_utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -Encoding UTF8 (Join-Path $Output 'run-result.json')
if(-not $done -or $code -ne 0){throw 'Thermal trial failed; inspect the preserved error and CSV files.'}
Write-Host "$Suite : PASS"
