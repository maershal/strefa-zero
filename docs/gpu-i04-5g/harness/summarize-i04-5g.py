from pathlib import Path
import csv,json,math,re
from collections import defaultdict
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g-results'
def rows(p):
 with p.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
paths={64:[d/'qa/gpu.pbf_64_coupling-profile'],128:[d/'qa/gpu.pbf_128_coupling-profile'],256:[d/'qa/coupling-profile']}
for trial in json.loads((d/'q-repeat-results.json').read_text()):
 assert trial['exit_code']==0 and not trial['timed_out']
 paths[trial['size']].append(d/'q-repeat'/f"{trial['size']}-{trial['repeat']}")
result={}
for size,folders in paths.items():
 maxima=defaultdict(lambda:defaultdict(float));legacy_fail=0
 for folder in folders:
  log=(folder/'coupling-tests.txt').read_text()
  assert f'pbf_size={size} particle_size=256 reduction_size=256' in log
  assert f'paired_source_pbf_size=256 replica_pbf_size={size}' in log and 'status=passed' in log
  flag=re.search(r'legacy90_pass=(\d)',log);assert flag;legacy_fail+=flag[1]=='0'
  data=defaultdict(list)
  for row in rows(folder/'coupling-replay.csv'):data[row['role']].append(row)
  summaries={x['role']:x for x in rows(folder/'coupling-replay-summary.csv')}
  for role in ('reference_repeat','local','combined','disabled_fault'):
   cases=data[role];summary=summaries[role]
   assert [int(x['tick']) for x in cases]==list(range(90))
   assert all(x['queue_pending']=='0' and x['queue_overflow']=='0' for x in cases)
   v={k:[float(x[k]) for x in cases] for k in ('mean','center','actor','center_dx','center_dy','actor_dx','actor_dy')}
   assert all(math.isfinite(x) for a in v.values() for x in a)
   actual={'mean_max':max(v['mean']),'center_max':max(v['center']),'actor_max':max(v['actor']),
    'mean_sum':sum(v['mean']),'center_bias':math.hypot(sum(v['center_dx']),sum(v['center_dy'])),
    'actor_bias':math.hypot(sum(v['actor_dx']),sum(v['actor_dy']))}
   for k,value in actual.items():
    assert math.isclose(value,float(summary[k]),abs_tol=1e-9,rel_tol=1e-8),(role,k)
    maxima[role][k]=max(maxima[role][k],value)
   accepted=all(value < (.05 if k.startswith('mean') else .02 if k.startswith('center') else .15) for k,value in actual.items())
   assert accepted==(summary['accepted']=='1')==(role!='disabled_fault')
   reactions=sum(int(x['reactions']) for x in cases)
   assert reactions==int(summary['reactions'])
   assert reactions>0 if role in ('local','combined') else reactions==0
 result[str(size)]={'runs':len(folders),'checkpoints':90*len(folders),'comparisons':360*len(folders),'faults_rejected':len(folders),'legacy90_diagnostic_rejections':legacy_fail,'maxima':dict(maxima)}
assert all(v['runs']==3 for v in result.values())
(d/'q-summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
