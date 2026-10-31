from pathlib import Path
import subprocess,json,copy,base64
from i04_native import win
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3b-results';g=d/'guard-fixtures';g.mkdir()
plan=json.loads((d/'replay-plan.json').read_text());cases=[]
variants=[('reused_plan',plan,'already produced QA data')]
bad=copy.deepcopy(plan);bad['ctest']['flags'][-1]='2';variants.append(('parallel_command',bad,'recorded serial CTest'))
fresh=g/'ctest';fresh.mkdir();(fresh/'CTestTestfile.cmake').write_bytes((d/'ctest/CTestTestfile.cmake').read_bytes())
bad=copy.deepcopy(plan);bad['ctest']['flags'][1]=win(fresh);bad['ctest']['junit']=win(g/'fresh.xml');bad['ctest']['flags'][4]=bad['ctest']['junit'];variants.append(('existing_qa_profiles',bad,'QA profile directory already exists'))
for name,value,expected in variants:
 p=g/(name+'.json');p.write_text(json.dumps(value,indent=2)+'\n');out=g/(name+'-must-not-exist')
 x=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(s/'scripts/profile-variance.ps1'),'-BaselineExecutable',win(r/'outputs/StrefaZero-Windows-x64-I04_5-G/StrefaZero.exe'),'-TriggerExecutable',win(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'),'-Output',win(out),'-Protocol','replay','-ReplayPlan',win(p)],text=True,capture_output=True)
 assert x.returncode!=0 and expected in x.stderr and not out.exists(),(name,x.stdout,x.stderr)
 cases.append({'case':name,'result':'rejected before native launch and output creation','reason':expected})
(d/'replay-guard-checks.json').write_text(json.dumps({'passed':len(cases),'checks':cases},indent=2)+'\n')
ps=f"$ErrorActionPreference='Stop';$t=$null;$e=$null;[void][Management.Automation.Language.Parser]::ParseFile('{win(s/'scripts/profile-variance.ps1')}',[ref]$t,[ref]$e);if($e.Count){{throw ($e | Out-String)}};@{{powershell=$PSVersionTable.PSVersion.ToString();parse_errors=0;final_one_shot_guards=3}} | ConvertTo-Json"
x=subprocess.run(['powershell.exe','-NoProfile','-EncodedCommand',base64.b64encode(ps.encode('utf-16le')).decode()],text=True,capture_output=True,check=True)
(d/'script-syntax-final.json').write_text(json.dumps(json.loads(x.stdout),indent=2)+'\n')
print('One-shot replay guards',len(cases),flush=True)
