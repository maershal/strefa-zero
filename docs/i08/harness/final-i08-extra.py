from pathlib import Path
import time,json,subprocess,sys
from i04_native import run
r=Path(__file__).resolve().parent.parent;d=r/'work/i08-results';exe=r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'
start=time.monotonic()
while not (d/'windows-result.json').exists():
 if (d/'ctest-result.json').exists() and json.loads((d/'ctest-result.json').read_text())['exit_code']!=0:break
 if time.monotonic()-start>7200:raise TimeoutError('Native validation did not complete')
 time.sleep(2)
if (d/'windows-result.json').exists():
 result=run(exe,['--mission-smoke-test','--show-test-window','--gpu-counters','--gl-debug','--gpu-render-quality','high','--gpu-fluid-profile','balanced','--test-vsync','off','--test-frame-limit','60'],d/'high-balanced-window',120)
 (d/'high-balanced-result.json').write_text(json.dumps(result,indent=2)+'\n')
with (d/'gcc-build.log').open('w') as log:
 result=subprocess.run(['cmake','--build',str(r/'work/build-i05-gcc'),'--parallel','2'],stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0
with (d/'gcc-tests.log').open('w') as log:
 result=subprocess.run(['ctest','--test-dir',str(r/'work/build-i05-gcc'),'--output-on-failure'],stdout=log,stderr=subprocess.STDOUT)
(d/'gcc-result.json').write_text(json.dumps({'exit_code':result.returncode},indent=2)+'\n');assert result.returncode==0
print('Final GCC tests complete',flush=True)
