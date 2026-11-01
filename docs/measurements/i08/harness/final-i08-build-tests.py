from pathlib import Path
import subprocess,json,hashlib,sys,shutil,time
from i04_native import run,win
r=Path(__file__).resolve().parent.parent;d=r/'work/i08-results';s=r/'outputs/strefa-zero';exe=r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'
subprocess.run(['/mnt/c/Windows/System32/cmd.exe','/c',win(r/'work/build-i08.cmd')],check=True)
locked={p.relative_to(s).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for prefix in ['src','assets'] for p in (s/prefix).rglob('*') if p.is_file()}
(d/'final-production.json').write_text(json.dumps(locked,indent=2)+'\n');(d/'final-executable-sha256.txt').write_text(hashlib.sha256(exe.read_bytes()).hexdigest()+'\n');shutil.copy2(r/'work/i08-build.log',d/'final-msvc-build.log')
for name,flags,timeout in [('final-quality',['--gpu-render-check','--gl-debug'],240),('final-benchmark',['--gpu-render-benchmark'],1200)]:
 result=run(exe,flags,d/name,timeout);assert result['exit_code']==0 and not result['timed_out'],result
subprocess.run(['python3',str(s/'scripts/analyze-render.py'),str(d/'final-benchmark')],check=True)
# CTest is serial: GPU trials must not overlap.
ctest=Path('/mnt/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe')
with (d/'ctest.log').open('w') as log:
 result=subprocess.run([str(ctest),'--test-dir',win(r/'work/build-strefa-zero-release'),'--output-on-failure','--output-junit',win(d/'ctest.xml')],stdout=log,stderr=subprocess.STDOUT)
(d/'ctest-result.json').write_text(json.dumps({'exit_code':result.returncode},indent=2)+'\n');print('Windows CTest complete:',result.returncode,flush=True)
# Preserve regressions even if the next stage needs corrections.
assert result.returncode==0
windows=[]
for repeat in range(1,4):
 for mode in ['off','on']:
  flags=['--mission-smoke-test','--show-test-window','--gpu-counters','--gl-debug','--gpu-render-quality','basic','--test-vsync',mode]
  if mode=='off':flags+=['--test-unpaced']
  if repeat==2:flags+=['--test-window-size','1600','960']
  result=run(exe,flags,d/'windows'/f'{mode}-{repeat}',120);windows.append({'mode':mode,'repeat':repeat,**result})
(d/'windows-result.json').write_text(json.dumps(windows,indent=2)+'\n');assert all(x['exit_code']==0 and not x['timed_out'] for x in windows),windows
print('All native I08 checks complete',flush=True)
