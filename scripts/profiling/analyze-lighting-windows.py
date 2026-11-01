#!/usr/bin/env python3
"""Report complete focused-window I11 runs; retain all measurements on failure."""
import argparse,csv,json,math,statistics
from pathlib import Path

def percentile(values,q):
    v=sorted(values);return v[min(len(v)-1,int((len(v)-1)*q))]
def analyze(root):
    trials=[]
    expected={f'{view}-{mode}-{n}' for view in ('mission','laboratory') for mode in ('off','on') for n in (1,2,3)}
    for directory in sorted(p for p in root.iterdir() if p.is_dir()):
        path=directory/'frame-timings.csv'
        if not path.exists():continue
        rows=list(csv.DictReader(path.open(encoding='utf-8-sig',newline='')))
        post=[r for r in rows if int(r['frame'])>=120]
        samples=[r for r in post if int(r['rendered'])]
        if not samples:raise ValueError('no post-warmup frames: '+directory.name)
        gaps=[float(r['previous_interval_ms']) for r in samples]
        if not all(math.isfinite(x) and x>=0 for x in gaps):raise ValueError('invalid frame timing')
        focused=all(int(r['focused']) and not int(r['minimized']) for r in post)
        visible=all(not (int(r['window_flags']) & (4|8)) and int(r['window_pixel_width'])>0 and int(r['window_pixel_height'])>0 for r in post)
        info={}
        for line in (directory/'frame-timings-info.txt').read_text().splitlines():
            if '=' in line:k,v=line.split('=',1);info[k]=v
        complete=info.get('rows_dropped')=='0' and all(int(r['rendered']) for r in post)
        result=json.loads((directory/'run-result.json').read_text(encoding='utf-8-sig'))
        succeeded=result.get('exit_code')==0 and not result.get('timed_out')
        dropped=max(float(r['mission_dropped_seconds']) for r in samples)
        pending=max(float(r['mission_pending_seconds']) for r in samples)
        duration=(int(rows[-1]['start_ns'])-int(rows[0]['start_ns']))/1e9
        vsync=bool(int(samples[-1]['vsync_enabled']))
        mode_matches=all(bool(int(r['vsync_enabled']))==('-on-' in directory.name) for r in post)
        entry={'name':directory.name,'samples':len(samples),'duration_seconds':duration,'focused':focused,'visible':visible,'complete':complete,'process_pass':succeeded,'median_ms':statistics.median(gaps),'p95_ms':percentile(gaps,.95),'p99_ms':percentile(gaps,.99),'maximum_ms':max(gaps),'dropped_seconds':dropped,'max_pending_seconds':pending,'vsync':vsync,'requested_mode_matches_actual':mode_matches}
        entry['qualified']=focused and visible and complete and succeeded and mode_matches and duration>=29.9 and dropped==0 and pending<.1
        entry['frame_budget_pass']=entry['qualified'] and entry['p95_ms']<=16.67
        trials.append(entry)
    all_present={t['name'] for t in trials}==expected
    return {'schema':1,'all_twelve_trials_present':all_present,'trials':trials,'uncapped_acceptance':all_present and all(t['frame_budget_pass'] for t in trials if '-off-' in t['name']),'vsync_acceptance':all_present and all(t['frame_budget_pass'] for t in trials if '-on-' in t['name']),'default':'off unless module budget and focused uncapped windows both pass'}
def controls():
    import tempfile
    fields=('rendered','frame','previous_interval_ms','focused','minimized','window_flags','window_pixel_width','window_pixel_height','mission_dropped_seconds','mission_pending_seconds','start_ns','vsync_enabled')
    with tempfile.TemporaryDirectory() as temporary:
        root=Path(temporary)
        for view in ('mission','laboratory'):
            for mode in ('off','on'):
                for repeat in (1,2,3):
                    directory=root/f'{view}-{mode}-{repeat}';directory.mkdir()
                    (directory/'frame-timings-info.txt').write_text('rows_dropped=0\n')
                    (directory/'run-result.json').write_text('{"exit_code":0,"timed_out":false}')
                    with (directory/'frame-timings.csv').open('w',newline='') as stream:
                        writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader()
                        for frame in range(1876):writer.writerow(dict(zip(fields,(1,frame,16,1,0,2,960,576,0,.005,1_000_000_000+frame*16_000_000,int(mode=='on')))))
        assert analyze(root)['uncapped_acceptance'] and analyze(root)['vsync_acceptance']
        directory=root/'mission-off-1';path=directory/'frame-timings.csv';rows=list(csv.DictReader(path.open()));passed=[]
        def write(data):
            with path.open('w',newline='') as stream:
                writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader();writer.writerows(data)
        for key,value,name in [('focused',0,'unfocused'),('minimized',1,'minimized'),('window_flags',8,'hidden'),('window_flags',4,'occluded'),('rendered',0,'unrendered_frame'),('vsync_enabled',1,'wrong_vsync'),('mission_dropped_seconds',.01,'dropped_time'),('mission_pending_seconds',.2,'backlog')]:
            changed=[dict(row) for row in rows];changed[500][key]=value;write(changed)
            assert not analyze(root)['uncapped_acceptance'],name;passed.append(name)
        write(rows);(directory/'frame-timings-info.txt').write_text('rows_dropped=1\n')
        assert not analyze(root)['uncapped_acceptance'];passed.append('missing_rows')
        (directory/'frame-timings-info.txt').write_text('rows_dropped=0\n');(directory/'run-result.json').write_text('{"exit_code":1,"timed_out":false}')
        assert not analyze(root)['uncapped_acceptance'];passed.append('process_failure')
        return {'positive':'PASS','rejected':passed}
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path,nargs='?');parser.add_argument('--controls',action='store_true');args=parser.parse_args()
    if args.controls:
        result=controls();print(json.dumps(result,indent=2))
        if args.directory:(args.directory/'window-analysis-controls.json').write_text(json.dumps(result,indent=2)+'\n')
    if args.directory and any(args.directory.glob('*/frame-timings.csv')):
        result=analyze(args.directory);(args.directory/'lighting-window-summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
