from pathlib import Path
import json,re

r=Path(__file__).resolve().parent.parent
s=r/'outputs/strefa-zero';d=r/'work/i04-5d-results'
qa=json.loads((d/'ctest-summary.json').read_text())
assert qa['tests']==35 and qa['passed']==35 and not qa['failed'],qa
w=json.loads((d/'window-summary.json').read_text())
checks=json.loads((d/'package-checks.json').read_text())
assert all(x['exit_code']==(77 if 'gl33' in name else 0) and not x['timed_out'] for name,x in checks.items())
elapsed=re.search(r'Total Test time.*?=\s*([\d.]+)',(d/'ctest.log').read_text())[1]

p=s/'docs/I04_5D_GPU_NEIGHBOR_READS.md';text=p.read_text()
text=text.replace('PENDING_QA',f'''**Pełny CTest: 35/35 PASS, kod 0, {elapsed} s.** Siedem nowych testów obejmuje cztery kombinacje AoS/SoA × sorted/reused, pełną misję narrow i sorted oraz walidację z automatycznym immutable. Każdy z pięciu testów cache wykonał 88 audytów bitowych. Przeszły także wcześniejsze testy B/C, kompletności sąsiedztwa, sprzężenia, UI, GL 3.3 i CPU. Nie zmieniono dotychczasowych limitów ani tolerancji. [Log](gpu-i04-5d/ctest.log), [JUnit](gpu-i04-5d/ctest.xml), [podsumowanie](gpu-i04-5d/ctest-summary.json).

Nowe kombinacje shaderów podczas pierwszej kompilacji wydłużyły część testów; wyniki przepustowości wykluczają inicjalizację. Kolekcja QA obejmuje tylko pliki świeższe niż początek bieżącego przebiegu. [Proweniencja](gpu-i04-5d/qa/collection-provenance.json). Sukces obecnej sesji nie unieważnia historycznych błędów C ani nie dowodzi usunięcia zmienności między sesjami.''')

def n(value,digits=4):return f'{value:.{digits}f}'.replace('.',',')
table='| Odczyt / liczba prób | Mediana pełnej klatki | Zakres p95 prób | Największa klatka |\n| --- | ---: | ---: | ---: |\n'
for mode,x in w['summary'].items():
 table+=f"| {mode} / {x['runs']} | {n(x['full_frame_median_of_medians_ms'])} ms | {n(x['full_frame_p95_range_ms'][0])}–{n(x['full_frame_p95_range_ms'][1])} ms | {n(x['full_frame_max_ms'])} ms |\n"
