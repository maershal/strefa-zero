param([Parameter(Mandatory=$true)][string]$SpecFile)
$ErrorActionPreference='Stop'
$taskSpec=Get-Content -LiteralPath $SpecFile -Raw | ConvertFrom-Json
$taskArgs=@($taskSpec.flags)+@('--profile',('"'+$taskSpec.profile+'"'))
$taskProcess=Start-Process -FilePath $taskSpec.executable -ArgumentList $taskArgs -PassThru
$taskTimedOut=-not $taskProcess.WaitForExit([int]$taskSpec.timeout_ms)
if($taskTimedOut){$taskProcess.Kill();$taskProcess.WaitForExit()}
$taskProcess.Refresh()
$taskResult=[pscustomobject]@{pid=$taskProcess.Id;exit_code=$taskProcess.ExitCode;timed_out=$taskTimedOut}
$taskResult | ConvertTo-Json | Set-Content -LiteralPath $taskSpec.result
$taskResult | ConvertTo-Json | Write-Host
if($taskTimedOut){exit 124}
exit $taskProcess.ExitCode
