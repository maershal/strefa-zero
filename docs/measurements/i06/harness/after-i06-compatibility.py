from pathlib import Path
import subprocess,hashlib,json
from i04_native import run,win
r=Path(__file__).resolve().parent.parent;d=r/'work/i06-results'
exe=r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'
for name,old in [('compatibility-mission',False),('previous-i05-mission',True)]:
 a=run(r/'outputs/StrefaZero-Windows-x64-I05/StrefaZero.exe' if old else exe,['--mission-test'],d/name,timeout=600)
 if not old and a['exit_code']!=0:print('Compatibility route still fails; inspect evidence',flush=True)
