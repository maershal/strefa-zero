from pathlib import Path
import re,json,hashlib,subprocess,base64
from i04_native import win
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g3b-results';b=r/'work/build-strefa-zero-release'
if d.exists():
 assert {p.name for p in d.iterdir()}=={'CTestTestfile-original.cmake'},'Unexpected pre-existing results'
else:d.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
state={'stage':'G3-B','runtime_modified':False,'baseline_sha256':sha(r/'outputs/StrefaZero-Windows-x64-I04_5-G/StrefaZero.exe'),'g2_sha256':sha(b/'runtime-070/StrefaZero.exe'),'root_manifest_sha256_before':sha(s/'SHA256SUMS.txt')}
assert state['baseline_sha256']=='1a1f53457ca590187417818f24d42bbea93d11c7b29bc5e7d8fea7320f918d5e'
assert state['g2_sha256']=='333287c4063fa722eb4314fd70c0f6509cc92479c7890ee1da772bf1b0443af8'
source=(b/'CTestTestfile.cmake').read_text();(d/'CTestTestfile-original.cmake').write_text(source)
lines=[];tests=[];binaries={}
for line in source.splitlines():
 if line.startswith('subdirs('):continue
 if line.startswith('add_test('):
  name=re.search(r'\[=\[(.*?)\]=\]',line)[1]
  command=re.findall(r'"([^\"]*)"',line)
  original=command.copy()
  exe=b/'runtime-070'/Path(command[0]).name;binaries[exe.relative_to(b).as_posix()]=sha(exe)
  if '--profile' in command:
   index=command.index('--profile')+1;destination=win(d/'qa'/name).replace('\\','/')
   line=line.replace('"'+command[index]+'"','"'+destination+'"');command[index]=destination
  tests.append({'order':len(tests)+1,'name':name,'original_command':original,'command':command})
 if line.startswith('set_tests_properties('):
  line=line.replace(' PROPERTIES ',' PROPERTIES WORKING_DIRECTORY "'+win(b).replace('\\','/')+'" ',1)
 lines.append(line)
assert len(tests)==52
(d/'ctest').mkdir();(d/'ctest/CTestTestfile.cmake').write_text('\n'.join(lines)+'\n')
state['native_binary_sha256']=binaries
(d/'base-state.json').write_text(json.dumps(state,indent=2)+'\n')
(d/'qa-schedule.json').write_text(json.dumps(tests,indent=2)+'\n')
protocol={'schema_version':1,'prelude_repetitions':1,'qa_cases':52,'q_extra':[{'mode':mode,'pbf_size':size,'name':name} for name,mode,size in [('off-1','off',256),('on-1','on',256),('on-2','on',256),('off-2','off',256),('on-64','on',64),('on-128','on',128)]],
 'measurements':['pre-off-1','pre-off-2','post-off-1','post-off-2','post-bounds','post-off-3','post-snapshots','post-off-4'],
 'capture_signal':{'scene':'mission','metric':'gpu_step_ms','percentile':'median','greater_than_ms':6,'consecutive_off_runs':2,'purpose':'diagnostic label only; no change to correctness acceptance'},
 'bounded_policy':'One prelude of 52 original cases and six Q trials, then one fixed OFF/OFF/bounds/OFF/snapshots/OFF sequence. Keep all results even without a slow condition. No tuning or retries for a desired result.',
 'qa_limit_seconds':1500,'measurement_timeout_seconds':180,'worker_max_seconds':2400,
 'differences_from_old_session':'Fresh profile directories and CTest log directory; original working directory and test arguments retained. External SMI/WMI observation adds overhead. No rebuild, no system/driver changes. Six Q trials preserve the original order/flags.'}
(d/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n')
ctest=r/'work/ctest-path.txt'
ps="$taskCtest=Get-ChildItem -LiteralPath 'C:\\Program Files (x86)\\Microsoft Visual Studio\\18\\BuildTools\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\ctest.exe';$taskCtest.FullName"
path=subprocess.check_output(['powershell.exe','-NoProfile','-EncodedCommand',base64.b64encode(ps.encode('utf-16le')).decode()],text=True).strip()
(d/'ctest-command.json').write_text(json.dumps({'executable':path,'flags':['--test-dir',win(d/'ctest'),'--output-on-failure','--output-junit',win(d/'ctest.xml'),'--parallel','1']},indent=2)+'\n')
print('Prepared 52 frozen tests, six Q triggers, eight cost profiles; binary hashes',len(binaries))
