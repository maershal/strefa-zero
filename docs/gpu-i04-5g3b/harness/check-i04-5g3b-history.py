from pathlib import Path
import json
from native_manifest import manifest,verify
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3b-results'
old={name:digest for digest,name in (line.split('  ',1) for line in (s/'SHA256SUMS.txt').read_text().splitlines())}
paths=[name for name in old if name.startswith(('docs/gpu','docs/reviews/')) or (name.startswith('docs/I') and name.endswith('.md'))]
current=d/'historical-current.sha256';manifest(s,paths,current)
for line in current.read_text().splitlines():
 digest,name=line.split('  ',1);assert digest==old[name],name
packages=[]
for letter in ['A','B','C','D','E0','Q','G','G2','G3']:
 verify(r/'outputs',r/'outputs'/f'StrefaZero-I04_5-{letter}-SHA256SUMS.txt');packages.append(letter)
result={'historical_files_verified':len(paths),'diagnostic_package_checksums_verified':packages,'review_source_unchanged':True}
(d/'historical-integrity.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
