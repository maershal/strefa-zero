from pathlib import Path
import subprocess,json,sys
sys.path.insert(0,'work');from i04_native import win
r=Path.cwd();d=r/'work/i04-5b-results/debug-windows';exe=r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'
for storage in ('direct','auto'):
 p=d/storage;p.mkdir(parents=True,exist_ok=False)
 spec={'executable':win(exe),'flags':['--mission-smoke-test','--gpu-storage',storage,'--show-test-window','--gl-debug','--gpu-counters'],'profile':win(p),'timeout_ms':120000,'result':win(p/'native-result.json')}
 (p/'native-spec.json').write_text(json.dumps(spec,indent=2)+'\n')
 with (p/'native-process.log').open('w') as log:subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(r/'work/run-native-i04-timed.ps1'),'-SpecFile',win(p/'native-spec.json')],stdout=log,stderr=subprocess.STDOUT,timeout=150)
 print(storage,json.loads((p/'native-result.json').read_text()),flush=True)
