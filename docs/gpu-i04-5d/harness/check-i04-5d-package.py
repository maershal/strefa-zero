from pathlib import Path
import json
from i04_native import run

r=Path(__file__).resolve().parent.parent
d=r/'work/i04-5d-results'
exe=r/'outputs/StrefaZero-Windows-x64-I04_5-D/StrefaZero.exe'
results={}
for name,flags,expected in (
    ('package-hot-check',['--gpu-hot-check','--gpu-storage','auto'],0),
    ('package-profile-sorted',['--gpu-profile-check','--gpu-neighbor-reads','sorted'],0),
    ('package-gl33-skip',['--opengl33','--gpu-hot-check'],77),
):
    result=run(exe,flags,d/name,600)
    results[name]=result
    assert result['exit_code']==expected and not result['timed_out'],(name,result)
(d/'package-checks.json').write_text(json.dumps(results,indent=2)+'\n')
