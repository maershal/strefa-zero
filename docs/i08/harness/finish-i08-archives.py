from pathlib import Path
import os,hashlib,zipfile,shutil,json,time
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';pkg=r/'outputs/StrefaZero-Windows-x64-I08';d=r/'work/i08-results'
assert json.loads((d/'package-checks.json').read_text())['assets_equal_to_validated_build']
assert hashlib.sha256((pkg/'StrefaZero.exe').read_bytes()).hexdigest()==(d/'final-executable-sha256.txt').read_text().strip()
# Prune Git before walking it. Path.rglob traversed all Git objects even though
# the later file filter excluded them from the manifest.
def inventory(folder):
 result=[]
 for current,dirs,names in os.walk(folder):
  dirs[:]=[name for name in dirs if name not in {'.git','__pycache__'}]
  result.extend(Path(current)/name for name in names if not name.endswith('.pyc'))
 return sorted(result)
shutil.copy2(Path(__file__).resolve(),s/'docs/i08/harness/finish-i08-archives.py')
shutil.copy2(Path(__file__).resolve(),pkg/'docs/i08/harness/finish-i08-archives.py')
rows=[]
for folder,name in [(pkg,'StrefaZero-Windows-x64-I08.zip'),(s,'StrefaZero-source-I08.zip')]:
 started=time.monotonic();items=[p for p in inventory(folder) if p!=folder/'SHA256SUMS.txt'];print('Inventory',folder.name,len(items),flush=True)
 lines=[]
 for p in items:lines.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(folder).as_posix())
 (folder/'SHA256SUMS.txt').write_text('\n'.join(lines)+'\n');items.append(folder/'SHA256SUMS.txt');print('Manifest',folder.name,round(time.monotonic()-started,1),flush=True)
 archive=r/'outputs'/name
 with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for p in items:
   # Stream into an entry without a second remote metadata traversal.
   info=zipfile.ZipInfo(folder.name+'/'+p.relative_to(folder).as_posix(),date_time=(2026,10,31,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16
   with p.open('rb') as source,z.open(info,'w',force_zip64=True) as target:shutil.copyfileobj(source,target,256*1024)
  assert z.testzip() is None
 row={'file':name,'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()};rows.append(row);print(name,row['bytes'],round(time.monotonic()-started,1),flush=True)
(r/'outputs/StrefaZero-I08-SHA256SUMS.txt').write_text('\n'.join(x['sha256']+'  '+x['file'] for x in rows)+'\n')
(r/'work/i08-package-result.json').write_text(json.dumps({'archives':rows,'source_and_package_manifests_verified':True},indent=2)+'\n')
print('I08 archives verified',flush=True)
