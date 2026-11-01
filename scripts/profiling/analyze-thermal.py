"""Validate the complete I10 ABBA protocol and qualify all full-stage segments."""
import argparse,csv,json,math,statistics
from collections import defaultdict
from pathlib import Path
SCENES=('cold','fire','oil','quench')
STAGES=('off','heat','heat_gas','full')
def distribution(values):
    a=sorted(values)
    if not a or any(not math.isfinite(x)or x<0 for x in a):raise ValueError('Invalid timing samples')
    return dict(samples=len(a),median=statistics.median(a),p95=a[min(len(a)-1,int(.95*len(a)))],p99=a[min(len(a)-1,int(.99*len(a)))],maximum=a[-1])
def summarize(rows):
    groups=defaultdict(list)
    for row in rows:
        key=(row['scene'],row['stage'],int(row['block']),int(row['order']))
        variant=row['stage']if int(row['order'])in(1,2)else'off'
        if row['variant']!=variant:raise ValueError('Invalid actual variant')
        if int(row['rendered'])!=int(row['tick'])%2:raise ValueError('Invalid render cadence')
        if int(row['tick'])%2 and float(row['frame_gpu_ms'])<=0:raise ValueError('Missing full-frame measurement')
        groups[key].append(row)
    expected={(s,t,b,o)for s in SCENES for t in STAGES for b in(1,2,3)for o in range(4)}
    if set(groups)!=expected:raise ValueError('Incomplete ABBA matrix')
    trials=[];index={}
    for key,values in sorted(groups.items()):
        if len(values)!=240 or {int(x['tick'])for x in values}!=set(range(240)):raise ValueError('Missing or duplicate tick')
        scene,stage,block,order=key
        result=dict(scene=scene,stage=stage,block=block,order=order)
        for metric in('physics_gpu_ms','cpu_submit_ms'):
            result[metric]=distribution([float(x[metric])for x in values])
        for metric in('render_gpu_ms','frame_gpu_ms'):
            result[metric]=distribution([float(x[metric])for x in values if x['rendered']=='1'])
        ordered=sorted(values,key=lambda x:int(x['tick']))
        result['cpu_frame_submit_ms']=distribution([float(ordered[i]['cpu_submit_ms'])+float(ordered[i+1]['cpu_submit_ms'])for i in range(0,240,2)])
        trials.append(result);index[key]=result
    gates=[]
    for s in SCENES:
        for t in STAGES:
            for b in(1,2,3):
                refs=[index[s,t,b,o]['physics_gpu_ms']['median']for o in(0,3)]
                for o in(1,2):
                    value=index[s,t,b,o]['physics_gpu_ms']['median'];delta=value-statistics.mean(refs);budget=.05 if s=='cold'else .70
                    gates.append(dict(scene=s,stage=t,block=b,order=o,reference_medians_ms=refs,candidate_median_ms=value,additional_gpu_ms=delta,budget_ms=budget,accepted=delta<=budget))
    full=[g for g in gates if g['stage']=='full']
    return dict(schema=1,samples=len(rows),segments=len(trials),iterations='I10 A/B/C plus D render',performance_accepted=all(x['accepted']for x in full),worst_hot_additional_ms=max(x['additional_gpu_ms']for x in full if x['scene']!='cold'),worst_cold_additional_ms=max(x['additional_gpu_ms']for x in full if x['scene']=='cold'),gates=gates,trials=trials)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);parser.add_argument('--controls',action='store_true');args=parser.parse_args()
    rows=list(csv.DictReader((args.folder/'thermal-samples.csv').open(encoding='utf-8-sig')));result=summarize(rows)
    pools=list(csv.DictReader((args.folder/'thermal-pool-segments.csv').open(encoding='utf-8-sig')))
    if len(pools)!=192 or any(x['finite']!='1'for x in pools):raise ValueError('Invalid thermal segment state')
    # The asynchronous producer records submit and GPU completion separately.
    # Join by tick; zeros in the submit record are not GPU measurements.
    measured=defaultdict(dict)
    for row in csv.DictReader((args.folder/'thermal-pass-samples.csv').open(encoding='utf-8-sig')):
        key=(row['scene'],int(row['tick']))
        for field in ('heat_ms','gas_ms','reaction_ms','cpu_ms'):
            value=float(row[field])
            if not math.isfinite(value) or value<0:raise ValueError('Invalid stage timing')
            if value:
                if field in measured[key]:raise ValueError('Duplicate stage timing')
                measured[key][field]=value
    result['stage_profiles']={}
    for scene in SCENES:
        ticks=[v for (s,_),v in measured.items() if s==scene]
        if len(ticks)!=120 or any(len(v)!=4 for v in ticks):raise ValueError('Incomplete stage profile')
        result['stage_profiles'][scene]={field:distribution([v[field] for v in ticks])for field in ('heat_ms','gas_ms','reaction_ms','cpu_ms')}
    (args.folder/'thermal-summary.json').write_text(json.dumps(result,indent=2)+'\n')
    if args.controls:
        altered=rows.copy();altered[0]=dict(altered[0],tick='1')
        cases={'missing':rows[1:],'duplicate':rows+[rows[0]],'bad_tick':altered,'bad_variant':[dict(rows[0],variant='bad')]+rows[1:],'nan_time':[dict(rows[0],physics_gpu_ms='nan')]+rows[1:],'negative_time':[dict(rows[0],cpu_submit_ms='-1')]+rows[1:],'bad_cadence':[dict(rows[0],rendered='1')]+rows[1:],'missing_frame':[rows[0],dict(rows[1],frame_gpu_ms='0')]+rows[2:]}
        checks={}
        for name,invalid in cases.items():
            try:summarize(invalid)
            except(ValueError,KeyError):checks[name]='PASS'
            else:raise ValueError('Control accepted '+name)
        (args.folder/'analysis-controls.json').write_text(json.dumps(checks,indent=2)+'\n')
    print(json.dumps({k:result[k]for k in('samples','segments','performance_accepted','worst_hot_additional_ms','worst_cold_additional_ms')},indent=2))
if __name__=='__main__':main()
