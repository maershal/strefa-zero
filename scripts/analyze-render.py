"""Validate the fixed I08 matrix and summarize independent trial distributions."""
from pathlib import Path
import argparse,csv,json,statistics,math
from collections import defaultdict

def distribution(values):
    if any(not math.isfinite(v) or v<0 for v in values):raise ValueError('Nonfinite or negative timing')
    a=sorted(values)
    if not a: raise ValueError('No measurements')
    return dict(samples=len(a),median=statistics.median(a),p95=a[min(len(a)-1,int(len(a)*.95))],p99=a[min(len(a)-1,int(len(a)*.99))],maximum=a[-1])

def analyze(folder):
    rows=list(csv.DictReader((folder/'render-samples.csv').open()))
    groups=defaultdict(list)
    for row in rows:groups[(int(row['scale']),row['scene'],row['variant'],int(row['block']),int(row['order']))].append(row)
    expected=2*3*3*3*4
    keys={(scale,scene,quality if order in [1,2] else 'reference_for_'+quality,block,order)
          for scale in [3,5] for scene in ['mission','sand_water','oil_water']
          for quality in ['basic','standard','high'] for block in [1,2,3] for order in range(4)}
    if set(groups)!=keys or len(groups)!=expected or any(len(v)!=240 for v in groups.values()):raise ValueError('Incomplete or mislabeled ABBA matrix')
    if any(len({int(row['frame']) for row in values})!=240 for values in groups.values()):raise ValueError('Duplicate render frame')
    results=[]
    for (scale,scene,variant,block,order),values in sorted(groups.items()):
        item=dict(scale=scale,scene=scene,variant=variant,block=block,order=order,memory_bytes=int(values[0]['memory_bytes']))
        for field in ['scene_ms','splat_ms','filter_x_ms','filter_y_ms','thickness_ms','composition_ms','foreground_ms','total_ms','water_ms','cpu_submit_ms','terrain_draw_ms']:
            item[field]=distribution([float(v[field]) for v in values])
        results.append(item)
    budgets={}
    for quality in ['basic','standard','high']:
        blocks=[]
        for block in range(1,4):
            scenes={}
            for scene in ['mission','sand_water','oil_water']:
                trials=[x for x in results if x['scale']==3 and x['scene']==scene and x['variant']==quality and x['block']==block]
                medians=[x['water_ms']['median'] for x in trials]
                scenes[scene]=dict(trial_medians_ms=medians,accepted=all(v<=.30 for v in medians))
            blocks.append(dict(block=block,scenes=scenes,accepted=all(v['accepted'] for v in scenes.values())))
        budgets[quality]=dict(blocks=blocks,accepted=all(b['accepted'] for b in blocks))
    default=next((q for q in ['high','standard','basic'] if budgets[q]['accepted']),'basic')
    summary=dict(samples=len(rows),segments=len(groups),budget_ms=.30,default=default,performance_accepted=budgets[default]['accepted'],budgets=budgets,trials=results)
    terrain=list(csv.DictReader((folder/'terrain-update.csv').open()))
    summary['terrain_dirty_preprocess_ms']=distribution([float(x['full_dirty_preprocess_ms']) for x in terrain])
    (folder/'render-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({k:summary[k] for k in ['samples','segments','default','performance_accepted']},indent=2))
    return summary

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);analyze(parser.parse_args().folder)
