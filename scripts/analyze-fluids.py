"""Summarize immutable I07 trials; never discard failures or promote defaults."""
import argparse,csv,json,statistics
from pathlib import Path

def rows(path):
    with path.open(encoding='utf-8-sig',newline='') as file:return list(csv.DictReader(file))

def analyze(root):
    result={'schema':1,'scene_names':{'4':'water_tank','5':'dam_break','6':'connected_vessels','7':'oil_water','8':'sand_water','10':'inverted_oil','11':'oil_drop'},'quality':{},'benchmark':{},'failures':[]}
    for folder in sorted(p for p in root.iterdir() if p.is_dir()):
        status=next((folder/name for name in ['run-result.json','native-result.json'] if (folder/name).exists()),None)
        if status:
            process=json.loads(status.read_text(encoding='utf-8-sig'))
            if process.get('exit_code')!=0 or process.get('timed_out'):result['failures'].append({'trial':folder.name,'process':process})
        quality=folder/'fluid-quality.csv'
        if quality.exists():
            grouped={}
            for row in rows(quality):
                if int(row['tick'])>=600:grouped.setdefault(row['scene'],[]).append(row)
            result['quality'][folder.name]={scene:{
                'late_checkpoints':len(data),
                'compression_max':max(float(r['compression']) for r in data),
                'surface_spatial_rms_mean':statistics.mean(float(r['surface_spatial_rms']) for r in data),
                'surface_temporal_amplitude':max(float(r['surface_mean']) for r in data)-min(float(r['surface_mean']) for r in data),
                'cluster_fraction_mean':statistics.mean(float(r['cluster_fraction']) for r in data),
                'kinetic_energy_mean':statistics.mean(float(r['kinetic_energy']) for r in data),
                'px_final':float(data[-1]['px']),'py_final':float(data[-1]['py']),
            } for scene,data in grouped.items()}
        benchmark=folder/'fluid-benchmark.csv'
        if benchmark.exists():result['benchmark'][folder.name]=rows(benchmark)
    result['abba_blocks']=[]
    for block in range(1,4):
        names=[f'block-{block}-{ordinal}-{preset}' for ordinal,preset in enumerate(['legacy','balanced','balanced','legacy'],1)]
        if all(name in result['benchmark'] for name in names):
            trials=[{row['scene']:row for row in result['benchmark'][name]} for name in names]
            scenes={}
            for scene in trials[0]:
                legacy=statistics.mean(float(trials[i][scene]['gpu_median_ms']) for i in [0,3])
                balanced=statistics.mean(float(trials[i][scene]['gpu_median_ms']) for i in [1,2])
                scenes[scene]={'legacy_mean_of_medians':legacy,'balanced_mean_of_medians':balanced,'delta_ms':balanced-legacy}
            result['abba_blocks'].append({'block':block,'scenes':scenes})
    a=result['quality'].get('legacy-a',{});b=result['quality'].get('legacy-b',{})
    result['legacy_envelope']={scene:{key:[min(a[scene][key],b[scene][key]),max(a[scene][key],b[scene][key])] for key in a[scene] if key not in ['px_final','py_final','late_checkpoints']} for scene in a.keys()&b.keys()}
    result['default_selection']='manual gates required: full historical integration, improvement beyond legacy envelope and mission median delta <=0.25ms; raw regime changes retained'
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();text=json.dumps(analyze(args.directory),indent=2,allow_nan=False)+'\n'
    if args.output:args.output.write_text(text,encoding='utf-8')
    else:print(text,end='')
