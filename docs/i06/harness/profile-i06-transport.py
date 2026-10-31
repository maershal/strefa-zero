from pathlib import Path
import time,json,csv,statistics
from i04_native import run
r=Path(__file__).resolve().parent.parent;d=r/'work/i06-results'
while not (d/'ctest-result.json').exists():time.sleep(2)
rows=[]
for name,mode in [('direct-before','direct'),('staged','staged'),('direct-after','direct')]:
 folder=d/('contract-'+name)
 result=run(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe',['--gameplay-check','--gpu-storage',mode],folder,timeout=180)
 assert result['exit_code']==0,result
 data=list(csv.DictReader((folder/'full-mission-contract.csv').open()))
 row={'case':name,'mode':mode,'trials':[]}
 for trial in ['1','2','3']:
  sub={}
  for k in ['cpu_contract_ms','cpu_logic_ms','gpu_ms']:
   v=sorted(float(x[k]) for x in data if x['trial']==trial);sub[k]={'median':statistics.median(v),'p95':v[int(.95*len(v))],'p99':v[int(.99*len(v))],'max':max(v)}
  row['trials'].append(sub)
 rows.append(row)
(d/'transport-contract-summary.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(rows,indent=2),flush=True)
