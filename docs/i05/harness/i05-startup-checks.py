from pathlib import Path
import json,shutil,hashlib
from i04_native import run
r=Path(__file__).resolve().parent.parent;out=r/'work/i05-results';exe=r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe';profile=out/'startup-cache'
def stats(path):
 return dict(line.strip().split('=',1) for line in path.read_text().splitlines() if '=' in line)
rows=[]
for mode in ['cold','warm']:
 result=run(exe,['--session-smoke-test','--shader-cache','on'],profile,timeout=90)
 assert result['exit_code']==0 and not result.get('timed_out',False),result
 data=stats(profile/'gpu-startup.txt');assert data['world_creations']=='1' and data['programs']=='42'
 assert data['cpu_game_created']=='0' and data['cpu_laboratory_created']=='0'
 assert data['source_builds']==('42' if mode=='cold' else '0'),data
 assert data['cache_hits']==('0' if mode=='cold' else '42'),data
 data['case']=mode;data['native_result']=result;rows.append(data)
 shutil.copy2(profile/'gpu-startup.txt',out/('startup-'+mode+'.txt'))
(out/'startup-summary.json').write_text(json.dumps({'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'rows':rows},indent=2)+'\n')
print('Startup cold/warm checks passed',flush=True)
