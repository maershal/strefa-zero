from pathlib import Path
import csv,json,math
from collections import defaultdict
from statistics import median
r=Path(__file__).resolve().parent.parent;d=r/'work/i04-5g-results';root=d/'windows'
def rows(p):
 with p.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def kv(p):return dict(line.split('=',1) for line in p.read_text(encoding='utf-8-sig').splitlines() if '=' in line)
def describe(v):
 v=sorted(v);return {'median':v[int(len(v)*.5)],'p95':v[int(len(v)*.95)],'p99':v[int(len(v)*.99)],'max':v[-1]}
runs=rows(root/'runs.csv');assert len(runs)==8;results=[]
for run in runs:
 assert run['ExitCode']=='0' and run['TimedOut']=='False' and not run['LaunchError']
 p=root/run['ProfileDirectory'];mission=kv(p/'mission-ui.txt');counters=kv(p/'window-counters.txt');debug=kv(p/'gl-debug.txt')
 native=json.loads((p/'run.json').read_text(encoding='utf-8-sig'));args=native['arguments']
 assert args==json.loads(run['ArgumentsJson']) and native['exit_code']==0 and not native['timed_out']
 variant=run['Variant']
 if variant!='q':
  size=variant[1:];assert args[args.index('--gpu-pbf-size')+1]==size and counters['pbf_size_effective']==size
  assert counters['local_size']==counters['reduction_size']=='256'
 else:assert '--gpu-pbf-size' not in args
 assert mission['score']=='3' and mission['mission_complete']=='1' and mission['paused_ok']=='1'
 assert int(mission['frames'])>=900 and int(mission['steps'])>=1700 and float(mission['dropped_seconds'])==0
 frames=[row for row in rows(p/'frame-timings.csv') if row['rendered']=='1' and int(row['frame'])>=30]
 assert len(frames)>=870 and all(float(x['mission_dropped_seconds'])==0 for x in frames)
 results.append({'name':run['Name'],'variant':variant,'frames':len(frames),'focused':sum(x['focused']=='1' for x in frames),'minimized':sum(x['minimized']=='1' for x in frames),
  'ticks':int(mission['steps']),'score':int(mission['score']),'dropped_seconds':float(mission['dropped_seconds']),
  'frame_ms':describe([float(x['frame_total_ms']) for x in frames]),'cpu_submit_ms':describe([float(x['total_ms']) for x in rows(p/'window-steps.csv')]),
  'debug':debug,'capture_included':True})
text='**8/8 widocznych prób okna**, Q/G256/G128/G64 po dwa powtórzenia w odwróconej kolejności, elapsed, VSync OFF, limit 60 FPS, debug/counters ON. Każda próba osiągnęła wynik 3, co najmniej 900 klatek i 1700 ticków, z działającą pauzą i bez odrzuconego czasu. To kontrole działającej gry na paczce G oraz niezmienionej Q.\n\n'
text+='| Wariant | Mediana / p95 całej klatki, ms | Fokus / analizowane klatki | Zminimalizowane |\n| --- | ---: | ---: | ---: |\n'
for v in ('q','g256','g128','g64'):
 data=[x for x in results if x['variant']==v];assert len(data)==2
 text+=f"| {v} | {median(x['frame_ms']['median'] for x in data):.5f} / {median(x['frame_ms']['p95'] for x in data):.5f} | {sum(x['focused'] for x in data)} / {sum(x['frames'] for x in data)} | {sum(x['minimized'] for x in data)} |\n"
phases={phase:sum(int(x['debug'].get(f'phase_{phase}_id131186',0)) for x in results) for phase in ('initialization','runtime','capture','validation','shutdown')}
dropped=sum(int(x['debug']['dropped']) for x in results)
text+=f"\nW tabeli mediana dwóch median i mediana dwóch p95; pierwsze 30 renderowanych klatek pominięto. Przechwytywanie obrazu jest wliczone, największa klatka {max(x['frame_ms']['max'] for x in results):.5f} ms. Limit 60 FPS i czas oczekiwania nie pozwalają przypisać podobnej klatki przyspieszeniu fizyki. Fokus i minimalizację raportujemy jawnie.\n\n"
text+=f"KHR_debug id131186 według fazy odebrania callbacku: {phases}; zgubione komunikaty: {dropped}. Faza odbioru asynchronicznego komunikatu nie jest dowodem przyczyny ani miejsca fizycznej pamięci. [Procesy](gpu-i04-5g/windows/runs.csv), [pełne podsumowanie](gpu-i04-5g/window-summary.json). Archiwum zachowuje CSV/logi; duże BMP z okien pominięto."
summary={'runs':8,'results':results,'debug_id131186_by_receipt_phase':phases,'debug_dropped':dropped,'report_text':text}
(d/'window-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(text)