phases=('initialization','runtime','capture','validation','shutdown')
counts={phase:sum(int(x['gl_debug'].get(f'phase_{phase}_id131186',0)) for x in w['runs']) for phase in phases}
dropped=sum(int(x['gl_debug']['dropped']) for x in w['runs'])
focused=sum(x['focused_frames'] for x in w['runs']);analyzed=sum(x['full_frame_after_30']['samples'] for x in w['runs'])
minimized=sum(x['minimized_frames'] for x in w['runs'])
assert all(x['mission']['score']=='3' and float(x['mission']['dropped_seconds'])==0 for x in w['runs'])
text=text.replace('PENDING_WINDOWS',f'''Wykonano sześć porównawczych prób widocznego okna full/narrow (po trzy, porządek naprzemienny tworzący ABBA), a osobno jedną diagnostyczną sorted. Wszystkie siedem procesów zakończyło się kodem 0: po 900 klatek, wynik misji 3, bez utraty czasu. VSync OFF, limit 60 FPS, elapsed, liczniki i KHR_debug ON. Nie wymuszano fokusu; odnotowano go w {focused}/{analyzed} analizowanych klatkach, minimalizacja: {minimized}.

{table}
Statystyki liczą pełny czas klatki po pierwszych 30 klatkach i obejmują testowe przechwycenie obrazu. W tabeli mediana jest medianą wyników prób. Dane bez przechwytywania oraz wszystkie p99 i maksima są zapisane osobno. [Pełne wyniki](gpu-i04-5d/window-summary.json), [porządek sześciu prób](gpu-i04-5d/windows-comparison/runs.csv), [osobna próba sorted](gpu-i04-5d/window-sorted-diagnostic/runs.csv). Trzy próby każdego wariantu nie stanowią sześciu prób odbioru jednej nowej ścieżki i nie zamykają pozostałych celów I04.5.

Łączne komunikaty migracji id=131186 według fazy odbioru callbacku: initialization {counts['initialization']}, runtime {counts['runtime']}, capture {counts['capture']}, validation {counts['validation']}, shutdown {counts['shutdown']}. Logger zgubił {dropped} komunikatów. Asynchroniczna dostawa może dotyczyć wcześniejszej pracy; faza odbioru nie dowodzi przyczyny ani fizycznej rezydencji pamięci. Callback pozostał aktywny.''')
text=text.replace('PENDING_PACKAGE','''Paczka `StrefaZero-Windows-x64-I04_5-D` przeszła własny `--gpu-hot-check --gpu-storage auto` oraz `--gpu-profile-check --gpu-neighbor-reads sorted` (oba kod 0). Wymuszony `--opengl33 --gpu-hot-check` zwrócił oczekiwane 77. EXE i assety odpowiadają finalnemu buildowi/źródłom. ZIP ma własny manifest SHA256 i kontrolę CRC; jest kompilacją diagnostyczną, bez nowego tagu wydania. [Kontrole paczki](gpu-i04-5d/package-checks.json), [walidacja](gpu-i04-5d/package-hot-check/hot-tests.txt), [zapis źródeł i EXE](gpu-i04-5d/source-state.json). Zweryfikowano niezmienność 2182 plików historycznych raportów/danych oraz sumy zewnętrznych paczek A/B/C.''')
assert 'PENDING' not in text;p.write_text(text)

