from pathlib import Path
import subprocess,time,json,hashlib
from i04_native import win
r=Path(__file__).resolve().parent.parent;d=r/'work/i06-results'
for name in ['compatibility-mission','previous-i05-mission']:
 while not (d/name/'native-result.json').exists():time.sleep(2)
 result=json.loads((d/name/'native-result.json').read_text())
 print(name,result,flush=True)
 if name=='compatibility-mission':assert result['exit_code']==0,result
exe=r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'
(d/'regression-executable-sha256.txt').write_text(hashlib.sha256(exe.read_bytes()).hexdigest()+'\n')
result=subprocess.run(['/mnt/c/WINDOWS/System32/cmd.exe','/c',win(r/'work/test-i06.cmd')])
(d/'ctest-result.json').write_text(json.dumps({'exit_code':result.returncode})+'\n')
print('Final Windows regression:',result.returncode,flush=True)
