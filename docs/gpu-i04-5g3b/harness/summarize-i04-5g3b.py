from pathlib import Path
from collections import defaultdict
from statistics import median
import json,csv,hashlib
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3b-results'
def rows(p):return list(csv.DictReader(p.open(encoding='utf-8-sig')))
def dump(name,v):(d/name).write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
metrics=[]
for group in ['session','visibility']:
 metrics.extend({'group':group,**x} for x in rows(d/group/'analysis.csv'))
cohorts={'pre':lambda x:x['group']=='session' and x['run'].startswith('pre-'),
'slow_post_off':lambda x:x['group']=='session' and x['run'].startswith('post-off-'),
'follow_hidden_off':lambda x:x['group']=='visibility' and x['visible']=='False',
'follow_visible_off':lambda x:x['group']=='visibility' and x['visible']=='True'}
agg=[]
for cohort,predicate in cohorts.items():
 for scene in ['mission','stress_16k','stress_32k','mission_blast']:
  for metric in ['gpu_step_ms','cpu_submit_ms','batch_mean_step_wall_ms']:
   a=[x for x in metrics if predicate(x) and x['scene']==scene and x['metric']==metric]
   agg.append({'cohort':cohort,'scene':scene,'metric':metric,'runs':len(a),'median_of_medians':median(float(x['median']) for x in a),'min_median':min(float(x['median']) for x in a),'max_median':max(float(x['median']) for x in a),'median_of_p95':median(float(x['p95']) for x in a)})
dump('all-aggregates.json',agg)
comparison=[]
for scene in ['mission','stress_16k','stress_32k','mission_blast']:
 slow=d/'session/post-snapshots'/scene;fast=s/'docs/gpu-i04-5g3/reproduction/snapshots-1'/scene
 a,b=rows(fast/'snapshots.csv'),rows(slow/'snapshots.csv')
 key=lambda x:(x['snapshot'],x['reach'],x['label'],x['tick'],x['ordinal'])
 old={key(x):x for x in a};assert set(old)=={key(x) for x in b}
 ratios=[int(x['global_candidates'])/int(old[key(x)]['global_candidates']) for x in b]
 comparison.append({'scene':scene,'fast_source':fast.relative_to(s).as_posix(),'fast_snapshot_csv_sha256':sha(fast/'snapshots.csv'),
 'slow_source':slow.relative_to(d).as_posix(),'slow_snapshot_csv_sha256':sha(slow/'snapshots.csv'),'joined_rows':len(b),
 'candidate_count_slow_over_fast_median':median(ratios),'candidate_count_slow_over_fast_min':min(ratios),'candidate_count_slow_over_fast_max':max(ratios),
 'histogram_bytes_equal':(fast/'snapshot-histograms.csv').read_bytes()==(slow/'snapshot-histograms.csv').read_bytes(),
 'fast_histogram_sha256':sha(fast/'snapshot-histograms.csv'),'slow_histogram_sha256':sha(slow/'snapshot-histograms.csv'),
 'scope':'Matched capture labels/ticks/ordinals/reach; distinct measured processes, not identical full raw states or uninstrumented OFF samples.'})
dump('candidate-state-comparison.json',comparison)
nv=[]
for group in ['session','visibility']:
 for p in sorted((d/group).glob('*/nvidia-smi.csv')):
  rr=[{k.strip():v.strip() for k,v in x.items() if k and v is not None} for x in rows(p)]
  item={'group':group,'run':p.parent.name,'samples':len(rr)}
  def extract(x,prefix):
   keys=[k for k in x if k.startswith(prefix)]
   if not keys:return None
   try:return float(x[keys[0]].split()[0])
   except ValueError:return None
  item['pstates']=sorted(set(x.get('pstate','') for x in rr))
  for cohort,data in [('whole_process',rr),('gpu_util_at_least_90',[x for x in rr if (extract(x,'utilization.gpu') or 0)>=90])]:
   item[cohort]={'samples':len(data)}
   for prefix in ['clocks.current.graphics','clocks.current.memory','temperature.gpu','utilization.gpu','power.draw','memory.used','memory.total']:
    values=[extract(x,prefix) for x in data];values=[x for x in values if x is not None]
    item[cohort][prefix]={'min':min(values),'median':median(values),'max':max(values)} if values else None
  nv.append(item)
dump('nvidia-summary.json',nv)
host=[]
for group in ['session','visibility']:
 host.extend({'group':group,**x} for x in json.loads((d/group/'host-analysis.json').read_text()))
dump('host-summary.json',host)
source_versions={group:{p.name:sha(p) for p in (d/group).iterdir() if p.is_file()} for group in ['tools','tools-visibility']}
source_versions['final_public']={name:sha(s/'scripts'/name) for name in ['profile-variance.ps1','variance-gpu-observer.ps1','variance-window-observer.cs','analyze-variance.py']}
dump('tool-source-versions.json',{'versions':source_versions,'note':'Replay/visibility tools snapshotted before their runs. Final public script adds one-shot plan guards after measurements; those guards were independently checked. Core native runner/physics did not change.'})
print(json.dumps({'cost_profiles':len(metrics)//12,'aggregate_rows':len(agg),'state_comparisons':len(comparison),'nvidia_runs':len(nv)},indent=2),flush=True)
