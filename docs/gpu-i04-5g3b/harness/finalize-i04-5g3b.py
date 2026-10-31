from pathlib import Path
import subprocess,hashlib,zipfile,json,re,shutil
from native_manifest import copy,manifest,verify
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3b-results';data=s/'docs/gpu-i04-5g3b';package=r/'outputs/StrefaZero-Diagnostics-I04_5-G3B'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
assert not data.exists() and not package.exists(),'Refusing to replace a diagnostic archive'
state=json.loads((d/'base-state.json').read_text());previous=json.loads((r/'work/i04-5g2-results/build-state.json').read_text())
for name,digest in state['native_binary_sha256'].items():assert sha(r/'work/build-strefa-zero-release'/name)==digest,name
assert sha(r/'outputs/StrefaZero-Windows-x64-I04_5-G/StrefaZero.exe')==state['baseline_sha256']
assert sha(r/'outputs/StrefaZero-Windows-x64-I04_5-G2/StrefaZero.exe')==state['g2_sha256']
for name,digest in previous['source_sha256'].items():assert sha(s/name)==digest,name
for p in (s/'assets').rglob('*'):
 if p.is_file():assert sha(p)==sha(r/'outputs/StrefaZero-Windows-x64-I04_5-G2/assets'/p.relative_to(s/'assets')),p
assert json.loads((d/'session/analysis.json').read_text())['valid_native_runs']==15
assert json.loads((d/'visibility/analysis.json').read_text())['valid_native_runs']==14
assert json.loads((d/'ctest-summary.json').read_text())['passed']==52
assert json.loads((d/'analyzer-negative-checks.json').read_text())['passed']==9
assert json.loads((d/'replay-guard-checks.json').read_text())['passed']==3
assert json.loads((d/'all-protocol-validation.json').read_text())['read_only']
versions=json.loads((d/'tool-source-versions.json').read_text())['versions']
for group in ['tools','tools-visibility']:
 for name,digest in versions[group].items():assert sha(d/group/name)==digest,(group,name)
for name,digest in versions['final_public'].items():assert sha(s/'scripts'/name)==digest,name
integrity=json.loads((d/'runtime-integrity.json').read_text());integrity['as_run_observer_snapshots_verified']=True;integrity['final_assets_equal_frozen_g2']=True
integrity['observer_note']='The collector compared replay sources before later public visibility/one-shot additions. Distinct as-run and final versions are retained and hashed.'
dump(d/'runtime-integrity.json',integrity)
for name in ['native-artifacts.ps1','native_manifest.py','i04_native.py','prepare-i04-5g3b.py','run-i04-5g3b.py','run-i04-5g3b-visibility.py','collect-i04-5g3b.py','check-i04-5g3b.py','check-i04-5g3b-cleanup.py','check-i04-5g3b-guards.py','summarize-i04-5g3b.py','check-i04-5g3b-history.py','write-i04-5g3b-docs.py','update-i04-5g3b-plan.py','finalize-i04-5g3b.py']:
 target=d/'harness'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/'work'/name,target)
public=['profile-variance.ps1','variance-gpu-observer.ps1','variance-window-observer.cs','analyze-variance.py']
for name in public:
 target=d/'tools-final'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(s/'scripts'/name,target)
copy(d,data);manifest(data);verify(data,data/'SHA256SUMS.txt')
package.mkdir();(package/'scripts').mkdir();(package/'docs').mkdir()
for name in public:shutil.copy2(s/'scripts'/name,package/'scripts'/name)
shutil.copy2(s/'docs/I04_5G3B_GPU_REPLAY.md',package/'docs/I04_5G3B_GPU_REPLAY.md')
copy(data,package/'docs/gpu-i04-5g3b')
(package/'README.md').write_text('''# Strefa Zero — diagnostyka I04.5-G3-B

[Raport](docs/I04_5G3B_GPU_REPLAY.md), dane i narzędzia z 2026-10-30. Odtworzono około 2,77→11,34→2,8 ms na tym samym EXE G1. Zebrano ślady sąsiedztwa w wolnym reżimie. Przyczyna nadal wymaga rozdzielenia warunków wykonania; pakiet nie jest nowym runtime ani poprawką wydajności.

Wymagania narzędzi: Windows PowerShell 5.1, Python 3 (biblioteka standardowa), lokalne zamrożone paczki G1/G2 i oryginalne testowe EXE CTest dla replay. Hashe są w raporcie i danych. Profile controls/reproduction/visibility przyjmują nowe katalogi Output. WMI GPU engines jest opcją wymagającą dostępnego licznika Windows; NVIDIA SMI wykrywa się automatycznie.

Zapisane plany replay są historią wykonanego pomiaru. Powtórka wymaga nowego planu ze świeżymi katalogami CTest/profile/JUnit, z zachowaniem oryginalnej kolejności, flag i limitów. Sam nowy Output nie przenosi ścieżek zawartych w planie. Końcowy supervisor odrzuca zużyte plany i istniejące katalogi QA przed uruchomieniem procesów. Narzędzia obu wykonanych serii i końcowe skrypty są zachowane osobno wraz z hashami.

Manifest SHA256SUMS.txt obejmuje cały pakiet; dane mają również własny manifest. Brak nowych plików EXE/DLL i bitmap smoke.
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
(r/'outputs/StrefaZero-I04_5-G3B-SHA256SUMS.txt').write_text(f'{sha(archive)}  {archive.name}\n{sha(package/"SHA256SUMS.txt")}  {package.name}/SHA256SUMS.txt\n')
checks=[s/'README.md',s/'docs/I04_5G3B_GPU_REPLAY.md',s/'docs/PLAN_ROZWOJU_GPU.md',s/'docs/REVIEW_I04_ROZSTRZYGNIECIA.md',r/'outputs/03_PLAN_PRAC.md',package/'README.md',package/'docs/I04_5G3B_GPU_REPLAY.md']
links=0
for p in checks:
 text=p.read_text();assert not any(ord(ch)<32 and ch not in '\n\r\t' for ch in text),p
 assert sum(line.lstrip().startswith('```') for line in text.splitlines())%2==0,p
 for match in re.finditer(r'(?<!!)\[[^\]\n]+\]\(([^)]+)\)',text):
  target=match[1].strip('<>').split('#',1)[0]
  if not target or re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:',target):continue
  assert (p.parent/target).exists(),(p,target);links+=1
result={'zip':str(archive),'zip_bytes':archive.stat().st_size,'zip_sha256':sha(archive),'zip_crc':'passed','zip_manifest_members_verified':zip_count,'document_links_verified':links,'root_manifest_files':len(order),'data_manifest_files':len((data/'SHA256SUMS.txt').read_text().splitlines()),'runtime_rebuilt':False}
dump(r/'work/i04-5g3b-package-integrity.json',result);print(json.dumps(result,indent=2),flush=True)
