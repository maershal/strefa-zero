from pathlib import Path
import json
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g2-results'
qa=json.loads((d/'ctest-summary.json').read_text());q=json.loads((d/'q-summary.json').read_text());state=json.loads((d/'build-state.json').read_text());windows=json.loads((d/'window-summary.json').read_text());history=json.loads((d/'historical-integrity.json').read_text())
assert qa['passed']==qa['tests']==52
series={name:json.loads((d/name/'analysis.json').read_text()) for name in ('matrix-slow','matrix')}
assert all(x['valid_runs']==18 for x in series.values())
def n(v,digits=5):return f'{v:.{digits}f}'.replace('.',',')
def table(data):
 rows={(x['scene'],x['variant']):x for x in data['aggregates'] if x['metric']=='gpu_step_ms'}
 text='| Scena | G1 | G2 OFF | G2 ON | ON vs OFF, mediana zmian par |\n| --- | ---: | ---: | ---: | ---: |\n'
 for scene in ('mission','stress_16k','stress_32k','mission_blast'):
  text+='| '+scene+' | '+' | '.join(n(rows[scene,v]['median_of_medians'])+' / '+n(rows[scene,v]['median_of_p95']) for v in ('g1','g2off','g2on'))+' | '+n(rows[scene,'g2on']['median_paired_change_vs_off_pct'],2)+'% |\n'
 return text
