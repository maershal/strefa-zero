from pathlib import Path
import importlib.util,json,copy
from unittest.mock import patch
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3b-results';root=d/'session'
spec=importlib.util.spec_from_file_location('variance',s/'scripts/analyze-variance.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
checks=[]
def negative(name,reader,predicate,mutate,expected):
 original=getattr(m,reader)
 def intercept(path):
  data=original(path)
  if predicate(path):mutate(data)
  return data
 try:
  with patch.object(m,reader,intercept),patch.object(m,'dump'),patch.object(m,'write_csv'):m.analyze(root)
 except ValueError as e:
  assert expected in str(e),(name,str(e));checks.append({'case':name,'result':'rejected','reason':str(e)})
 else:raise AssertionError('Accepted '+name)
negative('incomplete_schedule','rows',lambda p:p.name=='schedule.csv',lambda v:v.pop(),'Incomplete')
negative('wrong_replay_timeout','obj',lambda p:p.name=='run.json' and p.parent.name=='pre-off-1',lambda v:v.update(timeout_seconds=600),'Replay timeout')
negative('wrong_ctest_binary','obj',lambda p:p.name=='run.json' and p.parent.name=='full-qa',lambda v:v.update(executable_sha256='0'*64),'Executable hash')
negative('wrong_qa_command','obj',lambda p:p.name=='run.json' and p.parent.name=='full-qa',lambda v:v['arguments'].append('--version'),'CTest command')
negative('wrong_q_restrict','obj',lambda p:p.name=='run.json' and p.parent.name=='q-off-1',lambda v:v['arguments'].__setitem__(v['arguments'].index('--gpu-pbf-restrict')+1,'on'),'Wrong load trigger')
negative('missing_raw_tick','rows',lambda p:p.name=='window-steps.csv' and p.parent.name=='mission' and p.parent.parent.name=='post-off-1',lambda v:v.pop(),'Missing raw CPU')
negative('off_probe_enabled','rows',lambda p:p.name=='validation.csv' and p.parent.name=='pre-off-1',lambda v:v[0].update(header_copies='1'),'Probe overhead')
negative('missed_neighbor','rows',lambda p:p.name=='snapshots.csv' and p.parent.name=='mission',lambda v:v[0].update(missed='1'),'Incomplete candidate set')
original_parse=m.ET.parse
class BadTree:
 def __init__(self,path):self.tree=original_parse(path)
 def getroot(self):
  node=self.tree.getroot();m.ET.SubElement(node.find('.//testcase'),'skipped');return node
try:
 with patch.object(m.ET,'parse',BadTree),patch.object(m,'dump'),patch.object(m,'write_csv'):m.analyze(root)
except ValueError as e:
 assert 'Full QA failed' in str(e);checks.append({'case':'skipped_qa_case','result':'rejected','reason':str(e)})
else:raise AssertionError('Accepted skipped QA')
signals=[]
for inputs,expected in [([6,7],False),([6.1,6.2],True),([7,5],False)]:
 original=m.rows;captured={}
 def intercept(path):
  data=original(path)
  if path.name=='summary.csv' and path.parent.name in ['post-off-1','post-off-2']:
   replacement=inputs[0 if path.parent.name=='post-off-1' else 1]
   for x in data:
    if x['scene']=='mission' and x['metric']=='gpu_step_ms':
     for key in ['median','p95','p99','max']:x[key]=str(replacement)
  return data
 def sink(path,value):captured[path.name]=value
 with patch.object(m,'rows',intercept),patch.object(m,'dump',sink),patch.object(m,'write_csv'):m.analyze(root)
 actual=captured['capture-signal.json']['triggered'];assert actual==expected
 signals.append({'synthetic_first_post_medians':inputs,'expected_trigger':expected,'actual':actual,'scope':'Synthetic classification check; not measured performance'})
(d/'analyzer-negative-checks.json').write_text(json.dumps({'rejections':len(checks),'passed':len(checks),'checks':checks,'signal_fixture_checks':signals},indent=2)+'\n')
print('Replay negative controls',len(checks),'synthetic signal checks',len(signals),flush=True)