p=s/'docs/PLAN_ROZWOJU_GPU.md';text=p.read_text()
text=re.sub(r'^Status: .*$', 'Status: **I04.5-A/F wdrożone; B/C/D zbadane jako eksperymenty. D: 35/35 QA, 18/18 procesów pomiarowych i 7 prób okna. Domyślne full/direct oraz obie opcje C OFF. Cel 1,5 ms i przyczyna historycznej zmienności pozostają otwarte. E wymaga pomiaru chwilowego Dmax i kosztów przebiegów; I05–I11 nadal zaplanowane**.',text,count=1,flags=re.M)
text=text.replace('Aktualne QA po B: **28/28 PASS**','Aktualne QA po D: **35/35 PASS**')
text=text.replace('a transport i aktualne QA — [raport B](I04_5B_GPU_TRANSPORT.md).','transport — [raport B](I04_5B_GPU_TRANSPORT.md), a odczyty sąsiadów i aktualne QA — [raport D](I04_5D_GPU_NEIGHBOR_READS.md).')
text=re.sub(r'^- \[ \] \*\*D — warunkowy `sortedHot`:.*$', '- [x] **D — badanie odczytów i `sortedHot`:** porównano full/narrow/sorted w 18 procesach. Cache ma dwa aktualizowane banki pozycji/kompensacji/prędkości/masy/typu, bezpośredni gather według wpisu, mapę odwrotną dla producentów oraz unieważnianie przy edycji ID. Dodatkowe 2 MiB mieszczą się w istniejących 8 SSBO. 35/35 QA, po 88 audytów bitowych w pięciu testach cache; siedem okien bez utraty czasu. Narrow: około −2,2% mediany misji bez podobnego zysku 16k/32k; sorted spowalnia większe sceny. Domyślne full zachowane, szczegóły w raporcie D.',text,count=1,flags=re.M)
text=text.replace('**E — warunkowy lokalny `Dmax`:** zmierzyć wpływ pojedynczych outlierów;', '**E — warunkowy lokalny `Dmax`:** odroczone po D. Dziewięć końcowych snapshotów produkcyjnego SoA/reused ma Dmax=0,0625, ale nie mierzy szczytów całej trajektorii. Najpierw przypisać koszt przebiegom i zmierzyć Dmax/kandydatów w czasie przy eksplozjach. Następnie, jeżeli koszt się potwierdzi, zmierzyć wpływ pojedynczych outlierów;')
old='Następne jest przypisanie kosztu obecnych przebiegów przed decyzją D/E. Pełny odbiór całego I04.5 pozostaje otwarty.'
new='[Badanie D](I04_5D_GPU_NEIGHBOR_READS.md): 35/35 QA, 18/18 pomiarów i 7 okien; full pozostaje domyślne. Następne jest przypisanie kosztu przebiegów i pomiar chwilowego Dmax przed decyzją E. Pełny odbiór całego I04.5 pozostaje otwarty.'
assert old in text;text=text.replace(old,new)
anchor='\n\nPo każdej zakończonej iteracji dopisać:'
assert anchor in text;text=text.replace(anchor,'\n| I04.5-D | Badanie opt-in zakończone 2026-10-30; full domyślne | [Raport D](I04_5D_GPU_NEIGHBOR_READS.md): wąskie odczyty i aktualizowany cache w 8 SSBO, 35/35 QA, 18/18 procesów i 7 okien bez utraty czasu. Brak podstaw do zmiany domyślnej ścieżki; cache +2 MiB spowalnia 16k/32k | Profil kosztów przebiegów i chwilowego Dmax przed warunkowym E; cel 1,5 ms nadal otwarty |'+anchor)
anchor='\n## 8. Materiały pomocnicze'
text=text.replace(anchor,'\n- **2026-10-30, I04.5-D:** zachowujemy full/direct oraz obie opcje C OFF. 18/18 procesów potwierdziło niewielką przewagę narrow w misji, bez podobnej korzyści 16k/32k; sorted ma dodatkowy koszt zapisów i 2 MiB. Pełne QA 35/35, siedem prób okna bez utraty czasu. Mały końcowy Dmax nie wyklucza chwilowych outlierów; lokalny E czeka na profil w czasie i dowód kompletności. Cel 1,5 ms oraz historyczna przyczyna zmienności nadal otwarte.\n'+anchor)
p.write_text(text)

p=s/'README.md';text=p.read_text()
text=text.replace('D/E nadal wymagają uzasadnienia pomiarami.','Wyniki późniejszego badania odczytów są w raporcie D poniżej.')
anchor='\n[Plan rozwoju GPU, wspólnego świata, wody i efektów]'
assert anchor in text;text=text.replace(anchor,'\n**I04.5-D — odczyty sąsiadów:** dodano eksperymentalne narrow i aktualizowany cache sorted. **35/35 QA**, **18/18 procesów porównawczych** i **7 okien bez utraty czasu**. Domyślne pozostają full/direct i obie opcje C OFF: cache +2 MiB spowalnia większe sceny, a niewielki zysk narrow nie jest spójny między scenami. [Raport D](docs/I04_5D_GPU_NEIGHBOR_READS.md) opisuje wyniki i warunki dalszego E.\n'+anchor)
text=text.replace('Aktualny zakres po review: przypisanie kosztu przebiegów przed warunkowym D/E. B zakończono jako badanie; direct pozostaje domyślne.','Aktualny zakres po review: profil kosztów przebiegów i chwilowego Dmax przed warunkowym E. B/D zakończono jako badania; full/direct pozostają domyślne.')
p.write_text(text)

