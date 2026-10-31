from pathlib import Path
import importlib.util,json,csv,shutil,contextlib,io
r=Path(__file__).resolve().parent.parent;source=r/'work/i04-5g-results/matrix';fixture=r/'work/i04-5g-analyzer-fixture-complete'
assert not fixture.exists();shutil.copytree(source,fixture)
spec=importlib.util.spec_from_file_location('pbf_analyzer',r/'outputs/strefa-zero/scripts/analyze-pbf.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with (fixture/'runs.csv').open(encoding='utf-8-sig',newline='') as f:runs=list(csv.DictReader(f))
case=next(x for x in runs if x['Variant']=='g128');folder=fixture/case['ProfileDirectory'];results={}
def reject(name,path,changed):
 old=path.read_bytes();path.write_bytes(changed)
 try:
  with contextlib.redirect_stdout(io.StringIO()):module.analyze(fixture)
 except AssertionError as error:results[name]={'rejected':True,'reason':str(error)}
 else:raise RuntimeError('Analyzer accepted corrupt evidence: '+name)
 finally:path.write_bytes(old)
p=folder/'environment.txt';reject('wrong_effective_size',p,p.read_bytes().replace(b'pbf_size_effective=128',b'pbf_size_effective=64'))
p=fixture/'runs.csv';reject('incomplete_experiment',p,b'\n'.join(p.read_bytes().splitlines()[:-1])+b'\n')
p=folder/'validation.csv'
with p.open(encoding='utf-8-sig',newline='') as f:data=list(csv.DictReader(f))
data[0]['timestamp_pairs']='1';stream=io.StringIO();writer=csv.DictWriter(stream,fieldnames=list(data[0]));writer.writeheader();writer.writerows(data)
reject('diagnostic_overhead_enabled',p,stream.getvalue().encode())
(r/'work/i04-5g-results/analyzer-fixture-checks.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
