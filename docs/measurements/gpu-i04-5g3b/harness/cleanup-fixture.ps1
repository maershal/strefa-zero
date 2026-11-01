$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$taskTokens=$null;$taskErrors=$null
$taskAst=[Management.Automation.Language.Parser]::ParseFile('C:\users\mrmae\documents\codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\outputs\strefa-zero\scripts\profile-variance.ps1',[ref]$taskTokens,[ref]$taskErrors)
if($taskErrors.Count){throw ($taskErrors | Out-String)}
foreach($taskName in @('Join-NativeArguments','New-NativeInfo','Stop-OwnedNative')){
 $taskFunction=$taskAst.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $taskName},$true)
 . ([ScriptBlock]::Create($taskFunction.Extent.Text))
}
$BaselineExecutable=Join-Path $env:WINDIR 'System32\cmd.exe'
$taskParent=$null;$taskGrandchild=$null
try{
 $taskParent=[Diagnostics.Process]::Start((New-NativeInfo (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-EncodedCommand','JAB0AGEAcwBrAEkAbgBmAG8APQBbAEQAaQBhAGcAbgBvAHMAdABpAGMAcwAuAFAAcgBvAGMAZQBzAHMAUwB0AGEAcgB0AEkAbgBmAG8AXQA6ADoAbgBlAHcAKAApADsAJAB0AGEAcwBrAEkAbgBmAG8ALgBGAGkAbABlAE4AYQBtAGUAPQBKAG8AaQBuAC0AUABhAHQAaAAgACQAUABTAEgATwBNAEUAIAAnAHAAbwB3AGUAcgBzAGgAZQBsAGwALgBlAHgAZQAnADsAJAB0AGEAcwBrAEkAbgBmAG8ALgBBAHIAZwB1AG0AZQBuAHQAcwA9ACcALQBOAG8AUAByAG8AZgBpAGwAZQAgAC0ARQBuAGMAbwBkAGUAZABDAG8AbQBtAGEAbgBkACAAVQB3AEIAMABBAEcARQBBAGMAZwBCADAAQQBDADAAQQBVAHcAQgBzAEEARwBVAEEAWgBRAEIAdwBBAEMAQQBBAEwAUQBCAFQAQQBHAFUAQQBZAHcAQgB2AEEARwA0AEEAWgBBAEIAegBBAEMAQQBBAE0AdwBBAHcAQQBBAD0APQAnADsAJAB0AGEAcwBrAEkAbgBmAG8ALgBVAHMAZQBTAGgAZQBsAGwARQB4AGUAYwB1AHQAZQA9ACQAZgBhAGwAcwBlADsAJAB0AGEAcwBrAEkAbgBmAG8ALgBDAHIAZQBhAHQAZQBOAG8AVwBpAG4AZABvAHcAPQAkAHQAcgB1AGUAOwAkAHQAYQBzAGsATwB3AG4AQwBoAGkAbABkAD0AWwBEAGkAYQBnAG4AbwBzAHQAaQBjAHMALgBQAHIAbwBjAGUAcwBzAF0AOgA6AFMAdABhAHIAdAAoACQAdABhAHMAawBJAG4AZgBvACkAOwBbAEkATwAuAEYAaQBsAGUAXQA6ADoAVwByAGkAdABlAEEAbABsAFQAZQB4AHQAKAAnAEMAOgBcAHUAcwBlAHIAcwBcAG0AcgBtAGEAZQBcAGQAbwBjAHUAbQBlAG4AdABzAFwAYwBvAGQAZQB4AFwAMgAwADIANgAtADEAMAAtADIANABcAGMAaABjAGkAYQAtAGIAeQBtAC0AcwB0AHcAbwByAHoAeQAtAGcAcgAtAHQAeQBwAHUALQBzAG8AbABkAGEAdAAtADMAXAB3AG8AcgBrAFwAaQAwADQALQA1AGcAMwBiAC0AcgBlAHMAdQBsAHQAcwBcAGMAbABlAGEAbgB1AHAALQBjAGgAaQBsAGQALQBwAGkAZAAuAHQAeAB0ACcALABbAHMAdAByAGkAbgBnAF0AJAB0AGEAcwBrAE8AdwBuAEMAaABpAGwAZAAuAEkAZAApADsAUwB0AGEAcgB0AC0AUwBsAGUAZQBwACAALQBTAGUAYwBvAG4AZABzACAAMwAwAA==')))
 $taskWatch=[Diagnostics.Stopwatch]::StartNew()
 while(-not (Test-Path -LiteralPath 'C:\users\mrmae\documents\codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\work\i04-5g3b-results\cleanup-child-pid.txt')){if($taskWatch.Elapsed.TotalSeconds -gt 5){throw 'Own test descendant did not start'};Start-Sleep -Milliseconds 50}
 $taskOwnId=[int][IO.File]::ReadAllText('C:\users\mrmae\documents\codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\work\i04-5g3b-results\cleanup-child-pid.txt');$taskGrandchild=[Diagnostics.Process]::GetProcessById($taskOwnId)
 if($taskParent.HasExited -or $taskGrandchild.HasExited){throw 'Own process fixture exited early'}
 Stop-OwnedNative $taskParent $true
 if(-not $taskGrandchild.WaitForExit(3000) -or -not $taskParent.HasExited){throw 'Owned tree cleanup failed'}
 @{result='passed';own_parent_pid=$taskParent.Id;own_descendant_pid=$taskOwnId;terminated_owned_processes=2;scope='Only the synthetic test tree; no game or foreign process matching.'} | ConvertTo-Json
}finally{
 foreach($taskOwn in @($taskParent,$taskGrandchild)){if($taskOwn){if(-not $taskOwn.HasExited){$taskOwn.Kill();$taskOwn.WaitForExit()};$taskOwn.Dispose()}}
}
