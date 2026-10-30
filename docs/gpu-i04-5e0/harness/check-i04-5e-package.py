from pathlib import Path
import json
from i04_native import run
r=Path(__file__).resolve().parent.parent
d=r/'work/i04-5e-results';exe=r/'outputs/StrefaZero-Windows-x64-I04_5-E0/StrefaZero.exe'
results={}
for name,flags,expected in (
    ('package-cost-check',['--gpu-cost-check','--gpu-storage','auto'],0),
    ('package-profile-check',['--gpu-profile-check'],0),
    ('package-gl33-skip',['--opengl33','--gpu-cost-check'],77),
    ('package-window',['--mission-smoke-test','--show-test-window','--gl-debug','--gpu-counters'],0),
):
    result=run(exe,flags,d/name,180);results[name]=result
    assert result['exit_code']==expected and not result['timed_out'],(name,result)
(d/'package-checks.json').write_text(json.dumps(results,indent=2)+'\n')
