from pathlib import Path
import subprocess,hashlib,zipfile,json,re,shutil
from native_manifest import copy,manifest,verify
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3-results';data=s/'docs/gpu-i04-5g3'
package=r/'outputs/StrefaZero-Diagnostics-I04_5-G3'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
assert not data.exists() and not package.exists(),'Refusing to replace an existing diagnostic archive'
state=json.loads((d/'base-state.json').read_text());previous=json.loads((r/'work/i04-5g2-results/build-state.json').read_text())
for path,key in [(r/'outputs/StrefaZero-Windows-x64-I04_5-G/StrefaZero.exe','baseline_exe_sha256'),(r/'outputs/StrefaZero-Windows-x64-I04_5-G2/StrefaZero.exe','trigger_exe_sha256'),(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe','current_runtime_sha256')]:assert sha(path)==state[key],path
for name,digest in previous['source_sha256'].items():assert sha(s/name)==digest,name
for p in (s/'assets').rglob('*'):
 if p.is_file():assert sha(p)==sha(r/'outputs/StrefaZero-Windows-x64-I04_5-G2/assets'/p.relative_to(s/'assets')),p
integrity=json.loads((d/'runtime-integrity.json').read_text());integrity['g2_production_source_hashes_verified']=len(previous['source_sha256']);integrity['all_assets_equal_frozen_g2']=True;dump(d/'runtime-integrity.json',integrity)
consistent={}
for scene in ['stress_16k','stress_32k']:
 paths=[d/'reproduction'/name/scene/'snapshot-histograms.csv' for name in ['snapshots-1','snapshots-2']]
 assert paths[0].read_bytes()==paths[1].read_bytes();consistent[scene]={'equal_histogram_bytes':True,'sha256':sha(paths[0])}
dump(d/'candidate-histogram-integrity.json',consistent)
assert json.loads((d/'controls/analysis.json').read_text())['valid_native_runs']==4
assert json.loads((d/'reproduction/analysis.json').read_text())['valid_native_runs']==21
assert json.loads((d/'analyzer-negative-checks.json').read_text())['passed']==13
for name in ['native-artifacts.ps1','native_manifest.py','i04_native.py','run-native-i04.ps1','run-i04-5g3.ps1','run-i04-5g3-baseline.ps1','check-i04-5g3.py','check-i04-5g3-history.py','summarize-i04-5g3.py','write-i04-5g3-docs.py','update-i04-5g3-plan.py','finalize-i04-5g3.py']:
 target=d/'harness'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/'work'/name,target)
public=['profile-variance.ps1','variance-gpu-observer.ps1','variance-window-observer.cs','analyze-variance.py']
for name in public:
 target=d/'tools'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(s/'scripts'/name,target)
# This cache was created by this iteration's compile/import checks, not project input.
cache=s/'scripts/__pycache__/analyze-variance.cpython-312.pyc'
if cache.exists():cache.unlink()
try:cache.parent.rmdir()
except OSError:pass
copy(d,data);manifest(data);verify(data,data/'SHA256SUMS.txt')
package.mkdir();(package/'scripts').mkdir();(package/'docs').mkdir()
for name in public:shutil.copy2(s/'scripts'/name,package/'scripts'/name)
shutil.copy2(s/'docs/I04_5G3_GPU_VARIANCE.md',package/'docs/I04_5G3_GPU_VARIANCE.md')
copy(data,package/'docs/gpu-i04-5g3')
(package/'README.md').write_text('''# Strefa Zero — diagnostyka I04.5-G3-A

Narzędzia, raport i wyniki z 2026-10-30. [Raport](docs/I04_5G3_GPU_VARIANCE.md) zawiera komendy oraz ograniczenia. Nie jest to nowy runtime ani poprawka wydajności. W badaniu nie odtworzono wcześniejszego spowolnienia do około 12 ms; przyczyna G3 pozostaje otwarta.

Do odtworzenia potrzebne są Windows PowerShell 5.1, Python 3 (tylko biblioteka standardowa) i lokalne zamrożone paczki gry:

- G1 EXE SHA-256: `1a1f53457ca590187417818f24d42bbea93d11c7b29bc5e7d8fea7320f918d5e`.
- G2 EXE SHA-256: `333287c4063fa722eb4314fd70c0f6509cc92479c7890ee1da772bf1b0443af8`; tylko bodziec Q w protokole reproduction.

Wskaż pliki EXE wraz z ich oryginalnymi assets/SDL. Nie kopiuj samego EXE bez zasobów. Wybieraj nowe katalogi wyników. Supervisor uruchamia próby kolejno i odczytuje stan Windows/GPU; nie zmienia parametrów systemu. Obserwatory i sondy mają koszt opisany w raporcie. Opcjonalny NVIDIA SMI jest wykrywany automatycznie; próba reproduction z `-ObserveGpuEngines` wymaga dostępnej klasy liczników Windows.

`SHA256SUMS.txt` obejmuje cały pakiet; dane mają też własny manifest. Pakiet nie zawiera plików EXE, bitmap smoke ani nowych bibliotek.
''')
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
zip_count=0
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None
 for line in (package/'SHA256SUMS.txt').read_text().splitlines():
  digest,name=line.split('  ',1);assert hashlib.sha256(z.read(package.name+'/'+name)).hexdigest()==digest,name;zip_count+=1
 assert all(not x.lower().endswith(('.exe','.dll','.bmp','.pyc')) for x in z.namelist())
(r/'outputs/StrefaZero-I04_5-G3-SHA256SUMS.txt').write_text(f'{sha(archive)}  {archive.name}\n{sha(package/"SHA256SUMS.txt")}  {package.name}/SHA256SUMS.txt\n')
checks=[s/'README.md',s/'docs/I04_5G3_GPU_VARIANCE.md',s/'docs/PLAN_ROZWOJU_GPU.md',s/'docs/REVIEW_I04_ROZSTRZYGNIECIA.md',r/'outputs/03_PLAN_PRAC.md',package/'README.md',package/'docs/I04_5G3_GPU_VARIANCE.md']
links=0
for p in checks:
 text=p.read_text();assert not any(ord(ch)<32 and ch not in '\n\r\t' for ch in text),p
 assert sum(line.lstrip().startswith('```') for line in text.splitlines())%2==0,p
 for match in re.finditer(r'(?<!!)\[[^\]\n]+\]\(([^)]+)\)',text):
  target=match[1].strip('<>').split('#',1)[0]
  if not target or re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:',target):continue
  assert (p.parent/target).exists(),(p,target);links+=1
result={'zip':str(archive),'zip_bytes':archive.stat().st_size,'zip_sha256':sha(archive),'zip_crc':'passed','zip_manifest_members_verified':zip_count,'document_links_verified':links,'root_manifest_files':len(order),'data_manifest_files':len((data/'SHA256SUMS.txt').read_text().splitlines()),'runtime_rebuilt':False}
dump(r/'work/i04-5g3-package-integrity.json',result);print(json.dumps(result,indent=2),flush=True)
