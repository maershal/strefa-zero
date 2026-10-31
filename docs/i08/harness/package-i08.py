from pathlib import Path
import hashlib,json,shutil,subprocess,zipfile,sys,time,importlib.util
from i04_native import run,win
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i08-results';build=r/'work/build-strefa-zero-release/runtime-070';pkg=r/'outputs/StrefaZero-Windows-x64-I08'

def files(folder):
 return sorted(p for p in folder.rglob('*') if p.is_file() and '.git' not in p.relative_to(folder).parts and '__pycache__' not in p.relative_to(folder).parts and p.suffix!='.pyc')
def manifest(folder):
 (folder/'SHA256SUMS.txt').write_text('\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(folder).as_posix() for p in files(folder) if p!=folder/'SHA256SUMS.txt')+'\n')

def package():
 spec=importlib.util.spec_from_file_location('i08_finalize',r/'work/finalize-i08.py');final=importlib.util.module_from_spec(spec);spec.loader.exec_module(final)
 assert (d/'gcc-result.json').exists() and (d/'windows-result.json').exists()
 final.assert_locked();assert not pkg.exists(),'Do not overwrite a published package'
 # Restore the common historical helper after the two queued runners finish.
 shutil.copy2(d/'run-native-original.ps1',r/'work/run-native-i04.ps1')
 pkg.mkdir()
 for name in ['StrefaZero.exe','StrefaPhysics.exe','SDL3.dll']:shutil.copy2(build/name,pkg/name)
 for name in ['assets','licenses']:shutil.copytree(s/name,pkg/name)
 for f in (s/'assets').rglob('*'):
  if f.is_file():assert hashlib.sha256(f.read_bytes()).digest()==hashlib.sha256((build/'assets'/f.relative_to(s/'assets')).read_bytes()).digest(),f
 checks=[]
 for name,flags,timeout in [('package-quality',['--gpu-render-check','--gl-debug'],240),('package-mission',['--mission-smoke-test','--show-test-window','--gpu-counters','--gl-debug','--test-vsync','off','--test-frame-limit','60'],120)]:
  process=run(pkg/'StrefaZero.exe',flags,d/name,timeout);checks.append({'name':name,**process})
 assert all(x['exit_code']==0 and not x['timed_out'] for x in checks),checks
 (d/'package-checks.json').write_text(json.dumps({'checks':checks,'exe_sha256':hashlib.sha256((pkg/'StrefaZero.exe').read_bytes()).hexdigest(),'assets_equal_to_validated_build':True},indent=2)+'\n')
 # Make the compact comparison without adding a Python imaging dependency.
 subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',win(s/'scripts/plot-render.ps1'),'-InputFolder',win(d/'final-quality'),'-OutputFile',win(d/'comparison.png')],check=True)
 summary=final.finalize()
 harness=d/'harness';harness.mkdir(exist_ok=True)
 for name in ['i04_native.py','run-native-i04.ps1','build-i08.cmd','final-i08-build-tests.py','final-i08-extra.py','finalize-i08.py','package-i08.py','check-i08-analysis.py']:
  shutil.copy2(r/'work'/name,harness/name)
 # Keep canonical images and all early failure text; avoid repeating positive
 # preliminary BMP captures in a second copy of the same report.
 def ignore(path,names):
  omit={'__pycache__'}
  if Path(path).name in ['render-2','quality-final']:omit.update(n for n in names if n.endswith('.bmp'))
  return omit
 reports=s/'docs/i08';assert not reports.exists();shutil.copytree(d,reports,ignore=ignore)
 shutil.copytree(reports,pkg/'docs/i08')
 shutil.copy2(s/'docs/I08_SCENE_RENDERER.md',pkg/'docs/I08_SCENE_RENDERER.md')
 (pkg/'REPORT.md').write_text((s/'docs/I08_SCENE_RENDERER.md').read_text().replace('(i08/','(docs/i08/'))
 (pkg/'README.md').write_text('''# Strefa Zero — Windows I08

Uruchom `StrefaZero.exe`, a w menu wybierz Misję GPU lub Laboratorium GPU.

A/D: ruch; Spacja: skok; PPM: plecak; LPM: karabin; R: przeładuj; G: granat; F5: reset; Esc: menu. Zniszcz trzy tarcze i dotrzyj do wyjścia.

Jakość obrazu wody: `--gpu-render-quality basic|standard|high`. Domyślne basic mieści się w odebranym budżecie GTX 1650. W narzędziach jakość można zmieniać bez resetu świata. Render ma stałe 960 × 576 i skalowanie nearest.

Fizyka pozostaje domyślnie legacy. `--gpu-fluid-profile balanced` to osobny eksperymentalny profil I07, z nieodebranym budżetem. Poziomy standard/high oferują więcej efektów i kosztują więcej.

Wymagane Windows x64 i OpenGL 4.3 do świata GPU. Zachowaj obok EXE plik SDL3.dll, katalogi assets i licenses. Referencyjne sceny CPU obsługują OpenGL 3.3.

[Raport i wyniki I08](REPORT.md) opisuje testy, pomiary i otwarte ograniczenia. Dokładny build: BUILD_INFO.json. Narzędzia odtwarzania pomiarów są w scripts.
''')
 (pkg/'BUILD_INFO.json').write_text(json.dumps({'version':'0.7.0','iteration':'I08','physics_revision':'I07.1','default_physics':'legacy','default_render_quality':'basic',**summary},indent=2)+'\n')
 (pkg/'scripts').mkdir()
 for name in ['profile-render.ps1','analyze-render.py','plot-render.ps1']:shutil.copy2(s/'scripts'/name,pkg/'scripts'/name)
 manifest(s);manifest(pkg)
 archives=[]
 for folder,name in [(pkg,'StrefaZero-Windows-x64-I08.zip'),(s,'StrefaZero-source-I08.zip')]:
  archive=r/'outputs'/name;assert not archive.exists()
  with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
   for p in files(folder):z.write(p,folder.name+'/'+p.relative_to(folder).as_posix())
   assert z.testzip() is None
  archives.append({'file':name,'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()});print(name,archive.stat().st_size,flush=True)
 (r/'outputs/StrefaZero-I08-SHA256SUMS.txt').write_text('\n'.join(x['sha256']+'  '+x['file'] for x in archives)+'\n')
 (r/'work/i08-package-result.json').write_text(json.dumps({'archives':archives,'source_and_package_manifests_verified':True},indent=2)+'\n')
 print('I08 package, source archive and checksums complete',flush=True)
if __name__=='__main__':package()
