from pathlib import Path
import subprocess,sys,shutil
from i04_native import run,win
r=Path(__file__).resolve().parent.parent;d=r/'work/i06-results'
subprocess.run(['/mnt/c/WINDOWS/System32/cmd.exe','/c',win(r/'work/build-i06.cmd')],check=True)
shutil.copy2(r/'work/i06-build.log',d/'msvc-final-build.log')
a=run(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe',['--gameplay-check'],d/'gameplay-final',timeout=180)
sys.exit(0 if a['exit_code']==0 else 1)
