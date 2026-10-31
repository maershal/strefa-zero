"""Summarize the six visible-window trials without adding phase percentiles."""
import argparse, csv, json, math, statistics
from pathlib import Path

def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))

def distribution(values):
    a=sorted(values)
    if not a or any(not math.isfinite(x) or x<0 for x in a):
        raise ValueError('Invalid timing samples')
    return dict(samples=len(a),median=statistics.median(a),p95=a[min(len(a)-1,int(.95*len(a)))],p99=a[min(len(a)-1,int(.99*len(a)))],maximum=a[-1])

def properties(path):
    return dict(line.split('=',1) for line in path.read_text(encoding='utf-8-sig').splitlines() if '=' in line)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);args=parser.parse_args()
    summary=[]
    for repeat in range(1,4):
        for mode in ('off','on'):
            folder=args.folder/f'{mode}-{repeat}'
            result=json.loads((folder/'run-result.json').read_text(encoding='utf-8-sig'))
            if result['exit_code']!=0 or result['timed_out']:raise ValueError('Failed window trial')
            frames=rows(folder/'frame-timings.csv')
            kept=[x for x in frames if int(x['frame'])>=120 and x['rendered']=='1' and x['capture_performed']=='0']
            intervals=rows(folder/'mission-frames.csv')[120:]
            steps=rows(folder/'window-steps.csv')[120:]
            info=properties(folder/'frame-timings-info.txt');mission=properties(folder/'mission-ui.txt')
            if mission['mission_complete']!='1' or mission['score']!='3' or mission['dropped_seconds']!='0':
                raise ValueError('Mission or clock validation failed')
            summary.append(dict(vsync=mode,repeat=repeat,rows=len(frames),rows_dropped=int(info['rows_dropped']),
                frame_ms=distribution([float(x['frame_total_ms']) for x in kept]),
                interval_ms=distribution([float(x['frame_ms']) for x in intervals]),
                cpu_submit_ms=distribution([float(x['total_ms']) for x in steps]),
                interval_source='mission-frames.csv, all frames after 120-frame warmup',
                focus_fraction=sum(int(x['focused']) for x in kept)/len(kept),
                max_pending_seconds=max(float(x['mission_pending_seconds']) for x in kept),
                render_size=[int(info['render_width']),int(info['render_height'])],
                window_size=[int(kept[0]['window_pixel_width']),int(kept[0]['window_pixel_height'])],
                steps=int(mission['steps']),score=int(mission['score']),dropped_seconds=float(mission['dropped_seconds']),
                unpaced_target_accepted=mode=='off' and all(x['focused']=='1' for x in kept) and distribution([float(x['frame_ms']) for x in intervals])['p95']<=16.67))
    (args.folder/'window-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    for trial in summary:print(trial['vsync'],trial['repeat'],trial['interval_ms'])

if __name__=='__main__':main()