p=s/'docs/REVIEW_I04_ROZSTRZYGNIECIA.md';text=p.read_text()
text=re.sub(r'^Status: .*$', 'Status: **I04.5-A/F wdrożone; B/C/D zbadane jako eksperymenty. Domyślne full/direct i obie opcje C OFF. D: 35/35 QA, 18/18 procesów i 7 prób okna bez utraty czasu. Cel 1,5 ms i przyczyna zmienności pozostają otwarte; E wymaga profilu chwilowego Dmax oraz kosztów przebiegów**. [Raport A/F](I04_5_GPU_STABILIZATION.md), [C](I04_5C_GPU_WORK.md), [B](I04_5B_GPU_TRANSPORT.md), [D i aktualne QA](I04_5D_GPU_NEIGHBOR_READS.md).',text,count=1,flags=re.M)
text=text.replace('| I04.5-D/E warunkowo po pomiarze |','| I04.5-D zbadane, full domyślne; E odroczone do profilu w czasie |')
text=text.replace('Po C zapisano 16/16 pomiarów bez potwierdzonego przyspieszenia;', 'Stan po B, przed badaniem D: po C zapisano 16/16 pomiarów bez potwierdzonego przyspieszenia;')
text+='\nBadanie D porównało full/narrow/sorted w 18 procesach. Cache aktualizuje pełną geometrię kompensowaną i motion wraz z producentami, mieści się w 8 SSBO, lecz kosztuje 2 MiB i spowalnia większe sceny. Full pozostaje domyślne. Pełne QA 35/35 oraz siedem prób okna przeszły; dziewięć końcowych snapshotów Dmax=0,0625 nie wyklucza chwilowych outlierów. E czeka na profil w czasie i dowód kompletności. [Raport D](I04_5D_GPU_NEIGHBOR_READS.md).\n'
p.write_text(text)

p=r/'outputs/03_PLAN_PRAC.md';text=p.read_text()
text=text.replace('Następne jest przypisanie kosztu przebiegów przed warunkowym D/E.','Badanie [I04.5-D](strefa-zero/docs/I04_5D_GPU_NEIGHBOR_READS.md): 35/35 QA, 18/18 procesów i 7 okien; full pozostaje domyślne. Następne jest przypisanie kosztu przebiegów i pomiar chwilowego Dmax przed warunkowym E.')
p.write_text(text)

package=r/'outputs/StrefaZero-Windows-x64-I04_5-D'
(package/'QA.txt').write_text(f'''Strefa Zero — diagnostyczne I04.5-D, 2026-10-30
EXE SHA256: cca9aba9e226d786b55e5da05ddbc14357b1f6556404fec097032d67d7f7ee7a

Release MSVC /W4 /WX, IPO: PASS.
Pełny CTest: 35/35 PASS, kod 0, {elapsed} s. Dotychczasowe limity i tolerancje bez zmian.
Nowe testy: AoS/SoA × sorted/reused, misje narrow/sorted, auto immutable: PASS.
Pięć testów cache: po 88 audytów bitowej zgodności. Maksymalny błąd małych scen: 0.
Seria throughput: 18/18 procesów, analyzer schema 5, 324 wiersze, zero ostrzeżeń.
Okno: 3 full + 3 narrow w naprzemiennym porządku, osobno 1 sorted; wszystkie kod 0, wynik 3 i brak utraty czasu.
Szczegóły fokusu i KHR_debug w docs/gpu-i04-5d/window-summary.json.

Kontrole paczki:
--gpu-hot-check --gpu-storage auto: kod 0 PASS.
--gpu-profile-check --gpu-neighbor-reads sorted: kod 0 PASS.
--opengl33 --gpu-hot-check: oczekiwany kod 77 PASS.

Domyślne neighbor_reads=full, storage=direct, obie opcje C OFF.
Narrow i sorted są eksperymentalne; cache zajmuje dodatkowe 2 MiB.
Cel GPU 1,50 ms, przyczyna historycznej zmienności oraz cały I04.5 pozostają otwarte.
EXE/assety odpowiadają finalnemu buildowi/źródłom. To kompilacja diagnostyczna, bez nowego tagu wydania.
Pełny raport w projekcie: docs/I04_5D_GPU_NEIGHBOR_READS.md i docs/gpu-i04-5d/.
''')
print('Report, plan, pointers and package QA updated.')
