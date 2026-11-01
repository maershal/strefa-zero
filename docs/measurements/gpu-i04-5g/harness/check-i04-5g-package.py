from pathlib import Path
import json
from i04_native import run
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g-results';exe=r/'outputs/StrefaZero-Windows-x64-I04_5-G/StrefaZero.exe'
results={}
for name,flags,expected in (
 ('package-cost-check',['--gpu-cost-check','--gpu-pbf-size','64'],0),
 ('package-profile-check',['--gpu-profile-check'],0),
 ('package-gl33-skip',['--opengl33','--gpu-cost-check','--gpu-pbf-size','64'],77),
):
 result=run(exe,flags,d/name,180);results[name]=result
 (d/'package-checks.json').write_text(json.dumps(results,indent=2)+'\n')
 assert result['exit_code']==expected and not result['timed_out'],(name,result)
