from pathlib import Path
import subprocess,base64,json,re,shutil,xml.etree.ElementTree as ET
from datetime import datetime
from i04_native import win
r=Path(__file__).resolve().parent.parent;b=r/'work/build-strefa-zero-release';d=r/'work/i04-5q-results'
script="([IO.File]::GetCreationTimeUtc('"+win(d/'ctest.log')+"')).ToString('o')"
stamp=subprocess.check_output(['powershell.exe','-NoProfile','-EncodedCommand',base64.b64encode(script.encode('utf-16le')).decode()],text=True).strip()
start=datetime.fromisoformat(stamp.replace('Z','+00:00')).timestamp()
cases=ET.parse(d/'ctest.xml').getroot().findall('.//testcase')
summary={'ctest_log_created_utc':stamp,'tests':len(cases),'passed':0,'failed':[],'cases':[]}
for case in cases:
 failed=case.find('failure') is not None or case.find('error') is not None
 skipped=case.find('skipped') is not None
 summary['cases'].append({**case.attrib,'failed':failed,'skipped':skipped})
 if failed:summary['failed'].append(case.attrib['name'])
 elif not skipped:summary['passed']+=1
summary['log_summary']=re.findall(r'(?:Total Test time.*|\d+% tests passed.*)',(d/'ctest.log').read_text())
(d/'ctest-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
excluded=[];copied=0
for line in (b/'CTestTestfile.cmake').read_text().splitlines():
 m=re.search(r'add_test\(\[=\[(.*?)\]=\].*?"--profile" "([^"]+)"',line)
 if not m:continue
 folder=b/Path(m[2]).name
 for p in folder.rglob('*'):
  if not p.is_file() or p.suffix.lower() not in ('.txt','.csv','.json'):continue
  if p.stat().st_mtime+0.01<start:excluded.append(str(p.relative_to(b)));continue
  q=d/'qa'/folder.name/p.relative_to(folder);q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);copied+=1
(d/'qa'/'collection-provenance.json').write_text(json.dumps({'start_utc':stamp,'fresh_files_copied':copied,'older_files_excluded':excluded},indent=2)+'\n')
for name in ('run-native-i04-timed.ps1','collect-i04-5q-final.py','i04_native.py','run-native-i04.ps1','run-i04-5q-control.ps1','check-i04-5q-package.py','test-i04-5q.cmd','build-i04-5q.cmd','install-i04-5q.cmd'):
 q=d/'harness'/name;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/'work'/name,q)
shutil.copy2(r/'work/i04-5q-build.log',d/'build.log')


print(json.dumps({'passed':summary['passed'],'tests':len(cases),'failed':summary['failed'],'fresh_files':copied,'stale_files_excluded':len(excluded)},indent=2))
