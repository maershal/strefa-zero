from pathlib import Path
from collections import defaultdict
from statistics import median
import csv,json,hashlib,math
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g3-results';s=r/'outputs/strefa-zero'
def rows(p):return list(csv.DictReader(p.open(encoding='utf-8-sig')))
def num(v):return float(str(v).replace(',','.'))
def dump(name,v):(d/name).write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
series={g:rows(d/g/'analysis.csv') for g in ['controls','reproduction']}
agg=[]
for g,data in series.items():
 subsets={'all_cost':data}
 if g=='controls':subsets={'observer_off':[x for x in data if 'off-' in x['run']], 'observer_on':[x for x in data if 'on-' in x['run']]}
 else:subsets={'hidden_off':[x for x in data if x['cost_mode']=='off' and x['visible']=='False'],'visible_off':[x for x in data if x['cost_mode']=='off' and x['visible']=='True']}
 for subset,rr in subsets.items():
  for scene in ['mission','stress_16k','stress_32k','mission_blast']:
   for metric in ['gpu_step_ms','cpu_submit_ms','batch_mean_step_wall_ms']:
    a=[x for x in rr if x['scene']==scene and x['metric']==metric]
    agg.append({'protocol':g,'subset':subset,'scene':scene,'metric':metric,'runs':len(a),'median_of_medians':median(num(x['median']) for x in a),'min_median':min(num(x['median']) for x in a),'max_median':max(num(x['median']) for x in a),'median_of_p95':median(num(x['p95']) for x in a),'max_p95':max(num(x['p95']) for x in a)})
dump('aggregates.json',agg)
old=[]
for group in ['matrix-slow','matrix']:
 base=s/'docs/gpu-i04-5g2'/group
 for run in rows(base/'runs.csv'):
  if run['Variant']!='g1':continue
  folder=base/run['ProfileDirectory']
  for scene in ['mission','stress_16k','stress_32k','mission_blast']:
   raw=rows(folder/scene/'window-steps.csv');totals=dict(line.split('=',1) for line in (folder/scene/'window-counters.txt').read_text().splitlines() if '=' in line)
   item={'series':group,'run':run['Name'],'scene':scene,'source':folder.relative_to(s).as_posix(),'device_buffer_bytes':int(totals['device_buffer_bytes'])}
   for key in ['total_ms','solver_ms','prepare_ms','poll_ms','staging_ms','timer_begin_ms','timer_end_ms','dispatches','barriers','bins','body_reductions','readback_age_ticks']:
    a=[num(x[key]) for x in raw];item[key+'_median']=median(a);item[key+'_max']=max(a)
   old.append(item)
dump('historical-g1-cpu.json',old)
comparison=[]
for group in ['matrix-slow','matrix']:
 a=[x for x in old if x['series']==group and x['scene']=='mission']
 comparison.append({'series':group,'runs':len(a),**{key:median(x[key] for x in a) for key in a[0] if key.endswith(('_median','_max'))}})
dump('historical-g1-cpu-aggregate.json',comparison)
probes=[]
for name in ['snapshots-1','snapshots-2']:
 data=rows(d/'reproduction/snapshot-analysis.csv')
 for scene in ['mission','stress_16k','stress_32k','mission_blast']:
  a=[x for x in data if x['run']==name and x['scene']==scene and x['reach']=='2']
  probes.append({'run':name,'scene':scene,'captures':len(a),'missed':sum(int(x['missed']) for x in a),'dmax_max':max(num(x['dmax']) for x in a),'candidate_ratio_median':median(num(x['global_to_fresh']) for x in a),'candidate_ratio_max':max(num(x['global_to_fresh']) for x in a),'global_candidates_min':min(int(x['global_candidates']) for x in a),'global_candidates_max':max(int(x['global_candidates']) for x in a)})
dump('candidate-summary.json',probes)
host=[]
for group in ['controls','reproduction']:
 for x in json.loads((d/group/'host-analysis.json').read_text()):host.append({'protocol':group,**x})
dump('host-summary.json',host)
# Device telemetry is whole-process, not timed-tick-local. Preserve ranges without attribution.
nv=[]
for group in ['controls','reproduction']:
 for p in (d/group).glob('*/nvidia-smi.csv'):
  rr=[{k.strip():v.strip() for k,v in x.items() if k and v is not None} for x in rows(p)]
  item={'protocol':group,'run':p.parent.name,'samples':len(rr)}
  for key in ['pstate','name','driver_version']:
   item[key]=sorted(set(x.get(key,'') for x in rr))
  for prefix in ['temperature.gpu','utilization.gpu','clocks.current.graphics','clocks.current.memory','memory.used','memory.total']:
   numbers=[]
   for x in rr:
    keys=[k for k in x if k.startswith(prefix)]
    if keys:
     try:numbers.append(float(x[keys[0]].split()[0]))
     except ValueError:pass
   item[prefix]={'min':min(numbers),'max':max(numbers)} if numbers else None
  nv.append(item)
dump('nvidia-summary.json',nv)
state=json.loads((d/'base-state.json').read_text())
for path,key in [(r/'outputs/StrefaZero-Windows-x64-I04_5-G/StrefaZero.exe','baseline_exe_sha256'),(r/'outputs/StrefaZero-Windows-x64-I04_5-G2/StrefaZero.exe','trigger_exe_sha256'),(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe','current_runtime_sha256')]:
 assert hashlib.sha256(path.read_bytes()).hexdigest()==state[key],path
dump('runtime-integrity.json',{'unchanged':True,'baseline_sha256':state['baseline_exe_sha256'],'trigger_sha256':state['trigger_exe_sha256'],'runtime_sha256':state['current_runtime_sha256'],'runtime_rebuilt':False})
print(json.dumps({'aggregates':len(agg),'old_g1_scenes':len(old),'candidate_scenes':len(probes),'nv_runs':len(nv)},indent=2))
