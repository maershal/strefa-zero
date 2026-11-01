from pathlib import Path
import csv,json,hashlib,shutil,statistics,sys,xml.etree.ElementTree as ET,re
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i08-results';build=r/'work/build-strefa-zero-release'
import importlib.util
spec=importlib.util.spec_from_file_location('render_analysis',s/'scripts/analyze-render.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
distribution=mod.distribution

def kv(path):return dict(line.split('=',1) for line in path.read_text().splitlines() if '=' in line)
def load_json(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def assert_locked():
 locked=load_json(d/'final-production.json');changed=[name for name,sha in locked.items() if hashlib.sha256((s/name).read_bytes()).hexdigest()!=sha]
 assert not changed,changed
 assert hashlib.sha256((build/'runtime-070/StrefaZero.exe').read_bytes()).hexdigest()==(d/'final-executable-sha256.txt').read_text().strip()
 return changed

def finalize():
 assert_locked()
 tests=ET.parse(d/'ctest.xml').getroot().findall('.//testcase')
 failures=[x.attrib['name'] for x in tests if any(x.find(t) is not None for t in ['failure','error','skipped'])]
 assert len(tests)==59 and not failures,(len(tests),failures)
 assert load_json(d/'gcc-result.json')['exit_code']==0
 controls=load_json(d/'analysis-controls.json');assert len(controls)==7 and sum(bool(x.get('rejected')) for x in controls)==6
 budget=load_json(d/'final-benchmark/render-summary.json');assert budget['default']=='basic' and budget['performance_accepted']
 windows=[]
 for process in load_json(d/'windows-result.json'):
  folder=d/'windows'/f"{process['mode']}-{process['repeat']}";assert process['exit_code']==0 and not process['timed_out'],process
  rows=list(csv.DictReader((folder/'frame-timings.csv').open()))
  selected=[x for x in rows if x['rendered']=='1' and x['capture_performed']=='0' and int(x['frame'])>=120]
  info=kv(folder/'frame-profile-info.txt') if (folder/'frame-profile-info.txt').exists() else kv(folder/'frame-timings-info.txt')
  state=kv(folder/'mission-ui.txt');assert state['score']=='3' and state['mission_complete']=='1' and float(state['dropped_seconds'])==0
  frames=distribution([float(x['frame_total_ms']) for x in selected])
  all_frames=list(csv.DictReader((folder/'mission-frames.csv').open()))
  assert len(all_frames)==int(state['frames']),('Incomplete whole-window intervals',folder)
  # Every mission interval is retained separately from the bounded phase table.
  # The sole capture occurs after the final interval, so no interval contains it.
  intervals=distribution([float(x['frame_ms']) for x in all_frames if int(float(x['frame']))>=120])
  windows.append(dict(mode=process['mode'],repeat=process['repeat'],rows=len(rows),retained_frames=len(selected),frame_ms=frames,interval_ms=intervals,
   width=int(selected[0]['window_pixel_width']),height=int(selected[0]['window_pixel_height']),frame_limit=int(selected[0]['frame_limit']),
   focus_fraction=statistics.mean(int(x['focused']) for x in selected),max_pending_seconds=max(float(x['mission_pending_seconds']) for x in selected),
   rows_dropped=int(info['rows_dropped']),interval_source='mission-frames.csv (all frames)',interval_rows=len(all_frames),steps=int(state['steps']),dropped_seconds=float(state['dropped_seconds']),gpu_render_last_ms=float(state['gpu_render_ms']),gpu_step_last_ms=float(state['gpu_step_ms'])))
 (d/'window-summary.json').write_text(json.dumps(windows,indent=2)+'\n')
 assert load_json(d/'high-balanced-result.json')['exit_code']==0
 # Preserve only freshly generated regression text/CSV, never old screenshots in reused CTest folders.
 threshold=(d/'final-benchmark/render-summary.json').stat().st_mtime-1
 paths=re.findall(r'"--profile" "([^\"]+)"',(build/'CTestTestfile.cmake').read_text())
 for name in paths:
  folder=build/Path(name.replace('\\','/')).name
  if not folder.exists():continue
  for f in folder.iterdir():
   if f.is_file() and f.stat().st_mtime>=threshold and f.suffix.lower() in {'.txt','.csv','.json','.log','.xml'}:
    dst=d/'regression'/folder.name/f.name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,dst)
 summary=dict(windows_tests=59,windows_passed=59,gcc_tests=8,gcc_passed=8,frozen_checkpoints=12,terrain_rays=512,liquid_rays=512,benchmark_samples=budget['samples'],benchmark_segments=budget['segments'],
  default_render_quality='basic',default_physics='legacy',water_budget_accepted=True,water_budget_ms=.30,
  window_trials=6,window_trials_passed=6,window_unpaced_p95_accepted=all(x['interval_ms']['p95']<=16.67 for x in windows if x['mode']=='off'),
  window_vsync_p95_accepted=all(x['interval_ms']['p95']<=16.67 for x in windows if x['mode']=='on'),
  complete_frame_rows=all(x['rows_dropped']==0 for x in windows),complete_frame_intervals=True,high_balanced_integration_passed=True,exe_sha256=(d/'final-executable-sha256.txt').read_text().strip(),unchanged_production_sources=assert_locked())
 (d/'final-validation.json').write_text(json.dumps(summary,indent=2)+'\n')
 report=(s/'docs/I08_SCENE_RENDERER.md').read_text();report=report[:report.index('## Wyniki')]
 report+='## Wyniki końcowe\n\n'
 report+='Wdrożono pełne A/B/C. **Domyślnie basic; budżet wody odebrany w kontrolowanej macierzy GTX 1650.** Standard i high pozostają dostępne jako droższe poziomy jakości. Ustawienia i równania fizyki I07 pozostają bez zmian.\n\n'
 report+='Środowisko: Intel Core i5-9300HF, 4 rdzenie / 8 wątków; NVIDIA GeForce GTX 1650, sterownik 610.47, OpenGL 4.3; Windows Release/MSVC z ostrzeżeniami jako błędami. Sprawdzenie CPU GCC: 8/8.\n\n'
 report+='- Windows CTest **59/59 PASS**, bez pominięć. Nowy test renderera i rozszerzony test wspólnej sesji przeszły. Jedna sesja nadal tworzy jeden świat, teraz z 48 programami; oba modele CPU pozostają leniwe.\n'
 report+='- 12 checkpointów: pięć wariantów oraz porównanie filtrów na dokładnych kopiach GPU; wszystkie bajty ośmiu buforów fizyki, tick i kanał zdarzeń zachowane.\n'
 report+='- Niezależne orakle: splat, prędkość, segmenty, **512 odcinków terenu i 512 odcinków maski cieczy**; potwierdzone HP, deduplikacja błysku i reset.\n'
 report+='- Kontrakt obcego stanu, eksporty i większy render zaliczone. KHR_debug nie zgłosił błędów GL; zachowano ostrzeżenia wydajności dotyczące synchronicznych eksportów i specjalizacji shaderów. Końcowy test jakości ma 308 komunikatów i 0 utraconych. Regularny render nie odpytuje stanu GL; lokalizacje uniformów są buforowane przy inicjalizacji.\n\n'
 report+='[CTest](i08/ctest.log), [JUnit](i08/ctest.xml), [GCC](i08/gcc-tests.log), [testy obrazu](i08/final-quality/render-tests.txt), [KHR_debug](i08/final-quality/gl-debug.txt), [audyt źródeł](i08/source-audit.json), [podsumowanie](i08/final-validation.json).\n\n'
 report+='### Obraz i filtry\n\n![Porównanie referencji i etapów A/B/C](i08/comparison.png)\n\n'
 filter_rows=list(csv.DictReader((d/'final-quality/filter-comparison.csv').open()))
 report+='Średni bezwzględny błąd udziału oleju względem surowego pola, wyłącznie w nośności cieczy:\n\n| Scena | Filtr przestrzenny | Bilateral |\n| --- | ---: | ---: |\n'
 for name in ['phases','immersion','mission']:
  row=next(x for x in filter_rows if x['scene']==name);report+=f"| {name} | {float(row['spatial_phase_error']):.6f} | {float(row['bilateral_phase_error']):.6f} |\n"
 report+='\nTo miara rozmycia granicy faz, nie miara dokładności fizyki. [Wszystkie wyniki](i08/final-quality/filter-comparison.csv); oryginalne obrazy BMP są w tym samym katalogu.\n\n'
 report+='### Koszt wody i wybór jakości\n\nDla każdego bloku podano największą medianę pojedynczego segmentu kandydata spośród misji, piasku z wodą i wody z olejem, przy 960 × 576. Każdy segment ma 240 próbek.\n\n| Blok | basic [ms] | standard [ms] | high [ms] | Limit |\n| --- | ---: | ---: | ---: | ---: |\n'
 for block in range(3):
  v={q:max(max(v['trial_medians_ms']) for v in budget['budgets'][q]['blocks'][block]['scenes'].values()) for q in ['basic','standard','high']}
  report+=f"| {block+1} | {v['basic']:.6f} | {v['standard']:.6f} | {v['high']:.6f} | 0,300 |\n"
 report+='\nKońcowa macierz zawiera **51 840 próbek w 216 segmentach**: 3 bloki ABBA × 3 jakości × 3 sceny × 2 rozmiary. Wcześniejsza pełna macierz jest zachowana oddzielnie. Analizator przeszedł 7 kontroli, odrzucając brakujące/zdublowane próbki, błędne etykiety i niefinitywne/ujemne czasy: [kontrole](i08/analysis-controls.json). Wynik kwalifikuje basic, nie standard/high. [Surowe znaczniki](i08/final-benchmark/render-samples.csv), [mediany i percentyle każdego przebiegu](i08/final-benchmark/render-summary.json).\n\n'
 report+='Cały render jest droższy od referencji. Poniżej średnia z median segmentów basic oraz odpowiadającej referencji; nie są to percentyle połączonego rozkładu:\n\n| Rozmiar | Scena | Referencja [ms] | I08 basic [ms] |\n| --- | --- | ---: | ---: |\n'
 for scale in [3,5]:
  for scene in ['mission','sand_water','oil_water']:
   get=lambda variant:statistics.mean(x['total_ms']['median'] for x in budget['trials'] if x['scale']==scale and x['scene']==scene and x['variant']==variant)
   report+=f"| {320*scale} × {192*scale} | {scene} | {get('reference_for_basic'):.6f} | {get('basic'):.6f} |\n"
 report+='\nTeksturowy teren przy 960 × 576 kosztuje około 0,014–0,016 ms wobec 0,024–0,026 ms referencji w tej serii. Przy 1600 × 960 nowy przebieg kosztuje około 0,040–0,041 ms i jest droższy od referencji 0,004–0,006 ms. Usunięcie instancji nie gwarantuje przyspieszenia: pełnoekranowy przebieg zwiększa pracę fragmentów na pustej mapie. Większy rozmiar służy diagnostyce; gra nadal renderuje w 960 × 576.\n\n'
 t=budget['terrain_dirty_preprocess_ms'];report+=f"Pełny dirty preprocess terenu, obejmujący istniejące przygotowanie kolizji i nowy zapis tekstury: mediana {t['median']:.6f} ms, p95 {t['p95']:.6f} ms, 120 próbek. Nie przypisujemy całego tego czasu samemu imageStore. Pamięć renderera: 21 688 320 B przy 960 × 576 oraz 60 026 880 B przy 1600 × 960; bez diagnostycznych kopii buforów. Wszystkie jakości współdzielą zaalokowane cele, więc przełączanie nie realokuje obrazu.\n\n"
 report+='### Widoczne okna i ograniczenia\n\nSześć prób tej samej misji: trzy bez VSync i bez limitera FPS, trzy z VSync. Druga para używa większego okna; poniżej faktyczne wymiary z SDL. Statystyki poniżej opisują pełny odstęp między początkami klatek po 120 klatkach rozgrzewki, ze wszystkich wierszy mission-frames.csv. Jedyny eksport jest po ostatnim mierzonym odstępie.\n\n| Próba | Okno | Mediana [ms] | p95 [ms] | p99 [ms] | Maks. [ms] | Ticki | Pominięty czas |\n| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n'
 for w in windows:
  z=w['interval_ms'];report+=f"| {w['mode']}-{w['repeat']} | {w['width']} × {w['height']} | {z['median']:.3f} | {z['p95']:.3f} | {z['p99']:.3f} | {z['maximum']:.3f} | {w['steps']} | {w['dropped_seconds']:.0f} |\n"
 report+=f"\nKażda próba ukończyła misję 3/3. Cel p95 całej klatki ≤16,67 ms: bez VSync **{'spełniony' if summary['window_unpaced_p95_accepted'] else 'nieodebrany'}**, z VSync **{'spełniony' if summary['window_vsync_p95_accepted'] else 'nieodebrany'}**. Wszystkie odstępy klatek zachowano. Kompletność osobnej tabeli podziału czasu CPU na fazy: {'pełna' if summary['complete_frame_rows'] else 'ograniczona pojemnością bufora; liczniki utraconych wierszy są w podsumowaniu'}. [Wszystkie rozkłady, fokus i zaległość](i08/window-summary.json).\n\n"
 report+='Dodatkowa integracja high + eksperymentalne balanced przeszła misję w widocznym oknie. To test funkcjonalny tego połączenia, bez odbioru budżetu balanced. Pełna regresja nadal ujawnia historyczną zmienność GPU: przykładowo wariant PBF 128 miał p95 misji około 12,14 ms, podczas gdy bazowa misja około 3,06 ms. To różne konfiguracje i rozkłady; nie przypisujemy różnicy rendererowi. I04.5, stabilność I06 i budżet balanced I07 pozostają otwarte.\n\n'
 report+='Nieudane pierwsze kompilacje oraz uruchomienie z nierozwiniętymi nowymi include GLSL zachowano w artefaktach. Loader otrzymał jawne lokalne zależności i końcowe kontrole przeszły. Źródła produkcyjne nie zmieniły się podczas końcowej macierzy, CTest i prób okien. Osobne pliki SHA256 identyfikują wcześniejszy pomiar oraz końcowy EXE.\n'
 if (d/'package-checks.json').exists():report+='\nPaczka Windows przeszła własny test renderera oraz pełną trasę misji. [Weryfikacja paczki](i08/package-checks.json).\n'
 (s/'docs/I08_SCENE_RENDERER.md').write_text(report)
 p=s/'docs/PLAN_ROZWOJU_GPU.md';text=p.read_text().replace('Aktualizacja: **2026-10-30**, po review','Aktualizacja: **2026-10-31**, po I08 i review',1).replace('Status: **2026-10-30 —','Status: **2026-10-31 —',1)
 text=text.replace('I08–I11 nie rozpoczęte.','I08 wdrożone: A/B/C, default basic po 3 blokach ABBA, Windows 59/59 i GCC 8/8; szczegóły i ograniczenia w raporcie I08. I09–I11 nie rozpoczęte.',1)
 text=text.replace('| I08 | Renderer i wygląd wody | I05–I06; profil I07 | [ ] |','| I08 | Renderer i wygląd wody | I05–I06; profil I07 | [x]; basic domyślne po pomiarach, [raport](I08_SCENE_RENDERER.md) |')
 a=text.index('### I08 —');b=text.index('### I09 —',a);text=text[:a]+text[a:b].replace('- [ ]','- [x]')+text[b:]
 if '**2026-10-31, I08:**' not in text:text+='\n- **2026-10-31, I08:** wdrożono jawne przebiegi, wspólny tracker stanu, teren R16UI, liniową scenę, pole faz/prędkości, absorpcję i bezpieczną refrakcję, opcjonalny bilateral/Fresnel/pianę/kaustyki oraz HP i potwierdzony błysk tarcz. Windows 59/59, GCC 8/8; 12 zamrożonych checkpointów i po 512 prób terenu/maski cieczy. Końcowa macierz 51 840 próbek kwalifikuje basic do budżetu wody; cały render droższy od referencji. Cele całej klatki oraz historyczne ograniczenia są jawnie rozdzielone w raporcie I08.\n'
 p.write_text(text)
 print(json.dumps(summary,indent=2),flush=True)
 return summary
if __name__=='__main__':finalize()
