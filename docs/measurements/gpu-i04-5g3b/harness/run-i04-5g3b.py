from pathlib import Path
import subprocess,json,hashlib,base64
from i04_native import win
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3b-results'
state=json.loads((d/'base-state.json').read_text());ctest=json.loads((d/'ctest-command.json').read_text())
ctest['sha256']=hashlib.sha256(Path('/mnt/c')/Path('x')).hexdigest() if False else ''
# Hash the actual Windows CTest without launching a shell command containing paths.
ps=f"$ErrorActionPreference='Stop';(Get-FileHash -LiteralPath '{ctest['executable']}' -Algorithm SHA256).Hash.ToLowerInvariant()"
ctest['sha256']=subprocess.check_output(['powershell.exe','-NoProfile','-EncodedCommand',base64.b64encode(ps.encode('utf-16le')).decode()],text=True).strip()
ctest['working_directory']=win(r/'work/build-strefa-zero-release');ctest['junit']=win(d/'ctest.xml')
plan={'schema_version':1,'baseline_sha256':state['baseline_sha256'],'trigger_sha256':state['g2_sha256'],'ctest':ctest,
'names':['pre-off-1','pre-off-2','full-qa','q-off-1','q-on-1','q-on-2','q-off-2','q-on-64','q-on-128','post-off-1','post-off-2','post-bounds','post-off-3','post-snapshots','post-off-4']}
(d/'replay-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
# Validate PS 5.1 syntax before any benchmark. Store effective tools now.
ps=f"$ErrorActionPreference='Stop';$t=$null;$e=$null;[void][Management.Automation.Language.Parser]::ParseFile('{win(s/'scripts/profile-variance.ps1')}',[ref]$t,[ref]$e);if($e.Count){{throw ($e | Out-String)}};@{{powershell=$PSVersionTable.PSVersion.ToString();parse_errors=0}} | ConvertTo-Json"
x=subprocess.run(['powershell.exe','-NoProfile','-EncodedCommand',base64.b64encode(ps.encode('utf-16le')).decode()],text=True,capture_output=True,check=True)
(d/'script-syntax.json').write_text(json.dumps(json.loads(x.stdout),indent=2)+'\n')
(d/'tools').mkdir()
for name in ['profile-variance.ps1','variance-gpu-observer.ps1','variance-window-observer.cs']:(d/'tools'/name).write_bytes((s/'scripts'/name).read_bytes())
with (d/'protocol.log').open('w') as log:
 p=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(s/'scripts/profile-variance.ps1'),'-BaselineExecutable',win(r/'outputs/StrefaZero-Windows-x64-I04_5-G/StrefaZero.exe'),'-TriggerExecutable',win(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'),'-Output',win(d/'session'),'-Protocol','replay','-ReplayPlan',win(d/'replay-plan.json'),'-ObserveGpuEngines','-TimeoutSeconds','180'],stdout=log,stderr=subprocess.STDOUT)
(d/'protocol-exit.json').write_text(json.dumps({'exit_code':p.returncode},indent=2)+'\n')
print('Replay finished, exit',p.returncode,flush=True);print((d/'protocol.log').read_text(),flush=True)
raise SystemExit(p.returncode)
