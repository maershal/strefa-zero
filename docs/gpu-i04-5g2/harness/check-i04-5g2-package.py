from pathlib import Path
import json
from i04_native import run
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g2-results';exe=r/'outputs/StrefaZero-Windows-x64-I04_5-G2/StrefaZero.exe'
results={}
for name,flags,expected in (
 ('package-binding-check',['--gpu-pbf-restrict-check'],0),
 ('package-cost-check',['--gpu-cost-check','--gpu-pbf-restrict','on'],0),
 ('package-profile-check',['--gpu-profile-check','--gpu-pbf-restrict','on'],0),
 ('package-gl33-skip',['--opengl33','--gpu-pbf-restrict-check'],77),
):
 result=run(exe,flags,d/name,180);results[name]=result
 (d/'package-checks.json').write_text(json.dumps(results,indent=2)+'\n')
 assert result['exit_code']==expected and not result['timed_out'],(name,result)
