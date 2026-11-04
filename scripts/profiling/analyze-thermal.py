"""Validate the I10 ABBA protocol of --gpu-thermal-benchmark (schema 2) and qualify every segment.

Schema 2: blocks are the outer loop and each block draws its own scene order (seed in every row). After each scene
of a block a profile segment records per-stage and per-kernel GPU time, dispatches and barriers. The gas stage does
not depend on the scene, so it serves as a clock probe: a profile segment whose gas median deviates from the median
of all segments by more than --gas-tolerance (10%) is disqualified together with the trials of that scene and block.
With telemetry.csv (GpuTelemetry.psm1, scripts/profiling/profile-thermal.ps1 -Telemetry) a trial or profile segment
is also disqualified when a hw_* or sw_thermal_slowdown reason was active, or when its median graphics clock is below
90% of the session maximum (--clock-fraction). The GTX 1650 laptop drops to 1395 MHz and half its power limit after
reaching 85 C with no throttle reason and stays there after cooling; the clock criterion catches that regime. With
telemetry the gas reference is the median of the segments that pass the telemetry criteria, so a run spent mostly in
the slow regime does not make the slow regime the reference.
"""
import argparse,csv,json,math,statistics
from collections import defaultdict
from pathlib import Path
SCENES=('cold','fire','oil','quench')
STAGES=('off','heat','heat_gas','full')
STAGE_FIELDS=('heat_ms','gas_ms','reaction_ms','cpu_ms')
DISQUALIFYING_REASONS=('hw_slowdown','hw_thermal_slowdown','hw_power_brake_slowdown','sw_thermal_slowdown')
def distribution(values):
    a=sorted(values)
    if not a or any(not math.isfinite(x)or x<0 for x in a):raise ValueError('Invalid timing samples')
    return dict(samples=len(a),median=statistics.median(a),p95=a[min(len(a)-1,int(.95*len(a)))],p99=a[min(len(a)-1,int(.99*len(a)))],maximum=a[-1])
def single_seed(rows,name):
    seeds={row['seed']for row in rows}
    if len(seeds)!=1:raise ValueError(f'{name}: expected one seed, found {len(seeds)}')
    return seeds.pop()
def summarize(rows,disqualified=frozenset()):
    """disqualified: set of (scene, block) whose trials are excluded from the acceptance decision."""
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
        if len({x['sequence']for x in values})!=1:raise ValueError('A trial spans several sequence numbers')
        scene,stage,block,order=key
        result=dict(scene=scene,stage=stage,block=block,order=order,sequence=int(values[0]['sequence']))
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
                    gates.append(dict(scene=s,stage=t,block=b,order=o,reference_medians_ms=refs,candidate_median_ms=value,additional_gpu_ms=delta,budget_ms=budget,accepted=delta<=budget,disqualified=(s,b)in disqualified))
    full=[g for g in gates if g['stage']=='full'and not g['disqualified']]
    hot=[x['additional_gpu_ms']for x in full if x['scene']!='cold'];cold=[x['additional_gpu_ms']for x in full if x['scene']=='cold']
    return dict(schema=2,samples=len(rows),segments=len(trials),iterations='I10 A/B/C plus D render',
                performance_accepted=bool(hot)and bool(cold)and all(x['accepted']for x in full),
                qualified_full_gates=len(full),worst_hot_additional_ms=max(hot,default=None),worst_cold_additional_ms=max(cold,default=None),gates=gates,trials=trials)
def merge_profiles(rows):
    """Join the CPU row (cpu_ms, dispatches, barriers) and the GPU row (heat/gas/reaction) of every profiled tick."""
    measured=defaultdict(dict)
    for row in rows:
        key=(row['scene'],int(row['block']),int(row['tick']))
        for field in STAGE_FIELDS:
            value=float(row[field])
            if not math.isfinite(value) or value<0:raise ValueError('Invalid stage timing')
            if value:
                if field in measured[key]:raise ValueError('Duplicate stage timing')
                measured[key][field]=value
        if float(row['cpu_ms']):measured[key]['dispatches']=int(row['dispatches']);measured[key]['barriers']=int(row['barriers'])
    segments=defaultdict(list)
    for (scene,block,_),values in measured.items():segments[scene,block].append(values)
    if set(segments)!={(s,b)for s in SCENES for b in(1,2,3)}:raise ValueError('Incomplete profile segments')
    for key,ticks in segments.items():
        if len(ticks)!=120 or any(not set(STAGE_FIELDS)<=set(v)or v.get('dispatches',0)<=0 for v in ticks):raise ValueError(f'Incomplete stage profile {key}')
    return segments
