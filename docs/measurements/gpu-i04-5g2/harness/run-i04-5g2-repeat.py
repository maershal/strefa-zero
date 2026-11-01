from pathlib import Path
import json
from i04_native import run
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g2-results';exe=r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'
results=[]
for repeat,modes in [(1,['off','on']),(2,['on','off'])]:
 for mode in modes:
  result=run(exe,['--gpu-coupling-check','--gpu-pbf-restrict',mode],d/'q-repeat'/f'{mode}-{repeat}',180)
  results.append({'mode':mode,'size':256,'repeat':repeat,**result});(d/'q-repeat-results.json').write_text(json.dumps(results,indent=2)+'\n')
  assert result['exit_code']==0 and not result['timed_out'],results[-1]
for size in [64,128]:
 result=run(exe,['--gpu-coupling-check','--gpu-pbf-restrict','on','--gpu-pbf-size',str(size)],d/'q-interaction'/str(size),180)
 results.append({'mode':'on','size':size,'repeat':1,**result});(d/'q-repeat-results.json').write_text(json.dumps(results,indent=2)+'\n')
 assert result['exit_code']==0 and not result['timed_out'],results[-1]
