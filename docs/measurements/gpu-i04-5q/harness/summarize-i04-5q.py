from pathlib import Path
import csv,hashlib,json,statistics,subprocess
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5q-results'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
rows=list(csv.DictReader((d/'disabled-control/analysis.csv').open()));control={}
for scope in sorted({x['scope'] for x in rows}):
 control[scope]={}
 for name in ('E0','Q'):
  control[scope][name]={}
  for metric in ('gpu_step_ms','cpu_submit_ms','batch_mean_step_wall_ms'):
   values=[x for x in rows if x['scope']==scope and x['variant']==name and x['metric']==metric]
   assert len(values)==3 and all(x['run_valid']=='1' for x in values)
   control[scope][name][metric]={'median_of_medians':statistics.median(float(x['median']) for x in values),'median_of_p95':statistics.median(float(x['p95']) for x in values),
    'median_range':[min(float(x['median']) for x in values),max(float(x['median']) for x in values)]}
(d/'performance-controls.json').write_text(json.dumps(control,indent=2)+'\n')
print(json.dumps(control,indent=2),flush=True)
p=d/'package-window'
def kv(path):return dict(line.split('=',1) for line in path.read_text().splitlines() if '=' in line and not line.startswith('source='))
frames=[x for x in csv.DictReader((p/'frame-timings.csv').open()) if x['rendered']=='1' and int(x['frame'])>=30]
times=sorted(float(x['frame_total_ms']) for x in frames)
window={'mission':kv(p/'mission-ui.txt'),'debug':kv(p/'gl-debug.txt'),'frames':len(frames),'focused':sum(int(x['focused']) for x in frames),'minimized':sum(int(x['minimized']) for x in frames),
 'median_ms':times[len(times)//2],'p95_ms':times[int(len(times)*.95)],'p99_ms':times[int(len(times)*.99)],'max_ms':max(times),'capture_included':True}
assert window['mission']['score']=='3' and window['mission']['dropped_seconds']=='0'
(d/'package-window-summary.json').write_text(json.dumps(window,indent=2)+'\n');print('Window',window['median_ms'],window['p95_ms'],window['focused'],flush=True)
historical=[]
for row in (s/'SHA256SUMS.txt').read_text().splitlines():
 digest,name=row.split('  ',1)
 if name.startswith(('docs/gpu-i04-5/','docs/gpu-i04-5b/','docs/gpu-i04-5c/','docs/gpu-i04-5d/','docs/gpu-i04-5e0/','docs/reviews/')) or name in ('docs/I04_5_GPU_STABILIZATION.md','docs/I04_5B_GPU_TRANSPORT.md','docs/I04_5C_GPU_WORK.md','docs/I04_5D_GPU_NEIGHBOR_READS.md','docs/I04_5E0_GPU_COSTS.md'):
  assert sha(s/name)==digest,name;historical.append(name)
for label in ('A','B','C','D','E0'):
 for row in (r/f'outputs/StrefaZero-I04_5-{label}-SHA256SUMS.txt').read_text().splitlines():
  digest,name=row.split('  ',1);assert sha(r/'outputs'/name)==digest,name
paths=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=s).split(b'\0')
source={x.decode():sha(s/x.decode()) for x in paths if x and(x.startswith((b'src/',b'assets/',b'scripts/')) or x==b'CMakeLists.txt')}
exe=sha(r/'work/build-strefa-zero-release/runtime-070/StrefaZero.exe');assert exe==json.loads((d/'measured-exe.json').read_text())['sha256']
baseline=sha(r/'outputs/StrefaZero-Windows-x64-I04_5-E0/StrefaZero.exe');assert baseline==json.loads((s/'docs/gpu-i04-5e0/source-state.json').read_text())['exe_sha256']
proof=sha(r/'work/i04-5q-control-build/StrefaZero.exe');assert proof==json.loads((d/'control-build.json').read_text())['sha256']
for path in (s/'assets').rglob('*'):
 if path.is_file():
  assert sha(path)==sha(r/'work/build-strefa-zero-release/runtime-070/assets'/path.relative_to(s/'assets'))
  assert sha(path)==sha(r/'outputs/StrefaZero-Windows-x64-I04_5-E0/assets'/path.relative_to(s/'assets'))
(d/'source-state.json').write_text(json.dumps({'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=s,text=True).strip(),'source_sha256':source,'exe_sha256':exe,
 'baseline_E0_exe_sha256':baseline,'reference_control_build_sha256':proof,'historical_files_verified':len(historical),'assets_equal_to_E0':True,
 'production_physics':'unchanged; acceptance methodology revised explicitly in Q'},indent=2)+'\n')
b=r/'work/build-strefa-zero-release';cache=(b/'CMakeCache.txt').read_text();ninja=(b/'build.ninja').read_text()
props={line.split(':',1)[0]:line.split('=',1)[1] for line in cache.splitlines() if line and not line.startswith(('#','//')) and ':' in line and '=' in line and line.split(':',1)[0] in ('CMAKE_BUILD_TYPE','CMAKE_CXX_COMPILER','STREFA_WARNINGS_AS_ERRORS')}
rule=next(x for x in ninja.split('\n\n') if x.startswith('build ') and 'coupling_replay.cpp.obj: CXX_COMPILER' in x)
props['coupling_replay_compile_rule']=rule;assert props['CMAKE_BUILD_TYPE']=='Release' and props['STREFA_WARNINGS_AS_ERRORS']=='ON' and all(x in rule for x in ('/W4','/WX','/GL'))
(d/'build-config.json').write_text(json.dumps(props,indent=2)+'\n')
print('Source frozen; historical files',len(historical),'and all A/B/C/D/E0 checksums verified',flush=True)