def qualify_segments(segments,tolerance,reference_keys=None):
    """Gas-stage clock probe: median gas_ms per segment against the median of the segment medians in reference_keys
    (default: all segments)."""
    medians={key:statistics.median(v['gas_ms']for v in ticks)for key,ticks in segments.items()}
    reference=statistics.median(m for key,m in medians.items()if reference_keys is None or key in reference_keys)
    if reference<=0:raise ValueError('Gas stage was not measured')
    return reference,{key:dict(gas_median_ms=m,deviation=m/reference-1,disqualified=abs(m/reference-1)>tolerance)for key,m in medians.items()}
def load_telemetry(folder):
    path=folder/'telemetry.csv'
    if not path.exists():return None
    return [r for r in csv.DictReader(path.open(encoding='utf-8-sig'))if r.get('utc_unix_ns')]
def telemetry_window(samples,start,end,session_max_mhz,clock_fraction):
    """Median graphics clock, maximum temperature and the disqualifying reasons of [start, end] (UTC ns)."""
    inside=[r for r in samples if start<=int(r['utc_unix_ns'])<=end]
    reasons={name for r in inside for name in DISQUALIFYING_REASONS if r.get('reason_'+name)=='1'}
    clocks=[float(r['graphics_mhz'])for r in inside if r.get('graphics_mhz')not in(None,'')]
    temperatures=[float(r['temperature_c'])for r in inside if r.get('temperature_c')not in(None,'')]
    median=statistics.median(clocks)if clocks else None
    if median is not None and median<clock_fraction*session_max_mhz:reasons.add('graphics_clock_below_session_max')
    return dict(samples=len(inside),graphics_mhz_median=median,temperature_c_max=max(temperatures,default=None),reasons=sorted(reasons))
