from pathlib import Path
import subprocess,time,shutil,hashlib,json,xml.etree.ElementTree as ET
from i04_native import win,run
r=Path(__file__).resolve().parent.parent;d=r/'work/i05-results';exe=r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe'
while 'Total Test time' not in (d/'ctest.log').read_text(errors='replace'):time.sleep(2)
for attempt in range(10):
 try:
  cases=ET.parse(d/'ctest.xml').getroot().findall('.//testcase')
  if len(cases)==54:break
 except ET.ParseError:pass
 time.sleep(1)
assert len(cases)==54
failed=[c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None or c.find('skipped') is not None]
assert set(failed)<= {'gpu.mission_ui','gpu.laboratory_ui'},failed
report={'full_regression':{'total':54,'passed':54-len(failed),'not_passed':failed},
 'regression_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'controls':[]}
(d/'regression-build-sha256.txt').write_text(report['regression_sha256']+'\n')
print('Full regression',report['full_regression'],flush=True)
# Compare the same mission window scenario before changing the binary. Preserve
# failures and timeouts; no test threshold or solver setting is changed.
for name,binary in [('previous-mission-window',r/'outputs/StrefaZero-Windows-x64-I04_5-G2/StrefaZero.exe'),('i05-mission-window',exe)]:
 result=run(binary,['--mission-smoke-test'],d/name,timeout=60)
 report['controls'].append({'name':name,'result':result,'executable_sha256':hashlib.sha256(binary.read_bytes()).hexdigest()})
 (d/'final-checks.json').write_text(json.dumps(report,indent=2)+'\n')
with (d/'final-build-process.log').open('w') as log:
 subprocess.run(['/mnt/c/WINDOWS/System32/cmd.exe','/c',win(r/'work/build-i05.cmd')],stdout=log,stderr=subprocess.STDOUT,check=True)
shutil.copy2(r/'work/i05-build.log',d/'msvc-final-build.log')
for name,flags in [('final-session',['--session-smoke-test']),('final-laboratory',['--gpu-smoke-test'])]:
 result=run(exe,flags,d/name,timeout=90)
 report['controls'].append({'name':name,'result':result})
 if name=='final-session':assert result['exit_code']==0 and not result.get('timed_out',False),result
subprocess.run(['python3','work/i05-startup-checks.py'],cwd=r,check=True)
with (d/'install.log').open('w') as log:
 subprocess.run(['/mnt/c/WINDOWS/System32/cmd.exe','/c',win(r/'work/install-i05.cmd')],stdout=log,stderr=subprocess.STDOUT,check=True)
report['final_executable_sha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
report['final_ui_change']='Display existing export/replay success and error messages in the tools panel; no simulation changes.'
(d/'final-checks.json').write_text(json.dumps(report,indent=2)+'\n')
print('Final checks and installation complete',flush=True)
