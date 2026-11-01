from pathlib import Path
import json,subprocess
from i04_native import win
r=Path(__file__).resolve().parent.parent
index=0
def native(spec):
 global index
 index+=1;p=r/'work'/f'i04-5g2-artifact-{index}.json'
 p.write_text(json.dumps(spec,indent=2)+'\n')
 result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(r/'work/native-artifacts.ps1'),'-SpecFile',win(p)],text=True,capture_output=True,check=True)
 print(result.stdout.strip(),flush=True)
 return result.stdout.strip()
def copy(source,destination):
 return native({'action':'copy','source':win(source),'destination':win(destination)})
def manifest(root,paths=None,output=None):
 return native({'action':'manifest','root':win(root),'paths':paths,'output':win(output or root/'SHA256SUMS.txt')})
def verify(root,manifest_path):
 return native({'action':'verify','root':win(root),'manifest':win(manifest_path)})
