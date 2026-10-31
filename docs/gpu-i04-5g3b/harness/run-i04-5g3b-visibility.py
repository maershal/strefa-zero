from pathlib import Path
import subprocess,json
from i04_native import win
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g3b-results'
with (d/'visibility.log').open('w') as log:
 p=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(r/'outputs/strefa-zero/scripts/profile-variance.ps1'),'-BaselineExecutable',win(r/'outputs/StrefaZero-Windows-x64-I04_5-G/StrefaZero.exe'),'-Output',win(d/'visibility'),'-Protocol','visibility','-ObserveGpuEngines','-TimeoutSeconds','180'],stdout=log,stderr=subprocess.STDOUT)
(d/'visibility-exit.json').write_text(json.dumps({'exit_code':p.returncode},indent=2)+'\n')
print('Visibility finished',p.returncode,flush=True);print((d/'visibility.log').read_text(),flush=True)
raise SystemExit(p.returncode)
