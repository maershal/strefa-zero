from pathlib import Path
import csv,json,statistics

r=Path(__file__).resolve().parent.parent
d=r/'work/i04-5d-results'
def values(path):
    return dict(line.split('=',1) for line in path.read_text().splitlines() if '=' in line and not line.startswith('source='))
def stats(xs):
    xs=sorted(xs)
    return {'samples':len(xs),'median_ms':xs[min(len(xs)-1,int(len(xs)*.5))],
        'p95_ms':xs[min(len(xs)-1,int(len(xs)*.95))],
        'p99_ms':xs[min(len(xs)-1,int(len(xs)*.99))],'max_ms':max(xs)}
results=[]
for name in ('windows-comparison','window-sorted-diagnostic'):
    folder=d/name
    for row in csv.DictReader((folder/'runs.csv').open(encoding='utf-8-sig')):
        p=folder/row['ProfileDirectory']
        result=json.loads((p/'run.json').read_text(encoding='utf-8-sig'))
        report=values(p/'mission-ui.txt')
        frames=list(csv.DictReader((p/'frame-timings.csv').open()))
        frames=[x for x in frames if int(x['rendered'])==1 and int(x['frame'])>=30]
        runtime=[x for x in frames if int(x['capture_performed'])!=1]
        debug=values(p/'gl-debug.txt')
        results.append({'group':name,'name':row['Name'],'variant':row['Variant'],'native':result,
            'mission':report,'gl_debug':debug,
            'full_frame_after_30':stats([float(x['frame_total_ms']) for x in frames]),
            'without_capture_after_30':stats([float(x['frame_total_ms']) for x in runtime]),
            'focused_frames':sum(int(x['focused']) for x in frames),
            'minimized_frames':sum(int(x['minimized']) for x in frames)})
        assert result['exit_code']==0 and not result['timed_out'],result
summary={}
for mode in ('full','narrow','sorted'):
    selected=[x for x in results if x['variant']==mode]
    summary[mode]={'runs':len(selected),'full_frame_median_of_medians_ms':statistics.median(x['full_frame_after_30']['median_ms'] for x in selected),
        'full_frame_p95_range_ms':[min(x['full_frame_after_30']['p95_ms'] for x in selected),max(x['full_frame_after_30']['p95_ms'] for x in selected)],
        'full_frame_max_ms':max(x['full_frame_after_30']['max_ms'] for x in selected),
        'all_no_dropped_time':all(float(x['mission']['dropped_seconds'])==0 for x in selected),
        'all_score_3':all(x['mission']['score']=='3' for x in selected),
        'focused_frames':sum(x['focused_frames'] for x in selected),
        'analyzed_frames':sum(x['full_frame_after_30']['samples'] for x in selected),
        'minimized_frames':sum(x['minimized_frames'] for x in selected)}
(d/'window-summary.json').write_text(json.dumps({'warmup_frames':30,'capture_included_in_full_frame':True,'runs':results,'summary':summary},indent=2)+'\n')
print(json.dumps(summary,indent=2))
