from pathlib import Path
import subprocess,json
r=Path(__file__).resolve().parent.parent
def win(p):return subprocess.check_output(['wslpath','-w',str(p)],text=True).strip()
def run(exe,flags,out,timeout=600):
 out.mkdir(parents=True,exist_ok=True)
 spec={'executable':win(exe),'flags':flags,'profile':win(out),'timeout_ms':timeout*1000,'result':win(out/'native-result.json')}
 (out/'native-spec.json').write_text(json.dumps(spec,indent=2)+'\n')
 with (out/'native-process.log').open('w') as log:
  p=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(r/'work/run-native-i04.ps1'),'-SpecFile',win(out/'native-spec.json')],stdout=log,stderr=subprocess.STDOUT,timeout=timeout+120)
 result=json.loads((out/'native-result.json').read_text());print(out.name,result,flush=True);return result
if __name__=='__main__':
 result=run(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe',['--gpu-coupling-check'],r/'work/i04-results/native-coupling-check')
 raise SystemExit(result['exit_code'])
