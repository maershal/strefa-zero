from pathlib import Path
import importlib.util,json,csv,subprocess,contextlib,io
from i04_native import win
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g2-results';source=d/'matrix';fixture=r/'work/i04-5g2-analyzer-fixture'
assert not fixture.exists()
spec_path=r/'work/i04-5g2-fixture-copy.json';spec_path.write_text(json.dumps({'action':'copy','source':win(source),'destination':win(fixture)}))
subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(r/'work/native-artifacts.ps1'),'-SpecFile',win(spec_path)],check=True)
spec=importlib.util.spec_from_file_location('restrict_analyzer',r/'outputs/strefa-zero/scripts/analyze-pbf-restrict.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with (fixture/'runs.csv').open(encoding='utf-8-sig',newline='') as f:runs=list(csv.DictReader(f))
case=next(x for x in runs if x['Variant']=='g2on');folder=fixture/case['ProfileDirectory'];results={}
def reject(name,path,changed):
 old=path.read_bytes();path.write_bytes(changed)
 try:
  with contextlib.redirect_stdout(io.StringIO()):module.analyze(fixture)
 except AssertionError as error:results[name]={'rejected':True,'reason':str(error)}
 else:raise RuntimeError('Analyzer accepted corrupt evidence: '+name)
 finally:path.write_bytes(old)
p=folder/'environment.txt';reject('wrong_effective_mode',p,p.read_bytes().replace(b'pbf_restrict_effective=1',b'pbf_restrict_effective=0'))
p=folder/'mission/window-steps.csv';reject('missing_raw_tick',p,b'\n'.join(p.read_bytes().splitlines()[:-1])+b'\n')
p=folder/'validation.csv'
with p.open(encoding='utf-8-sig',newline='') as f:data=list(csv.DictReader(f))
data[0]['timestamp_pairs']='1';stream=io.StringIO();writer=csv.DictWriter(stream,fieldnames=list(data[0]));writer.writeheader();writer.writerows(data)
reject('diagnostic_overhead_enabled',p,stream.getvalue().encode())
(d/'analyzer-fixture-checks.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
