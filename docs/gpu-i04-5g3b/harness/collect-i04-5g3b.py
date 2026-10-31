from pathlib import Path
from datetime import datetime
import xml.etree.ElementTree as ET
import json,re,csv,hashlib
from statistics import median
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3b-results';root=d/'session';b=r/'work/build-strefa-zero-release'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def dump(name,v):(d/name).write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def rows(p):return list(csv.DictReader(p.open(encoding='utf-8-sig')))
state=json.loads((d/'base-state.json').read_text())
for name,digest in state['native_binary_sha256'].items():assert sha(b/name)==digest,name
assert sha(r/'outputs/StrefaZero-Windows-x64-I04_5-G/StrefaZero.exe')==state['baseline_sha256']
previous=json.loads((r/'work/i04-5g2-results/build-state.json').read_text())
for name,digest in previous['source_sha256'].items():assert sha(s/name)==digest,name
for name in ['profile-variance.ps1','variance-gpu-observer.ps1','variance-window-observer.cs']:assert sha(s/'scripts'/name)==sha(d/'tools'/name),name
all_cases=ET.parse(d/'ctest.xml').getroot().findall('.//testcase');planned=json.loads((d/'qa-schedule.json').read_text())
assert len(all_cases)==52 and [x.attrib['name'] for x in all_cases]==[x['name'] for x in planned]
assert all(x.find('failure') is None and x.find('error') is None and x.find('skipped') is None for x in all_cases)
old_cases=ET.parse(r/'work/i04-5g2-results/ctest.xml').getroot().findall('.//testcase')
old_log=(r/'work/i04-5g2-results/ctest.log').read_text(errors='replace')
new_log=(root/'full-qa/process-stdout.txt').read_text(encoding='utf-8-sig')
old_order=re.findall(r'Start\s+\d+:\s*([^\n\r]+)',old_log);new_order=re.findall(r'Start\s+\d+:\s*([^\n\r]+)',new_log)
assert old_order==new_order==[x['name'] for x in planned],(old_order,new_order)
summary={'cases':52,'passed':52,'failed':0,'skipped':0,'original_case_order_preserved':True,
'case_seconds_sum':sum(float(c.attrib['time']) for c in all_cases),'old_case_seconds_sum':sum(float(c.attrib['time']) for c in old_cases),
'limits':'Original CTest properties and working directory; only profile/log paths change. Outer supervisor limit 1500 s.',
'cases_detail':[{**c.attrib,'old_seconds':next(float(x.attrib['time']) for x in old_cases if x.attrib['name']==c.attrib['name'])} for c in all_cases]}
dump('ctest-summary.json',summary)
q=[]
for p in sorted(d.rglob('coupling-tests.txt')):
 text=p.read_text();end=re.search(r'legacy90_pass=(\d+)',text)
 assert 'status=passed' in text and 'disabled_coupling_negative_control=rejected' in text and 'paired_state_replay=passed (90 checkpoints' in text,p
 q.append({'path':p.relative_to(d).as_posix(),'checkpoints':90,'comparisons':360,'disabled_fault_rejected':True,'legacy90_pass':int(end[1]) if end else None})
dump('q-summary.json',{'processes':len(q),'checkpoints':sum(x['checkpoints'] for x in q),'comparisons':sum(x['comparisons'] for x in q),'disabled_faults_rejected':len(q),'legacy90_diagnostic_failed':sum(x['legacy90_pass']==0 for x in q),'records':q})
analysis=rows(root/'analysis.csv');aggregates=[]
for stage in ['pre','post']:
 for scene in ['mission','stress_16k','stress_32k','mission_blast']:
  for metric in ['gpu_step_ms','cpu_submit_ms','batch_mean_step_wall_ms']:
   a=[x for x in analysis if x['run'].startswith(stage+'-off-') and x['scene']==scene and x['metric']==metric]
   aggregates.append({'stage':stage,'scene':scene,'metric':metric,'runs':len(a),'median_of_medians':median(float(x['median']) for x in a),'min_median':min(float(x['median']) for x in a),'max_median':max(float(x['median']) for x in a),'median_of_p95':median(float(x['p95']) for x in a)})
dump('aggregates.json',aggregates)
current=json.loads((root/'host-analysis.json').read_text());dump('host-summary.json',current)
# Do not map unknown GPU PID rows in the CTest interval to particular children.
dump('runtime-integrity.json',{'unchanged':True,'runtime_rebuilt':False,'native_binary_hashes_verified':state['native_binary_sha256'],'production_source_hashes_verified':len(previous['source_sha256']),'all_three_as_run_observer_sources_unchanged':True})
probes=[]
for scene in ['mission','stress_16k','stress_32k','mission_blast']:
 rr=rows(root/'post-snapshots'/scene/'snapshots.csv')
 for reach in ['1','2']:
  a=[x for x in rr if x['reach']==reach]
  probes.append({'scene':scene,'reach':reach,'captures':len(a),'missed':sum(int(x['missed']) for x in a),'dmax_max':max(float(x['dmax']) for x in a),'candidate_ratio_median':median(float(x['global_to_fresh']) for x in a),'candidate_ratio_max':max(float(x['global_to_fresh']) for x in a)})
dump('candidate-summary.json',probes)
print(json.dumps({'qa_passed':len(all_cases),'order_preserved':True,'q_processes':len(q),'q_extra_processes':len([x for x in q if x['path'].startswith('session/')]),'native_hashes_verified':len(state['native_binary_sha256'])},indent=2),flush=True)
