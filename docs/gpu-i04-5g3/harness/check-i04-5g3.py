from pathlib import Path
import subprocess,json,base64,importlib.util,sys
from unittest.mock import patch
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3-results'
win=lambda p:subprocess.check_output(['wslpath','-w',str(p)],text=True).strip()
ps='''$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$taskChecks=@()
'''
for name in ['profile-variance.ps1','variance-gpu-observer.ps1']:
 ps+=f"$taskTokens=$null;$taskErrors=$null;[void][Management.Automation.Language.Parser]::ParseFile('{win(s/'scripts'/name)}',[ref]$taskTokens,[ref]$taskErrors)\nif($taskErrors.Count){{throw ($taskErrors | Out-String)}}\n$taskChecks+='{name}'\n"
ps+=f"Add-Type -Path '{win(s/'scripts/variance-window-observer.cs')}'\n$taskSample=[StrefaVariance.WindowObserver]::Read($PID)\n"
ps+="@{powershell=$PSVersionTable.PSVersion.ToString();parsed_scripts=$taskChecks;csharp_compilation='passed';read_only_window_probe='passed';sample_own_windows=$taskSample.Count} | ConvertTo-Json\n"
x=subprocess.run(['powershell.exe','-NoProfile','-EncodedCommand',base64.b64encode(ps.encode('utf-16le')).decode()],capture_output=True,text=True,check=True)
(d/'script-syntax.json').write_text(json.dumps(json.loads(x.stdout),indent=2)+'\n')
spec=importlib.util.spec_from_file_location('variance',s/'scripts/analyze-variance.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
checks=[]
def negative(name,root,reader,predicate,mutation,expected):
 original=getattr(m,reader)
 def intercept(path):
  value=original(path)
  if predicate(path):mutation(value)
  return value
 try:
  with patch.object(m,reader,intercept):m.analyze(d/root)
 except ValueError as e:
  assert expected in str(e),(name,str(e));checks.append({'case':name,'result':'rejected','reason':str(e)})
 else:raise AssertionError('Accepted '+name)
first=lambda p:p.parent.name=='observer-off-1'
negative('truncated_schedule','controls','rows',lambda p:p.name=='schedule.csv',lambda v:v.pop(),'Incomplete')
negative('unfrozen_baseline','controls','obj',lambda p:p.name=='environment.json',lambda v:v.update(baseline_sha256='0'*64),'Baseline hash')
negative('native_failed','controls','obj',lambda p:first(p) and p.name=='run.json',lambda v:v.update(exit_code=7),'Native process failed')
negative('wrong_native_pid','controls','obj',lambda p:first(p) and p.name=='run.json',lambda v:v.update(pid=1),'Native identity')
negative('wrong_exe','controls','obj',lambda p:first(p) and p.name=='run.json',lambda v:v.update(executable_sha256='0'*64),'Executable hash')
negative('wrong_visibility','controls','obj',lambda p:first(p) and p.name=='run.json',lambda v:v.update(visible_requested=True),'Visibility request')
def change_group(v):v['arguments'][v['arguments'].index('--gpu-pbf-size')+1]='64'
negative('wrong_group','controls','obj',lambda p:first(p) and p.name=='run.json',change_group,'Cost argument')
negative('off_probe_enabled','controls','rows',lambda p:first(p) and p.name=='validation.csv',lambda v:v[0].update(header_copies='1'),'Probe overhead')
negative('missing_cpu_tick','controls','rows',lambda p:p.name=='window-steps.csv' and p.parent.name=='mission' and p.parent.parent.name=='observer-off-1',lambda v:v.pop(),'Missing raw CPU')
negative('nan_cpu','controls','rows',lambda p:p.name=='window-steps.csv' and p.parent.name=='mission' and p.parent.parent.name=='observer-off-1',lambda v:v[0].update(solver_ms='nan'),'Nonfinite')
negative('missed_neighbor','reproduction','rows',lambda p:p.name=='snapshots.csv' and p.parent.name=='mission' and p.parent.parent.name=='snapshots-1',lambda v:v[0].update(missed='1'),'Incomplete candidate set')
negative('incomplete_histogram','reproduction','rows',lambda p:p.name=='snapshot-histograms.csv' and p.parent.name=='mission' and p.parent.parent.name=='snapshots-1',lambda v:v.pop(),'Incomplete candidate histogram')
negative('missing_gpu_worker','reproduction','obj',lambda p:p.name=='gpu-engine-status.json',lambda v:v.update(available=False),'GPU engine observer failed')
(d/'analyzer-negative-checks.json').write_text(json.dumps({'cases':len(checks),'passed':len(checks),'checks':checks},indent=2)+'\n')
print('Syntax/C# passed; negative analyzer controls',len(checks),flush=True)
