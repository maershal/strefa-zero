from pathlib import Path
from statistics import median
import csv,json
from collections import defaultdict
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g-results';root=d/'matrix'
def rows(p):
 with p.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def kv(p):return dict(line.split('=',1) for line in p.read_text(encoding='utf-8-sig').splitlines() if '=' in line)
records=[]
for run in rows(root/'runs.csv'):
 for scene in ['mission','stress_16k','stress_32k','mission_blast']:
  folder=root/run['ProfileDirectory']/scene;steps=rows(folder/'window-steps.csv');counters=kv(folder/'window-counters.txt');assert len(steps)==240
  values={name:{'median':median(float(x[name]) for x in steps),'max':max(float(x[name]) for x in steps)} for name in ['dispatches','barriers','bins','body_reductions','readback_age_ticks']}
  records.append({'run':run['Name'],'variant':run['Variant'],'scene':scene,'device_buffer_bytes':int(counters['device_buffer_bytes']),'ticks':len(steps),'metrics':values})
assert len(records)==64
result={'raw_cpu_counter_ticks':sum(x['ticks'] for x in records),'rows':records,'scope':'Raw per-tick CPU/call/HUD-age data; GPU timing is preserved as per-process percentiles over 240 samples, not individual GPU tick times.'}
(d/'counter-summary.json').write_text(json.dumps(result,indent=2)+'\n')
for v in ['q','g256','g128','g64']:
 row=next(x for x in records if x['variant']==v and x['scene']=='mission')
 print(v,row['device_buffer_bytes'],row['metrics'])