qtable='| Tryb / PBF | Procesy Q | Checkpointy / porównania | Wyłączenia sprzężenia odrzucone |\n| --- | ---: | ---: | ---: |\n'
for name,data in q.items():qtable+=f"| {name} | {data['runs']}/{data['runs']} | {data['checkpoints']} / {data['comparisons']} | {data['faults_rejected']}/{data['runs']} |\n"
legacy=sum(x['legacy90_diagnostic_rejections'] for x in q.values())
report=f'''# I04.5-G2 — kwalifikatory dostępu do rozdzielonych banków PBF

Data: 2026-10-30. Baza: lokalna, niezmieniona paczka [G1](I04_5G_GPU_PBF_GROUPS.md), EXE `{state['baseline_exe_sha256']}`. Ostatni commit repozytorium nadal opisuje Q; zastany zakres G1 zachowano. EXE G2 `{state['exe_sha256']}`. [Stan builda i źródeł](gpu-i04-5g2/build-state.json), [stan wejściowy G1](gpu-i04-5g2/base-source-state.json).

## Zakres i kontrakt

Dodano eksperymentalne `--gpu-pbf-restrict on|off`, domyślnie **OFF**. Przy programach specjalizowanych ON zmienia wyłącznie korektę PBF i XSPH (`liquids.comp`, operacje 1 i 4). Wejściowy bank otrzymuje `readonly restrict`, wyjściowy `writeonly restrict`. Gęstość/lambda, diagnostyka, ziarna, teren, postać, redukcje oraz in-place spawn/blast/debris pozostają bez tych kwalifikatorów. Żądanie w trybie ubershaderów ma efektywne OFF, jawnie raportowane.

`restrict` jest obietnicą o dostępie do pamięci, a nie sprawdzeniem wykonywanym przez GPU; złamanie obietnicy daje niezdefiniowane wyniki. Reguły kwalifikatorów dla członków i bloków SSBO opisuje [GLSL 4.30, §4.10–4.11, strony drukowane 83–85](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.30.pdf). W tej implementacji oba banki są pełnymi, odrębnymi obiektami buforów, wiązanymi przez `glBindBufferBase`.

Produkcja rozwiązuje rzeczywiste ID baz przed `UseProgram`, zmianą wiązań i dispatch. Dla oznaczonego programu kontrola wymaga niezerowych banków 0/1 oraz braku ich aliasowania z każdym innym z ośmiu wiązań SSBO. Brak zapytań o stan GL w kroku. Programy in-place nie otrzymują tej deklaracji ani kontroli. Bariery i harmonogram solvera są zachowane; nie dodano dispatchu, bufora GPU ani odczytu zwrotnego w runtime. Wariant ON obejmuje również koszt tej kontroli CPU; nie zakładamy, że jest darmowa.

Specjalizacja powstaje przy tworzeniu programu przez ścisłą zmianę dwóch deklaracji kanonicznego `particle_storage.glsl`, dla obu gałęzi AoS/SoA. Wymagane są dokładnie dwa wystąpienia każdej deklaracji, inaczej tworzenie programu kończy się błędem wymagającym przeglądu ABI. Nie ma drugiej kopii układu cząstki. OFF przekazuje dotychczasowy tekst GLSL bez zmian; pliki shaderów na dysku mają te same hashe co G1. To ogranicza też ponowną kompilację do programów rzeczywiście objętych zmianą.

## Poprawność

**Pełny CTest: 52/52 PASS, kod 0**, {sum(float(x.get('time',0)) for x in qa['cases']):.2f} s sumy przypadków; czas ścienny 593,13 s. Osiem nowych przypadków obejmuje kontrakt, ciecze, misję, Q i cztery układy AoS/SoA × sorted/reused z alternatywnymi odczytami oraz opcjami C. Dotychczasowe grupy 64/128, CPU, UI, transport, sąsiedztwo, trasy 30/60/144 FPS i GL 3.3 nadal przechodzą. [Log](gpu-i04-5g2/ctest.log), [JUnit](gpu-i04-5g2/ctest.xml), [podsumowanie](gpu-i04-5g2/ctest-summary.json).

Test kontraktu wstrzykuje aliasy Input/Output z każdym innym SSBO oraz brak wiązania banku, w obu kierunkach ping-pong i AoS/SoA. **128/128 odrzuceń przed jakimkolwiek wywołaniem GL**, bez zmiany autorytatywnych cząstek. Test akceptuje tylko dedykowany błąd kontroli produkcyjnej; dodatkowy bezpiecznik uniemożliwia wysłanie błędnego dispatchu nawet po regresji kontroli. Po odrzuceniach przechodzą zwykły krok, wybuch i edycja/spawn/erase. Sprawdzono domyślne OFF i fallback ubershaderów.

Źródła odczytano z faktycznie dołączonych obiektów shaderów OpenGL przez `GetAttachedShaders`/`GetShaderSource`, wyłącznie w walidacji. ON i OFF różnią się tylko powyższymi kwalifikatorami w dwóch programach; pozostałe trzy programy cieczy są identyczne. Zachowano pary źródeł dla AoS/SoA: [kontrola i osiem plików GLSL](gpu-i04-5g2/binding-source-check/pbf-restrict-tests.txt). Porównanie wykonano przed zapisem; zapis tekstowy używa zakończeń wierszy Windows. To dowód danych przekazanych kompilatorowi, nie dowód konkretnej optymalizacji kodu maszynowego.

Nośnik Q pozostaje przy PBF 256 i bez kwalifikatorów; repliki otrzymują badany wariant. Budżety kroku i skumulowanego defektu nie zostały zmienione. G2 OFF i ON mają po próbie z CTest i dwóch dodatkowych; ON 64/128 dodatkowo sprawdza współdziałanie z G1:

{qtable}

Łącznie **8/8 procesów, 720 checkpointów, 2880 porównań, 8/8 wykrytych rzeczywistych wyłączeń sprzężenia**. [Niezależnie przeliczone sumy](gpu-i04-5g2/q-summary.json). Dawna reguła 90-krokowych niezależnych trajektorii odrzuciła {legacy}/8 prób diagnostycznych i nadal pozostaje dostępna jawnie. Wynik Q nie oznacza deterministyczności ani zaliczenia dawnego kryterium.

## Pomiary i zmienność bazy

GTX 1650, NVIDIA 610.47, OpenGL 4.3, Release MSVC `/W4 /WX`, IPO. W każdej serii: G1/G2 OFF/G2 ON × sześć powtórzeń, wszystkie sześć permutacji porządku. Pozycje i skierowane sąsiedztwo równoważone wewnątrz bloków po trzy procesy; brak gwarancji identycznych warunków systemowych. G2 OFF/ON używają jednego EXE, G1 osobnego niezmienionego EXE. Każda scena ma 120 ticków rozgrzewki i 240 próbek; general/PBF/reduction 256, full/direct, reused/SoA, combined, HUD auto/2, elapsed, C i sondy OFF. Poll/drain co 16 ticków poza CPU submit, ale w czasie ściennym partii. Próby GPU wykonywano pojedynczo.

Analyzer sprawdza kody i faktyczne argumenty procesów, żądany/efektywny tryb, grupy 256, liczebność, skończoność, próbki CPU i wyłączenie sond. Kontroluje niezmienność EXE, przypięty hash G1 i wspólne pliki GLSL. Każda seria ma 216 wierszy analizy i **17280 surowych wierszy CPU/liczników**; czasy GPU zachowano jako percentyle 240 próbek, bez indywidualnych czasów GPU. [Negatywne kontrole analyzera](gpu-i04-5g2/analyzer-fixture-checks.json), [składnia skryptów](gpu-i04-5g2/script-syntax.json).

Najpierw G1 osiągnęła **2,846752 ms** GPU misji w kontroli przed uruchomieniem kandydata. Po pełnym QA pierwsza seria dała około **12 ms na tym samym G1**, podobnie na obu G2. Następne kontrole G1 wróciły do **2,823328 ms** przy domyślnych i **2,830592 ms** przy jawnych grupach 256. Między tymi kontrolami nie zmieniono badanych EXE ani ustawień GPU. Zachowano [kontrolę początkową](gpu-i04-5g2/baseline-before-candidate/summary.csv), [świeże kontrole](gpu-i04-5g2/condition-control/), [notatkę](gpu-i04-5g2/condition-note.json). To obserwacja zmienności niezmienionej bazy, nie identyfikacja przyczyny.

Wykonano jedną dodatkową pełną serię w odtworzonych warunkach; obu serii nie łączono i żadnej nie usunięto. Nie przypisujemy różnicy 2,8↔12 ms kwalifikatorom G2, DWM, temperaturze, rezydencji VRAM ani sterownikowi bez dalszego dowodu. Telemetria obejmuje cały proces, także tworzenie świata i przejścia bezczynności; nie jest pomiarem konkretnego dispatchu.

Poniżej mediana sześciu median / mediana sześciu p95, ms/tick; zmiana par to mediana procentowych zmian median w odpowiadających powtórzeniach.

### Pierwsza seria — obserwowany wolniejszy przebieg bazy

{table(series['matrix-slow'])}

[Procesy](gpu-i04-5g2/matrix-slow/runs.csv), [agregaty](gpu-i04-5g2/matrix-slow/aggregate.csv), [liczniki i wiek HUD](gpu-i04-5g2/matrix-slow/counter-summary.json). GPU G1 nie było stabilną referencją względem wcześniejszych sesji; CPU submit również był znacznie wyższy. Nie jest to dowód kosztu kontroli aliasów.

### Pełna powtórka po świeżych kontrolach G1

{table(series['matrix'])}

[Procesy](gpu-i04-5g2/matrix/runs.csv), [wszystkie GPU/CPU/p99/maksima/czas ścienny](gpu-i04-5g2/matrix/aggregate.csv), [liczniki i pamięć](gpu-i04-5g2/matrix/counter-summary.json). Nie łączono percentyli ani próbek między procesami i nie wykazano istotności statystycznej. Wnioski dotyczą badanych warunków, nie ogólnej naprawy wydajności.

## Widoczna gra i paczka

{windows['report_text']}

Paczka `StrefaZero-Windows-x64-I04_5-G2.zip` ma własne kontrole: [wyniki](gpu-i04-5g2/package-checks.json), `QA.txt` i `SHA256SUMS.txt` w paczce. Obejmuje te same zasoby i testowany EXE; tryb ON jest jawny. Nie jest nowym tagiem wersji gry. Zachowano hashe {history['historical_files_verified']} wcześniejszych raportów/danych oraz paczek A–G1; [integralność historii](gpu-i04-5g2/historical-integrity.json).

## Odtworzenie

```powershell
StrefaZero.exe --gpu-pbf-restrict-check --profile '<binding-QA>'
StrefaZero.exe --gpu-liquid-test --gpu-pbf-restrict on --profile '<liquid-QA>'
StrefaZero.exe --mission-test --gpu-pbf-restrict on --profile '<mission-QA>'
StrefaZero.exe --gpu-coupling-check --gpu-pbf-restrict on --profile '<Q-ON>'
.\\scripts\\profile-pbf-restrict.ps1 -Executable '<G2>\\StrefaZero.exe' -BaselineExecutable '<G1>\\StrefaZero.exe' -Output '<nowy katalog>'
python .\\scripts\\analyze-pbf-restrict.py --input '<nowy katalog>'
```

## Decyzja

{(d/'decision.txt').read_text()}
'''
(s/'docs/I04_5G2_GPU_PBF_RESTRICT.md').write_text(report)
print('Wrote G2 report',len(report))
