from pathlib import Path
import subprocess,time,xml.etree.ElementTree as ET
from i04_native import win
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g-results'
while not (d/'ctest.xml').exists():time.sleep(2)
for attempt in range(10):
 try:cases=ET.parse(d/'ctest.xml').getroot().findall('.//testcase');break
 except ET.ParseError:time.sleep(1)
assert len(cases)==44 and all(x.find('failure') is None and x.find('error') is None and x.find('skipped') is None for x in cases),'CTest did not pass completely; refusing performance measurements'
def invoke(name,command):
 print('START',name,flush=True)
 with (d/(name+'.log')).open('w') as log:subprocess.run(command,cwd=r,stdout=log,stderr=subprocess.STDOUT,check=True)
 print('DONE',name,flush=True)
invoke('collect',['python3','work/collect-i04-5g.py'])
invoke('q-repeat',['python3','-u','work/run-i04-5g-repeat.py'])
invoke('q-analysis',['python3','work/summarize-i04-5g.py'])
invoke('matrix',['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(r/'work/run-i04-5g-matrix.ps1')])
invoke('matrix-analysis',['python3','outputs/strefa-zero/scripts/analyze-pbf.py','--input',str(d/'matrix')])
