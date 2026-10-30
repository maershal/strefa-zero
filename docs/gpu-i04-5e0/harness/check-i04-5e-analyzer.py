from pathlib import Path
import csv,importlib.util,json,sys
sys.dont_write_bytecode=True
r=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('stabilization',r/'outputs/strefa-zero/scripts/analyze-stabilization.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
d=r/'work/i04-5e-results/analyzer-fixtures'
results={}
for name,csv_request,native_request,effective,expected in (
    ('matching','passes','passes','passes',1),
    ('effective_mismatch','bounds','bounds','passes',0),
    ('missing_effective','bounds','bounds','',0),
    ('csv_native_conflict','passes','bounds','bounds',0),
):
    p=d/name;q=p/'test';q.mkdir(parents=True,exist_ok=True)
    with (p/'runs.csv').open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=['Name','Mode','CostMode']);writer.writeheader();writer.writerow({'Name':'test','Mode':'elapsed','CostMode':csv_request})
    (q/'run.json').write_text(json.dumps({'exit_code':0,'timed_out':False,'arguments':['--gpu-cost-profile','--gpu-cost-mode',native_request]}))
    (q/'environment.txt').write_text('gpu_timer_mode=elapsed\n'+('cost_mode='+effective+'\n' if effective else ''))
    rows,status=module.analyze(p,30)
    assert status[0]['run_valid']==expected,(name,status)
    results[name]={'status':'passed','run_valid':status[0]['run_valid'],'warnings':status[0]['warnings']}
(d.parent/'analyzer-fixture-checks.json').write_text(json.dumps(results,indent=2)+'\n')
print('Analyzer diagnostic-mode fixtures: 4/4 passed')
