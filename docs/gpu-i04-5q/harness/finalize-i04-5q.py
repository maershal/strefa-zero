from pathlib import Path
import subprocess,hashlib,zipfile,json,re
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';data=s/'docs/gpu-i04-5q';package=r/'outputs/StrefaZero-Windows-x64-I04_5-Q'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def manifest(folder):
 files=sorted((p for p in folder.rglob('*') if p.is_file() and p!=folder/'SHA256SUMS.txt'),key=lambda p:p.relative_to(folder).as_posix().casefold())
 (folder/'SHA256SUMS.txt').write_text(''.join(f'{sha(p)}  {p.relative_to(folder).as_posix()}\n' for p in files))
 return len(files)
report=(s/'docs/I04_5Q_GPU_COUPLING_QA.md').read_text()
assert 'PENDING' not in report
assert (package/'QA.txt').is_file()
assert sha(package/'StrefaZero.exe')==sha(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe')
frozen=json.loads((data/'source-state.json').read_text())
assert sha(package/'StrefaZero.exe')==frozen['exe_sha256']
for name,digest in frozen['source_sha256'].items():assert sha(s/name)==digest,name
for p in (s/'assets').rglob('*'):
 if p.is_file():assert sha(p)==sha(package/'assets'/p.relative_to(s/'assets')),p
print('data manifest',manifest(data),flush=True)
print('package manifest',manifest(package),flush=True)
paths=set(x.decode() for x in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=s).split(b'\0') if x)
paths.discard('SHA256SUMS.txt')
old=[x.split('  ',1)[1] for x in (s/'SHA256SUMS.txt').read_text().splitlines() if x]
order=[p for p in old if p in paths]
for p in sorted(paths-set(order),key=str.casefold):
 at=next((i for i,x in enumerate(order) if x.casefold()>p.casefold()),len(order));order.insert(at,p)
assert len(order)==len(paths) and set(order)==paths
(s/'SHA256SUMS.txt').write_text(''.join(f'{sha(s/p)}  {p}\n' for p in order))
for directory in (data,package,s):
 for line in (directory/'SHA256SUMS.txt').read_text().splitlines():
  digest,name=line.split('  ',1);assert sha(directory/name)==digest,name
print('root manifest verified',len(order),flush=True)
archive=package.with_suffix('.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in sorted(package.rglob('*')):
  if p.is_file():z.write(p,package.name+'/'+p.relative_to(package).as_posix())
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None
 assert hashlib.sha256(z.read(package.name+'/StrefaZero.exe')).hexdigest()==sha(package/'StrefaZero.exe')
checksum=r/'outputs/StrefaZero-I04_5-Q-SHA256SUMS.txt'
checksum.write_text(f'{sha(archive)}  {archive.name}\n{sha(package/"SHA256SUMS.txt")}  {package.name}/SHA256SUMS.txt\n')
# Verify all relative links in the changed documentation, ignoring remote links and anchors.
for p in [s/'README.md',s/'docs/I04_5Q_GPU_COUPLING_QA.md',s/'docs/PLAN_ROZWOJU_GPU.md',s/'docs/REVIEW_I04_ROZSTRZYGNIECIA.md',r/'outputs/03_PLAN_PRAC.md']:
 text=p.read_text()
 assert not any(ord(ch)<32 and ch not in '\n\r\t' for ch in text),p
 assert sum(line.lstrip().startswith('```') for line in text.splitlines())%2==0,p
 for match in re.finditer(r'(?<!!)\[[^\]\n]+\]\(([^)]+)\)',text):
  target=match[1].strip('<>').split('#',1)[0]
  if not target or re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:',target):continue
  assert (p.parent/target).exists(),(p,target)
print(json.dumps({'zip':str(archive),'zip_bytes':archive.stat().st_size,'zip_sha256':sha(archive),'zip_crc':'passed','document_links':'passed'},indent=2),flush=True)
