from pathlib import Path
import subprocess,shutil,json,hashlib
from i04_native import win
r=Path(__file__).resolve().parent.parent
out=r/'work/i04-5c-results/smoke-control';stage=r/'work/i04-5c-smoke-comparison'
stage.mkdir(exist_ok=True);out.mkdir(exist_ok=True)
records=[]
for mode in ('gl43','gl33','coupling','mission_ui'):
 for variant in ('A','C'):
  source=r/f'outputs/StrefaZero-Windows-x64-I04_5-{variant}'
  for name in ('StrefaZero.exe','SDL3.dll'):shutil.copy2(source/name,stage/name)
  shutil.copytree(source/'assets',stage/'assets',dirs_exist_ok=True)
  profile=out/f'{mode}-{variant}';profile.mkdir(exist_ok=False)
  flags=(['--gpu-coupling-check'] if mode=='coupling' else (['--mission-smoke-test'] if mode=='mission_ui' else ['--smoke-test']+(['--opengl33'] if mode=='gl33' else [])))
  spec={'executable':win(stage/'StrefaZero.exe'),'flags':flags,'profile':win(profile),'timeout_ms':120000,'result':win(profile/'native-result.json')}
  (profile/'native-spec.json').write_text(json.dumps(spec,indent=2)+'\n')
  with (profile/'native-process.log').open('w') as log:
   subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(r/'work/run-native-i04-timed.ps1'),'-SpecFile',win(profile/'native-spec.json')],stdout=log,stderr=subprocess.STDOUT,timeout=150)
  record={'variant':variant,'mode':mode,'exe_sha256':hashlib.sha256((stage/'StrefaZero.exe').read_bytes()).hexdigest(),**json.loads((profile/'native-result.json').read_text())}
  records.append(record);(out/'runs.json').write_text(json.dumps({'diagnostic_limit_seconds':120,'ctest_limits_unchanged':True,'runs':records},indent=2)+'\n')
  print(record,flush=True)
