from pathlib import Path
import json
from i04_native import run
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g-results';exe=r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'
results=[]
for repeat,sizes in [(1,[64,128,256]),(2,[256,128,64])]:
 for size in sizes:
  result=run(exe,['--gpu-coupling-check','--gpu-pbf-size',str(size)],d/'q-repeat'/f'{size}-{repeat}',180)
  results.append({'size':size,'repeat':repeat,**result})
  (d/'q-repeat-results.json').write_text(json.dumps(results,indent=2)+'\n')
  assert result['exit_code']==0 and not result['timed_out'],results[-1]
