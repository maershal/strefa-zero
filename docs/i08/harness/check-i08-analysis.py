from pathlib import Path
import csv,json,importlib.util,tempfile,shutil,contextlib,io
r=Path(__file__).resolve().parent.parent;d=r/'work/i08-results';script=r/'outputs/strefa-zero/scripts/analyze-render.py'
spec=importlib.util.spec_from_file_location('analysis',script);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
rows=list(csv.DictReader((d/'final-benchmark/render-samples.csv').open()));fields=list(rows[0]);results=[]
with tempfile.TemporaryDirectory(prefix='strefa-i08-analysis-') as name:
 folder=Path(name);shutil.copy2(d/'final-benchmark/terrain-update.csv',folder/'terrain-update.csv')
 def run(case,data,valid=False):
  with (folder/'render-samples.csv').open('w') as f:
   writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(data)
  try:
   with contextlib.redirect_stdout(io.StringIO()):summary=m.analyze(folder)
  except (ValueError,KeyError) as error:
   assert not valid,(case,str(error));results.append({'case':case,'rejected':True,'reason':str(error)});return
  assert valid,case
  assert summary==json.loads((d/'final-benchmark/render-summary.json').read_text())
  results.append({'case':case,'accepted':True,'identical_to_original_summary':True})
 run('complete',rows,True)
 run('missing_sample',rows[:-1])
 broken=[dict(x) for x in rows];broken[0]['frame']=broken[1]['frame'];run('duplicate_frame',broken)
 broken=[dict(x) for x in rows];broken[0]['total_ms']='nan';run('nonfinite',broken)
 broken=[dict(x) for x in rows];broken[1]['water_ms']='-0.1';run('negative_time',broken)
 broken=[dict(x) for x in rows]
 for row in broken:
  if row['variant']=='high':row['variant']='wrong_quality'
 run('missing_quality_with_same_row_count',broken)
 broken=[dict(x) for x in rows];broken[0]['order']='9';run('bad_order',broken)
(d/'analysis-controls.json').write_text(json.dumps(results,indent=2)+'\n')
print('Analysis controls:',len(results),'passed')
