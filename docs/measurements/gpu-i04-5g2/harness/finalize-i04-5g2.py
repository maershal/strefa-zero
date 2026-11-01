from pathlib import Path
import subprocess,hashlib,zipfile,json,re,shutil
from native_manifest import copy,manifest,verify
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g2-results';data=s/'docs/gpu-i04-5g2';package=r/'outputs/StrefaZero-Windows-x64-I04_5-G2'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert not data.exists(),'Refusing to replace historical results'
report=(s/'docs/I04_5G2_GPU_PBF_RESTRICT.md').read_text();assert 'PENDING' not in report
state=json.loads((d/'build-state.json').read_text())
assert sha(package/'StrefaZero.exe')==sha(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe')==state['exe_sha256']
for name,digest in state['source_sha256'].items():assert sha(s/name)==digest,name
for p in (s/'assets').rglob('*'):
 if p.is_file():assert sha(p)==sha(package/'assets'/p.relative_to(s/'assets')),p
assert (package/'QA.txt').is_file()
harness=['native-artifacts.ps1','native_manifest.py','build-i04-5g2.cmd','test-i04-5g2.cmd','i04_native.py','run-native-i04.ps1','run-i04-5g2-after-tests.py','run-i04-5g2-repeat.py','run-i04-5g2-matrix.ps1','run-i04-5g2-windows.ps1','run-i04-5g2-condition-control.ps1','collect-i04-5g2.py','summarize-i04-5g2.py','check-i04-5g2-analyzer.py','write-i04-5g2-docs.py','update-i04-5g2-plan.py','finalize-i04-5g2.py','check-i04-5g2-package.py','check-i04-5g2-history.py','summarize-i04-5g2-windows.py','run-i04-5g2-final-checks.py']
for name in harness:
 p=d/'harness'/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/'work'/name,p)
copy(d,data);manifest(data);verify(data,data/'SHA256SUMS.txt')
manifest(package);verify(package,package/'SHA256SUMS.txt')
paths=set(x.decode() for x in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=s).split(b'\0') if x)
paths.discard('SHA256SUMS.txt');assert not any('__pycache__' in x or x.endswith('.pyc') for x in paths)
old=[x.split('  ',1)[1] for x in (s/'SHA256SUMS.txt').read_text().splitlines() if x]
order=[p for p in old if p in paths]
for p in sorted(paths-set(order),key=str.casefold):
 at=next((i for i,x in enumerate(order) if x.casefold()>p.casefold()),len(order));order.insert(at,p)
assert len(order)==len(paths) and set(order)==paths
manifest(s,order);verify(s,s/'SHA256SUMS.txt')
archive=package.with_suffix('.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in sorted(package.rglob('*')):
  if p.is_file():z.write(p,package.name+'/'+p.relative_to(package).as_posix())
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None
 assert hashlib.sha256(z.read(package.name+'/StrefaZero.exe')).hexdigest()==state['exe_sha256']
(r/'outputs/StrefaZero-I04_5-G2-SHA256SUMS.txt').write_text(f'{sha(archive)}  {archive.name}\n{sha(package/"SHA256SUMS.txt")}  {package.name}/SHA256SUMS.txt\n')
for p in [s/'README.md',s/'docs/I04_5G2_GPU_PBF_RESTRICT.md',s/'docs/PLAN_ROZWOJU_GPU.md',s/'docs/REVIEW_I04_ROZSTRZYGNIECIA.md',r/'outputs/03_PLAN_PRAC.md']:
 text=p.read_text();assert not any(ord(ch)<32 and ch not in '\n\r\t' for ch in text),p
 assert sum(line.lstrip().startswith('```') for line in text.splitlines())%2==0,p
 for match in re.finditer(r'(?<!!)\[[^\]\n]+\]\(([^)]+)\)',text):
  target=match[1].strip('<>').split('#',1)[0]
  if not target or re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:',target):continue
  assert (p.parent/target).exists(),(p,target)
print(json.dumps({'zip':str(archive),'zip_bytes':archive.stat().st_size,'zip_sha256':sha(archive),'zip_crc':'passed','document_links':'passed','manifest_files':len(order)},indent=2),flush=True)
