"""Validate complete I09 ABBA blocks; qualify every low-quality candidate segment."""
from pathlib import Path
from collections import defaultdict
import argparse,csv,json,math,statistics

SCENES=('mission','dam_break','sand_water','saturated')
METRICS=('total_gpu_ms','update_gpu_ms','render_gpu_ms','cpu_submit_ms')

def distribution(values):
    if not values or any(not math.isfinite(x) or x<0 for x in values):
        raise ValueError('Missing, negative or nonfinite timing')
    a=sorted(values)
    return dict(samples=len(a),median=statistics.median(a),p95=a[min(len(a)-1,int(.95*len(a)))],p99=a[min(len(a)-1,int(.99*len(a)))],maximum=a[-1])

def summarize(rows):
    groups=defaultdict(list)
    for row in rows:
        key=(row['scene'],int(row['scale']),row['quality'],int(row['block']),int(row['order']))
        expected_variant=row['quality'] if int(row['order']) in (1,2) else 'off'
        if row.get('variant',expected_variant)!=expected_variant:raise ValueError('Invalid actual VFX variant')
        alive,free=int(row['alive']),int(row['free'])
        if alive<0 or free<0 or alive+free!=16384:raise ValueError('Invalid pool partition counters')
        drawn=int(row.get('drawn',alive))
        if drawn<0 or drawn>alive:raise ValueError('Invalid draw counters')
        groups[key].append(row)
    expected={(scene,scale,quality,block,order) for scene in SCENES for scale in (3,5) for quality in ('off','low','high') for block in (1,2,3) for order in range(4)}
    if set(groups)!=expected:raise ValueError('Incomplete or mislabeled ABBA matrix')
    trials=[];indexed={}
    for key,values in sorted(groups.items()):
        scene,scale,quality,block,order=key
        if len(values)!=240 or {int(x['frame']) for x in values}!=set(range(240)):
            raise ValueError('Missing or duplicate frame')
        if scene=='saturated' and quality!='off' and order in (1,2) and any(int(x['alive'])!=16384 for x in values):
            raise ValueError('Saturated candidate is not full')
        result=dict(scene=scene,scale=scale,quality=quality,block=block,order=order)
        for metric in METRICS:result[metric]=distribution([float(x[metric]) for x in values])
        trials.append(result);indexed[key]=result
    gates=[]
    for scene in SCENES:
        for scale in (3,5):
            for quality in ('off','low','high'):
                for block in (1,2,3):
                    references=[indexed[(scene,scale,quality,block,i)]['total_gpu_ms']['median'] for i in (0,3)]
                    baseline=statistics.mean(references)
                    for order in (1,2):
                        candidate=indexed[(scene,scale,quality,block,order)]
                        delta=candidate['total_gpu_ms']['median']-baseline
                        gates.append(dict(scene=scene,scale=scale,quality=quality,block=block,order=order,reference_medians_ms=references,candidate_median_ms=candidate['total_gpu_ms']['median'],additional_gpu_ms=delta,accepted=delta<=.25))
    low=[x for x in gates if x['scale']==3 and x['quality']=='low']
    return dict(schema=1,samples=len(rows),segments=len(trials),budget_ms=.25,default='low',performance_accepted=all(x['accepted'] for x in low),worst_low_additional_gpu_ms=max(x['additional_gpu_ms'] for x in low),gates=gates,trials=trials)

def controls(rows):
    results={}
    cases={
        'missing_sample':rows[1:],
        'duplicate_sample':[rows[0]]+rows,
        'duplicate_frame':[dict(rows[0],frame=rows[1]['frame'])]+rows[1:],
        'bad_label':[dict(rows[0],quality='invalid')]+rows[1:],
        'bad_variant':[dict(rows[0],variant='invalid')]+rows[1:],
        'bad_pool_count':[dict(rows[0],free='-1')]+rows[1:],
        'nan_time':[dict(rows[0],total_gpu_ms='nan')]+rows[1:],
        'negative_time':[dict(rows[0],update_gpu_ms='-1')]+rows[1:],
    }
    saturated=next(i for i,r in enumerate(rows) if r['scene']=='saturated' and r['quality']=='low' and r['order']=='1')
    invalid=rows.copy();invalid[saturated]=dict(invalid[saturated],alive='16383');cases['not_full']=invalid
    for name,invalid in cases.items():
        try:summarize(invalid)
        except (ValueError,KeyError):results[name]='PASS'
        else:raise ValueError('Analyzer accepted '+name)
    return results

def main():
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);parser.add_argument('--controls',action='store_true');args=parser.parse_args()
    rows=list(csv.DictReader((args.folder/'vfx-samples.csv').open(encoding='utf-8-sig')))
    result=summarize(rows);(args.folder/'vfx-summary.json').write_text(json.dumps(result,indent=2)+'\n')
    if args.controls:(args.folder/'analysis-controls.json').write_text(json.dumps(controls(rows),indent=2)+'\n')
    passes=args.folder/'vfx-pass-samples.csv'
    if passes.exists():
        # Pair the two update ticks and all render-layer calls of the same frame.
        # Their sum excludes gaps between passes and cannot qualify the main budget.
        grouped=defaultdict(lambda:defaultdict(float))
        for row in csv.DictReader(passes.open(encoding='utf-8-sig')):
            key=(row['scene'],(int(row['tick'])+1)//2)
            for metric in ('field_ms','update_ms','emit_ms','draw_ms'):
                grouped[key][metric]+=float(row[metric])
        metrics=('field_ms','update_ms','emit_ms','draw_ms')
        with (args.folder/'vfx-frame-stages.csv').open('w',newline='') as stream:
            writer=csv.writer(stream);writer.writerow(('scene','frame',*metrics,'sum_without_gaps_ms'))
            for (scene,frame),values in sorted(grouped.items()):writer.writerow((scene,frame,*(values[m] for m in metrics),sum(values.values())))
        details={scene:{m:distribution([values[m] for (s,_),values in grouped.items() if s==scene]) for m in metrics} for scene in SCENES}
        (args.folder/'vfx-stage-summary.json').write_text(json.dumps(details,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('samples','segments','default','performance_accepted','worst_low_additional_gpu_ms')},indent=2))

if __name__=='__main__':main()
