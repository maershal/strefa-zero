from pathlib import Path
import csv,json,hashlib
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5g-results'
analysis=json.loads((d/'matrix/analysis.json').read_text());qa=json.loads((d/'ctest-summary.json').read_text());q=json.loads((d/'q-summary.json').read_text());state=json.loads((d/'build-state.json').read_text());windows=json.loads((d/'window-summary.json').read_text())
assert qa['passed']==qa['tests']==44 and analysis['valid_runs']==16
rows={(x['scene'],x['metric'],x['variant']):x for x in analysis['aggregates']}
def n(v,digits=5):return f'{v:.{digits}f}'.replace('.',',')
table='| Scena | Q | G 256 | G 128 | G 64 |\n| --- | ---: | ---: | ---: | ---: |\n'
for scene in ('mission','stress_16k','stress_32k','mission_blast'):
 table+='| '+scene+' | '+' | '.join(n(rows[scene,'gpu_step_ms',v]['median_of_medians'])+' / '+n(rows[scene,'gpu_step_ms',v]['median_of_p95']) for v in ('q','g256','g128','g64'))+' |\n'
changes='| Scena | G 256 vs Q | G 128 vs G 256 | G 64 vs G 256 |\n| --- | ---: | ---: | ---: |\n'
for scene in ('mission','stress_16k','stress_32k','mission_blast'):
 changes+='| '+scene+' | '+n(rows[scene,'gpu_step_ms','g256']['median_paired_change_vs_q_pct'],2)+'% | '+' | '.join(n(rows[scene,'gpu_step_ms',v]['median_paired_change_vs_g256_pct'],2)+'%' for v in ('g128','g64'))+' |\n'
