from pathlib import Path
import subprocess,shutil,hashlib,json
from i04_native import win
r=Path(__file__).resolve().parent.parent;d=r/'work/i06-results'
subprocess.run(['/mnt/c/WINDOWS/System32/cmd.exe','/c',win(r/'work/build-i06.cmd')],check=True)
shutil.copy2(r/'work/i06-build.log',d/'msvc-final-validation-build.log')
subprocess.run(['/mnt/c/WINDOWS/System32/cmd.exe','/c',win(r/'work/retest-i06-final.cmd')],check=True)
s=r/'outputs/strefa-zero';before=json.loads((d/'source-before-execution-fix.json').read_text());changed=[name for name,digest in before.items() if hashlib.sha256((s/name).read_bytes()).hexdigest()!=digest]
assert changed==['src/gpu/execution_validation.cpp'],changed
(d/'final-diagnostic-change.json').write_text(json.dumps({'changed_source_since_full_regression':changed,'scope':'Retry refused admission in timer FIFO stress fixture; production engine and shaders unchanged','exe_sha256':hashlib.sha256((r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe').read_bytes()).hexdigest()},indent=2)+'\n')
print('Final targeted regression passed',flush=True)
