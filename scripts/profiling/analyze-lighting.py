#!/usr/bin/env python3
"""Validate the complete I11 ABBA matrix before computing any acceptance result."""
import argparse,csv,json,math,statistics,tempfile
from pathlib import Path
SCENES=('day','night','fire','explosion','water','sand')
def percentile(values,p):
    ordered=sorted(values);return ordered[min(len(ordered)-1,int((len(ordered)-1)*p))]
def analyze(path):
    groups={};seen=set()
    with path.open(newline='',encoding='utf-8-sig') as stream:
        for row in csv.DictReader(stream):
            scene,quality=row['scene'],row['quality'];block,order,frame=(int(row[k]) for k in ('block','order','frame'))
            if scene not in SCENES or quality not in ('low','high','off_for_low','off_for_high') or block not in (1,2,3) or order not in range(4) or frame not in range(240):raise ValueError('invalid identity')
            if (quality.startswith('off_for_'))!=(order in (0,3)):raise ValueError('invalid ABBA order')
            key=(scene,quality,block,order);identity=key+(frame,)
            if identity in seen:raise ValueError('duplicate frame')
            seen.add(identity);gpu,cpu=float(row['pair_gpu_ms']),float(row['cpu_submit_ms'])
            if not all(math.isfinite(v) and v>=0 for v in (gpu,cpu)) or int(row['memory_bytes'])<=0:raise ValueError('invalid measurement')
            groups.setdefault(key,[]).append((gpu,cpu))
    expected={(s,q if o in (1,2) else 'off_for_'+q,b,o) for s in SCENES for q in ('low','high') for b in (1,2,3) for o in range(4)}
    if set(groups)!=expected or any(len(g)!=240 for g in groups.values()):raise ValueError('incomplete matrix')
    result={'schema':1,'frames':len(seen),'segments':len(groups),'budget_ms':.35,'measurement':'candidate: two presentation ticks and render; reference: compatible render with I11 ticks bypassed; frozen physics; no window or present','segments_detail':[],'comparisons':[]}
    for key,rows in groups.items():
        values=[r[0] for r in rows];cpu=[r[1] for r in rows]
        result['segments_detail'].append(dict(zip(('scene','quality','block','order'),key),median_ms=statistics.median(values),p95_ms=percentile(values,.95),p99_ms=percentile(values,.99),cpu_median_ms=statistics.median(cpu)))
    for scene in SCENES:
        for quality in ('low','high'):
            for block in (1,2,3):
                reference=statistics.mean(statistics.median(r[0] for r in groups[(scene,'off_for_'+quality,block,o)]) for o in (0,3))
                for order in (1,2):
                    delta=statistics.median(r[0] for r in groups[(scene,quality,block,order)])-reference
                    result['comparisons'].append({'scene':scene,'quality':quality,'block':block,'order':order,'extra_ms':delta,'pass':delta<=.35})
    result['low_budget_pass']=all(r['pass'] for r in result['comparisons'] if r['quality']=='low')
    result['window_acceptance']='separate; requires focused visible windows, full frame timing and no dropped time'
    result['default']='off until both module and window acceptance pass'
    return result

def analyze_passes(path):
    names=('presentation_tick','shadow_rebuild','lighting','composition','bloom','postprocess','total')
    groups={};seen=set()
    with path.open(newline='',encoding='utf-8-sig') as stream:
        for row in csv.DictReader(stream):
            key=(row['scene'],row['quality'],int(row['block']),int(row['order']),row['kind'])
            identity=key+(int(row['serial']),)
            if identity in seen:raise ValueError('duplicate pass sample')
            seen.add(identity)
            values=[float(row[name+'_ms']) for name in names]
            if not all(math.isfinite(v) and v>=0 for v in values) or sum(values[:-1])>values[-1]+.00002:raise ValueError('invalid pass timing')
            if (row['kind']=='tick' and any(values[1:-1])) or (row['kind']=='render' and values[0]!=0):raise ValueError('mixed pass scopes')
            groups[key]=groups.get(key,0)+1
    expected={(s,q,b,o,k):480 if k=='tick' else 240 for s in SCENES for q in ('low','high') for b in (1,2,3) for o in (1,2) for k in ('tick','render')}
    if groups!=expected:raise ValueError('incomplete pass matrix')
    return {'samples':len(seen),'complete':True,'composition_and_bloom_separate':True}

def controls():
    fields=('scene','quality','block','order','frame','pair_gpu_ms','cpu_submit_ms','memory_bytes')
    rows=[dict(zip(fields,(s,q if o in (1,2) else 'off_for_'+q,b,o,f,.4 if o in (1,2) else .2,.01,100))) for s in SCENES for q in ('low','high') for b in (1,2,3) for o in range(4) for f in range(240)]
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'input.csv'
        def write(data):
            with p.open('w',newline='') as stream:w=csv.DictWriter(stream,fieldnames=fields);w.writeheader();w.writerows(data)
        write(rows);assert analyze(p)['low_budget_pass'];passed=[]
        variants={'missing':rows[:-1],'duplicate':rows+[rows[0]],'negative':[dict(rows[0],pair_gpu_ms=-1)]+rows[1:],'nonfinite':[dict(rows[0],pair_gpu_ms='nan')]+rows[1:],'label':[dict(rows[0],scene='unknown')]+rows[1:],'order':[dict(rows[0],order=2)]+rows[1:],'memory':[dict(rows[0],memory_bytes=0)]+rows[1:]}
        for name,data in variants.items():
            write(data)
            try:analyze(p)
            except ValueError:passed.append(name)
            else:raise AssertionError('guard failed: '+name)
        return {'positive':'PASS','rejected':passed}
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path,nargs='?');parser.add_argument('--controls',action='store_true');args=parser.parse_args()
    if args.controls:
        value=controls();print(json.dumps(value,indent=2))
        if args.directory:(args.directory/'lighting-analysis-controls.json').write_text(json.dumps(value,indent=2)+'\n')
    if args.directory and (args.directory/'lighting-samples.csv').exists():
        result=analyze(args.directory/'lighting-samples.csv')
        if (args.directory/'lighting-passes.csv').exists():result['per_pass_validation']=analyze_passes(args.directory/'lighting-passes.csv')
        (args.directory/'lighting-summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ('segments_detail','comparisons')},indent=2))