def main():
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);parser.add_argument('--controls',action='store_true')
    parser.add_argument('--gas-tolerance',type=float,default=.10,help='allowed relative deviation of the gas stage median (default 0.10)')
    parser.add_argument('--clock-fraction',type=float,default=.90,help='telemetry: minimum median graphics clock as a fraction of the session maximum (default 0.90)')
    args=parser.parse_args()
    rows=list(csv.DictReader((args.folder/'thermal-samples.csv').open(encoding='utf-8-sig')))
    pools=list(csv.DictReader((args.folder/'thermal-pool-segments.csv').open(encoding='utf-8-sig')))
    if len(pools)!=192 or any(x['finite']!='1'for x in pools):raise ValueError('Invalid thermal segment state')
    profile_rows=list(csv.DictReader((args.folder/'thermal-pass-samples.csv').open(encoding='utf-8-sig')))
    seed=single_seed(rows,'thermal-samples.csv')
    if single_seed(pools,'thermal-pool-segments.csv')!=seed or single_seed(profile_rows,'thermal-pass-samples.csv')!=seed:raise ValueError('Seeds differ between files')
    segments=merge_profiles(profile_rows)
    samples=load_telemetry(args.folder);telemetry=None;telemetry_bad=set();reference_keys=None
    if samples:
        session_max=max(float(r['graphics_mhz'])for r in samples if r.get('graphics_mhz')not in(None,''))
        window=lambda row:telemetry_window(samples,int(row['start_utc_unix_ns']),int(row['end_utc_unix_ns']),session_max,args.clock_fraction)
        telemetry=dict(session_max_graphics_mhz=session_max,trials={int(p['sequence']):window(p)for p in pools},
                       profile_segments={f"{r['scene']}:{r['block']}":window(r)for r in csv.DictReader((args.folder/'thermal-profile-segments.csv').open(encoding='utf-8-sig'))})
        telemetry_bad={(p['scene'],int(p['block']))for p in pools if telemetry['trials'][int(p['sequence'])]['reasons']}
        telemetry_bad|={(k.split(':')[0],int(k.split(':')[1]))for k,v in telemetry['profile_segments'].items()if v['reasons']}
        reference_keys=set(segments)-telemetry_bad or None
    reference,qualification=qualify_segments(segments,args.gas_tolerance,reference_keys)
    disqualified={key for key,q in qualification.items()if q['disqualified']}|telemetry_bad
    result=summarize(rows,frozenset(disqualified))
    result['seed']=seed;result['gas_reference_median_ms']=reference;result['gas_tolerance']=args.gas_tolerance
    result['profile_segments']=[dict(scene=s,block=b,**q)for (s,b),q in sorted(qualification.items())]
    result['disqualified_segments']=sorted([list(k)for k in disqualified])
    result['gas_reference']='telemetry_qualified_segments'if reference_keys else'all_segments'
    if telemetry is not None:result['telemetry']=dict(telemetry,trials={str(k):v for k,v in sorted(telemetry['trials'].items())})
    result['stage_profiles']={}
    for scene in SCENES:
        ticks=[v for b in(1,2,3)if(scene,b)not in disqualified for v in segments[scene,b]]
        result['stage_profiles'][scene]=({field:distribution([v[field]for v in ticks])for field in STAGE_FIELDS+('dispatches','barriers')}
                                         if ticks else None)
    operations=defaultdict(list)
    path=args.folder/'thermal-op-samples.csv'
    for row in csv.DictReader(path.open(encoding='utf-8-sig'))if path.exists()else[]:
        if(row['scene'],int(row['block']))not in disqualified:operations[row['scene'],int(row['op'])].append(float(row['gpu_ms']))
    result['kernel_profiles']={scene:{str(op):distribution(v)for (s,op),v in sorted(operations.items())if s==scene}for scene in SCENES}
    (args.folder/'thermal-summary.json').write_text(json.dumps(result,indent=2)+'\n')
    if args.controls:
        altered=rows.copy();altered[0]=dict(altered[0],tick='1')
        cases={'missing':rows[1:],'duplicate':rows+[rows[0]],'bad_tick':altered,'bad_variant':[dict(rows[0],variant='bad')]+rows[1:],'nan_time':[dict(rows[0],physics_gpu_ms='nan')]+rows[1:],'negative_time':[dict(rows[0],cpu_submit_ms='-1')]+rows[1:],'bad_cadence':[dict(rows[0],rendered='1')]+rows[1:],'missing_frame':[rows[0],dict(rows[1],frame_gpu_ms='0')]+rows[2:]}
        checks={}
        for name,invalid in cases.items():
            try:summarize(invalid)
            except(ValueError,KeyError):checks[name]='PASS'
            else:raise ValueError('Control accepted '+name)
        # Gas stage of one segment slowed beyond twice the tolerance (a clock drop): that segment must be disqualified.
        target=('quench',3)
        slowed={key:[dict(v,gas_ms=v['gas_ms']*(1+3*args.gas_tolerance))for v in ticks]if key==target else ticks for key,ticks in segments.items()}
        if not qualify_segments(slowed,args.gas_tolerance)[1][target]['disqualified']:raise ValueError('Control accepted gas_outlier')
        checks['gas_outlier']='PASS'
        # 1395 MHz against a 1890 MHz session maximum, no throttle reason: the slow regime must be caught by the clock.
        slow=[dict(utc_unix_ns=str(n),graphics_mhz='1395',temperature_c='64')for n in(1,2)]
        if 'graphics_clock_below_session_max'not in telemetry_window(slow,0,3,1890,args.clock_fraction)['reasons']:raise ValueError('Control accepted slow_clock')
        checks['slow_clock']='PASS'
        try:single_seed([dict(rows[0],seed=rows[0]['seed']+'1')]+rows[1:],'control')
        except ValueError:checks['mixed_seed']='PASS'
        else:raise ValueError('Control accepted mixed_seed')
        (args.folder/'analysis-controls.json').write_text(json.dumps(checks,indent=2)+'\n')
    print(json.dumps({k:result[k]for k in('seed','samples','segments','performance_accepted','qualified_full_gates','worst_hot_additional_ms','worst_cold_additional_ms','disqualified_segments')},indent=2))
if __name__=='__main__':main()