qtable='| PBF | Powtórki Q | Checkpointy / porównania | Wykryte wyłączenia sprzężenia |\n| --- | ---: | ---: | ---: |\n'
for size,data in q.items():qtable+=f"| {size} | {data['runs']}/{data['runs']} | {data['checkpoints']} / {data['comparisons']} | {data['faults_rejected']}/{data['runs']} |\n"
report=f'''# I04.5-G — niezależny rozmiar grup PBF

Data: 2026-10-30. Baza: opublikowane Q, commit `7e85553109c35d390a151454011fa8836db44045`. To badanie kształtu uruchomień; nie zmienia równań, liczby podkroków/iteracji, tolerancji ani polityki zaległości zegara. Decyzję opisuje koniec raportu.

## Zakres

Dodano `--gpu-pbf-size 64|128|256`. Dotyczy gęstości/lambdy, korekty, diagnostyki gęstości i XSPH w `liquids.comp` (operacje 0/1/2/4). Zerowanie diagnostyki (3) nadal ma jeden wątek w programie specjalizowanym. Ziarna, predykcja i redukcje pozostają na 256 w porównaniu G.

Brak flagi zachowuje dotychczasowe dziedziczenie `--gpu-local-size`; zwykła gra nadal efektywnie używa 256. Warianty 64/128 są jawne i eksperymentalne. Nie dodano wskazówek `restrict`, zmian Dmax ani nowych metod fizyki.

Osobny argument indirect PBF powstaje na GPU z rzeczywistego `aliveCount`: `ceil(count/PBF_LOCAL_SIZE)`, także zero dla pustego świata. Argumenty są w istniejącym buforze Free, słowa 34–36; dotychczasowe offsety zachowano. Pojemność tego bufora wzrosła o **12 B**, bez dodatkowego SSBO, dispatchu, bariery ani odczytu CPU w kroku. Publikacja argumentów korzysta z dotychczasowej bariery command po przebiegu 12. Cząstki nadal mają stabilne adresy ID.

## Odtworzenie

Windows PowerShell 5.1, Release MSVC `/W4 /WX`, IPO, GTX 1650, NVIDIA 610.47, OpenGL 4.3. EXE G SHA256 `{state['candidate_exe_sha256']}`; Q `{state['baseline_exe_sha256']}`. Hashe źródeł/shaderów/skryptów: [build-state](gpu-i04-5g/build-state.json), konfiguracja i sprzęt: [środowisko natywnego harnessu](gpu-i04-5g/matrix/g256-1/environment.json).

```powershell
StrefaZero.exe --gpu-execution-check --profile '<QA-dispatch>'
StrefaZero.exe --gpu-liquid-test --gpu-pbf-size 64 --profile '<QA-liquid-64>'
StrefaZero.exe --mission-test --gpu-pbf-size 64 --profile '<QA-mission-64>'
StrefaZero.exe --gpu-coupling-check --gpu-pbf-size 64 --profile '<QA-Q-64>'
# Powtórzyć fizykę, misję i Q dla 128 oraz Q dla 256.
.\\scripts\\profile-pbf.ps1 -Executable '<G>\\StrefaZero.exe' -BaselineExecutable '<Q>\\StrefaZero.exe' -Output '<nowy katalog>'
python .\\scripts\\analyze-pbf.py --input '<nowy katalog>'
```

## Poprawność

**Pełny CTest: {qa['passed']}/{qa['tests']} PASS w protokole Q**, kod 0, 1434,50 s; czas obejmuje tworzenie wcześniej nieużywanych wariantów shaderów. Sześć nowych przypadków obejmuje ciecz, misję i Q dla 64/128; domyślne 256 przechodzi istniejące testy. Zachowano testy tras 30/60/144 FPS, szczelności cienkich ścian, gęstości CPU, zachowania mas/ID, resetów, transportu, C/D, CPU, UI i GL 3.3. [Log](gpu-i04-5g/ctest.log), [JUnit](gpu-i04-5g/ctest.xml), [podsumowanie](gpu-i04-5g/ctest-summary.json).

Rozszerzony test wykonania sprawdza rozmiary PBF 64/128/256 przy ogólnym i redukcyjnym 256: 0, 1, rozmiar−1/rozmiar/rozmiar+1, 255/256/257 oraz 32768 aktywnych cząstek. Nieciągłe ID są permutowane po pełnej puli. Sprawdza rzeczywiste argumenty GPU i niezależną sumę gęstości Poly6 double na CPU dla każdego aktywnego ID, także na końcu niepełnej grupy. [Wynik](gpu-i04-5g/qa/execution-profile/execution-tests.txt).

Q otrzymuje teraz wybraną konfigurację zamiast tworzyć domyślną bez względu na CLI. Ciągły nośnik referencyjny używa PBF 256, repliki wybranego rozmiaru. Dzięki temu wspólne checkpointy obejmują także różnicę względem 256. Wartości stałych budżetów i rzeczywista negatywna kontrola pozostają takie jak w Q. Każdy rozmiar ma próbę z pełnego CTest i dwie kolejne w niezależnych procesach:

{qtable}

[Przeliczone z surowych CSV sumy i maksima](gpu-i04-5g/q-summary.json). To kontrola defektów na wspólnych stanach, nie dowód zgodności długich niezależnych trajektorii. Stary losowy iloraz pozostaje diagnostyczny: odrzucił 3/9 długich tras (po jednej dla każdego rozmiaru). Nie traktujemy wyniku Q jako zaliczenia starej reguły; pełne wyniki zachowano.

## Koszt

**Końcowa seria porównawcza: 16/16 procesów**, cztery warianty × cztery powtórzenia, cztery sceny w każdym procesie. Kolejność Williams równoważy pozycje i poprzedzający wariant. Q jest niezmienioną opublikowaną paczką, G 256 mierzy koszt samego rozdzielenia, G 128/64 zmieniają tylko rozmiar rodziny PBF w tym samym EXE. Próby GPU wykonywano pojedynczo. Pierwsza próba skryptu uruchomiła tylko cztery kontrole Q, a nowe flagi G odrzuciła lista dozwolonych argumentów przed startem procesu. Dopisano flagę i powtórzono całą zrównoważoną serię w świeżym katalogu. Dodatkowa pełna seria miała poprawne podsumowania, lecz brakowało plików CPU/liczników w katalogach scen. Nadzorca tworzy teraz te katalogi przed startem każdej wersji, a analyzer wymaga 240 wierszy i konfiguracji każdej sceny. Całą serię powtórzono; [notatka o eksporcie](gpu-i04-5g/raw-export-note.json). Bezpośrednie użycie `--gpu-cost-profile` wymaga przygotowania czterech katalogów scen albo użycia dołączonego harnessu. Wstępne próby nie wchodzą do poniższych porównań; [log i wyjaśnienie](gpu-i04-5g/matrix-retry-note.json) zachowują problem skryptu, bez zmian runtime i progów.

Każda scena: 120 ticków rozgrzewki, 240 pomiarowych; elapsed, SoA/reused, full/direct, combined, HUD auto/2, C OFF, sondy kosztów i grupowe timery OFF. `glFinish`/poll co 16 ticków poza CPU submit, ale w czasie ściennym partii. Eksplozje jak w E0. Analyzer sprawdza rzeczywiste argumenty procesu, efektywny PBF, stałe 256 pozostałych grup, liczbę próbek, liczebność, skończoność, wyłączenie sond i niezmienność EXE. [Kolejność](gpu-i04-5g/matrix/schedule.csv), [procesy](gpu-i04-5g/matrix/runs.csv), [analiza](gpu-i04-5g/matrix/analysis.csv).

Poniżej **mediana czterech median / mediana czterech p95**, ms GPU/tick. Nie łączono próbek w pozorny wspólny percentyl.

{table}

Mediana zmian median w odpowiadających sobie powtórzeniach; ujemna wartość oznacza krótszy czas:

{changes}

Pełne CPU, p99, maksima i czas ścienny są w [agregatach](gpu-i04-5g/matrix/aggregate.csv). Wynik pojedynczego pomiaru Q przed zmianą (misja GPU 2,78950 ms) zachowano [osobno](gpu-i04-5g/before-change/summary.csv); nie jest podstawą porównania nowych wariantów. Nie wykazano istotności statystycznej ani naprawy historycznej zmienności warunków GPU. Składnia PowerShell 5.1 i trzy negatywne kontrole analyzera przeszły: [składnia](gpu-i04-5g/script-syntax.json), [odrzucone błędne dane](gpu-i04-5g/analyzer-fixture-checks.json). Zachowano 15360 wierszy surowych kroków CPU/liczników; czasy GPU są zapisane jako percentyle 240 próbek na proces/scenę, bez pojedynczych surowych czasów GPU. [Liczniki i pamięć](gpu-i04-5g/counter-summary.json).

## Okno i paczka

{{WINDOW_RESULTS}}

Zweryfikowano niezmienność 4930 wcześniejszych plików raportów/danych i sumy paczek A–Q: [integralność historii](gpu-i04-5g/historical-integrity.json).

Paczka diagnostyczna `StrefaZero-Windows-x64-I04_5-G.zip` ma własne sumy SHA256 i kontrolę CRC. Nie zmienia wersji gry ani opublikowanego wydania Q. Własne kontrole paczki są w [package-checks](gpu-i04-5g/package-checks.json); skrypty pomiaru i analizy znajdują się również w jej katalogu tools.

## Decyzja

{{DECISION}}
'''
report=report.replace('{WINDOW_RESULTS}',windows['report_text']).replace('{DECISION}',(d/'decision.txt').read_text())
(s/'docs/I04_5G_GPU_PBF_GROUPS.md').write_text(report)
print('Wrote report',len(report))
