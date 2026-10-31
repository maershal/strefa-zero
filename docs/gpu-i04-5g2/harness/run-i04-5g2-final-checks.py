from pathlib import Path
import subprocess
from i04_native import win
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g2-results'
steps=[('windows',['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(r/'work/run-i04-5g2-windows.ps1')]),('window-analysis',['python3','work/summarize-i04-5g2-windows.py']),('package-checks',['python3','-u','work/check-i04-5g2-package.py'])]
for name,args in steps:
 print('START',name,flush=True)
 with (d/(name+'.log')).open('w') as log:subprocess.run(args,cwd=r,stdout=log,stderr=subprocess.STDOUT,check=True)
 print('DONE',name,flush=True)
