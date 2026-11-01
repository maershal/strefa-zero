from pathlib import Path
import subprocess,base64,json
from i04_native import win
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g3b-results'
marker=d/'cleanup-child-pid.txt'
child=base64.b64encode('Start-Sleep -Seconds 30'.encode('utf-16le')).decode()
parent=f"""$taskInfo=[Diagnostics.ProcessStartInfo]::new();$taskInfo.FileName=Join-Path $PSHOME 'powershell.exe';$taskInfo.Arguments='-NoProfile -EncodedCommand {child}';$taskInfo.UseShellExecute=$false;$taskInfo.CreateNoWindow=$true;$taskOwnChild=[Diagnostics.Process]::Start($taskInfo);[IO.File]::WriteAllText('{win(marker)}',[string]$taskOwnChild.Id);Start-Sleep -Seconds 30"""
parent64=base64.b64encode(parent.encode('utf-16le')).decode()
ps=f"""$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$taskTokens=$null;$taskErrors=$null
$taskAst=[Management.Automation.Language.Parser]::ParseFile('{win(r/'outputs/strefa-zero/scripts/profile-variance.ps1')}',[ref]$taskTokens,[ref]$taskErrors)
if($taskErrors.Count){{throw ($taskErrors | Out-String)}}
foreach($taskName in @('Join-NativeArguments','New-NativeInfo','Stop-OwnedNative')){{
 $taskFunction=$taskAst.Find({{param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $taskName}},$true)
 . ([ScriptBlock]::Create($taskFunction.Extent.Text))
}}
$BaselineExecutable=Join-Path $env:WINDIR 'System32\\cmd.exe'
$taskParent=$null;$taskGrandchild=$null
try{{
 $taskParent=[Diagnostics.Process]::Start((New-NativeInfo (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-EncodedCommand','{parent64}')))
 $taskWatch=[Diagnostics.Stopwatch]::StartNew()
 while(-not (Test-Path -LiteralPath '{win(marker)}')){{if($taskWatch.Elapsed.TotalSeconds -gt 5){{throw 'Own test descendant did not start'}};Start-Sleep -Milliseconds 50}}
 $taskOwnId=[int][IO.File]::ReadAllText('{win(marker)}');$taskGrandchild=[Diagnostics.Process]::GetProcessById($taskOwnId)
 if($taskParent.HasExited -or $taskGrandchild.HasExited){{throw 'Own process fixture exited early'}}
 Stop-OwnedNative $taskParent $true
 if(-not $taskGrandchild.WaitForExit(3000) -or -not $taskParent.HasExited){{throw 'Owned tree cleanup failed'}}
 @{{result='passed';own_parent_pid=$taskParent.Id;own_descendant_pid=$taskOwnId;terminated_owned_processes=2;scope='Only the synthetic test tree; no game or foreign process matching.'}} | ConvertTo-Json
}}finally{{
 foreach($taskOwn in @($taskParent,$taskGrandchild)){{if($taskOwn){{if(-not $taskOwn.HasExited){{$taskOwn.Kill();$taskOwn.WaitForExit()}};$taskOwn.Dispose()}}}}
}}
"""
(d/'harness').mkdir(exist_ok=True);(d/'harness/cleanup-fixture.ps1').write_text(ps)
x=subprocess.run(['powershell.exe','-NoProfile','-EncodedCommand',base64.b64encode(ps.encode('utf-16le')).decode()],text=True,capture_output=True,timeout=20)
assert x.returncode==0,(x.stdout,x.stderr)
(d/'owned-tree-cleanup.json').write_text(json.dumps(json.loads(x.stdout),indent=2)+'\n');print('Own native tree cleanup passed',flush=True)
