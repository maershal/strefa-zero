from pathlib import Path
import json,re,statistics
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5e-results'
qa=json.loads((d/'ctest-summary.json').read_text());assert qa['tests']==38 and qa['passed']==37 and qa['failed']==['gpu.coupling']
control=json.loads((d/'coupling-controls.json').read_text());assert control['coupling-recheck-E0']['passed'] and not control['coupling-control-D']['passed']
checks=json.loads((d/'package-checks.json').read_text());assert all(x['exit_code']==(77 if 'gl33' in name else 0) and not x['timed_out'] for name,x in checks.items())
window=json.loads((d/'package-window-summary.json').read_text());source=json.loads((d/'source-state.json').read_text())
p=s/'docs/I04_5E0_GPU_COSTS.md';t=p.read_text()
t=t.replace('PENDING_QA','''**Pełny CTest: 37/38, kod 8, 330,27 s.** Wszystkie trzy nowe testy diagnostyki (AoS, SoA i auto/immutable) przeszły, podobnie dotychczasowe testy B/C/D, misji, sąsiedztwa, wykonania, odczytów, fizyki, UI, CPU i GL 3.3. Nie przeszedł wcześniejszy `gpu.coupling`, w porównaniu geometrycznym wariantu local z referencją. [Pełny log](gpu-i04-5e0/ctest.log), [JUnit](gpu-i04-5e0/ctest.xml), [podsumowanie](gpu-i04-5e0/ctest-summary.json).

W nieudanej próbie E0 przesunięcie środka wyniosło 0,0501863 przy limicie 0,0467122, wyliczonym z powtórki referencji. Osobna powtórka E0 przeszła. Niezmieniona paczka D odtworzyła ten sam błąd: 0,0213728 przy limicie 0,0205292. Hash kontrolnego D odpowiada zachowanemu buildowi `cca9aba9…7f7ee7a`. [Pierwszy błąd](gpu-i04-5e0/coupling-first-failure/coupling-tests.txt), [powtórka E0](gpu-i04-5e0/coupling-recheck-E0/coupling-tests.txt), [kontrola D](gpu-i04-5e0/coupling-control-D/coupling-tests.txt), [liczby i wyniki procesów](gpu-i04-5e0/coupling-controls.json).

To dowód występowania błędu także w bazie, nie wyjaśnienie jego przyczyny ani zamknięcie QA. Nie zmieniono testu, limitów ani tolerancji; nie zastąpiono pełnego wyniku 37/38 zieloną powtórką. Baza QA sprzężenia wymaga wyjaśnienia przed kolejną optymalizacją. Kolekcja zachowuje świeże pliki, w tym nieudaną próbę, i pomija starsze artefakty. [Proweniencja kolekcji](gpu-i04-5e0/qa/collection-provenance.json).''')
c=json.loads((d/'disabled-control-summary.json').read_text())
def n(value):return f'{value:.5f}'.replace('.',',')
table='| Build / sonda wyłączona | GPU misja | GPU 16k | GPU 32k | CPU submit misji |\n| --- | ---: | ---: | ---: | ---: |\n'
for name in ('D','E0'):
 vals=[c[scene][name]['gpu_step_ms']['median_of_medians'] for scene in ('throughput_mission','throughput_stress_16k','throughput_stress_32k')]
 table+='| '+name+' | '+' | '.join(n(x) for x in vals)+' | '+n(c['throughput_mission'][name]['cpu_submit_ms']['median_of_medians'])+' |\n'
t=t.replace('PENDING_CONTROL','''Dodatkowo porównano niezmienione D i E0 z wyłączoną nową sondą w identycznym dotychczasowym protokole `--gpu-stabilization-profile`: po trzy procesy, naprzemienny porządek tworzący ABBA. **6/6 kod 0, analyzer 108 wierszy, zero ostrzeżeń.** Oba EXE są identyfikowane hashami, a shadery mają identyczną zawartość.

'''+table+'''
E0 ma w tej małej serii medianę GPU 32k dłuższą o około 1,15%, a misji o 0,47%; zakresy prób nakładają się. Nie wykonano testu istotności i nie deklarujemy zerowego kosztu wyłączonych haków CPU. CPU submit pozostaje poniżej celu 0,1 ms. [Wyniki kontroli](gpu-i04-5e0/disabled-control-summary.json), [procesy](gpu-i04-5e0/disabled-control/runs.csv), [hashe źródeł i EXE](gpu-i04-5e0/source-state.json). Nie zmieniano ustawień systemu ani sterownika; telemetria dotyczy całych procesów, także inicjalizacji i walidacji.''')
t=t.replace('zerowania i odczytu maksymalnej kompresji cieczy','zerowania i diagnostyki maksymalnej kompresji cieczy')
t=t.replace('Następny eksperyment I04.5-G:', 'Najpierw wymaga wyjaśnienia baza QA sprzężenia. Po jej odbiorze następny eksperyment I04.5-G:')
debug=window['debug'];t=t.replace('PENDING_PACKAGE',f'''Paczka `StrefaZero-Windows-x64-I04_5-E0` przeszła własne `--gpu-cost-check --gpu-storage auto` i `--gpu-profile-check` (kod 0); `--opengl33 --gpu-cost-check` zwróciło oczekiwane 77. Kontrola widocznego okna przeszła: 900 klatek, 1798 ticków, wynik 3 i brak utraty czasu. Mediana całej klatki po 30 klatkach rozgrzewki: {n(window['median_ms'])} ms, p95 {n(window['p95_ms'])} ms, maksimum {n(window['max_ms'])} ms, z przechwytywaniem obrazu w pętli.

Fokus był aktywny w {window['focused_frames']}/{window['analyzed_frames']} analizowanych klatkach, minimalizacja: {window['minimized_frames']}. To kontrola diagnostyczna, nie odbiór wymaganego okna z fokusem. KHR_debug id131186 według fazy odbioru: runtime {debug['phase_runtime_id131186']}, validation {debug['phase_validation_id131186']}; logger zgubił {debug['dropped']} komunikatów. Asynchroniczny callback nie dowodzi miejsca pamięci ani przyczyny historycznego spowolnienia. [Kontrole paczki](gpu-i04-5e0/package-checks.json), [okno i fazy debug](gpu-i04-5e0/package-window-summary.json).

EXE i assety odpowiadają finalnemu buildowi/źródłom. Paczka ma manifest SHA256 i kontrolę CRC ZIP. Jest kompilacją diagnostyczną z jawnym otwartym wynikiem QA 37/38, bez nowego tagu wydania. Zweryfikowano niezmienność {source['historical_files_verified']} plików historycznych raportów/danych oraz sum zewnętrznych paczek A/B/C/D.''')
assert 'PENDING' not in t;p.write_text(t)

p=s/'docs/PLAN_ROZWOJU_GPU.md';t=p.read_text()
t=re.sub(r'^Status: .*$', 'Status: **I04.5-A/F wdrożone; B/C/D zbadane jako eksperymenty; E0 zakończone jako diagnostyka. E0: 16/16 pomiarów i 6/6 kontroli wyłączonej sondy. Pełne QA 37/38: otwarta zmienność gpu.coupling odtworzona także na D. Domyślne full/direct, obie opcje C i nowa diagnostyka OFF. Cel 1,5 ms nadal otwarty; lokalny E odroczony. Najpierw baza QA, potem punktowy eksperyment PBF G; I05–I11 nadal zaplanowane**.',t,count=1,flags=re.M)
t=re.sub(r'^Aktualne QA po D:.*$', 'Aktualne QA po E0: **37/38**, bez zmiany limitów i tolerancji; nowe testy diagnostyki 3/3 PASS. Powtórka E0 przeszła, niezmienione D odtworzyło ten sam błąd sprzężenia. Przyczyna nadal otwarta. Historyczne wyniki C, B i D pozostają w raportach.',t,count=1,flags=re.M)
t=t.replace('a odczyty sąsiadów i aktualne QA — [raport D](I04_5D_GPU_NEIGHBOR_READS.md).','odczyty sąsiadów — [raport D](I04_5D_GPU_NEIGHBOR_READS.md), a koszty przebiegów, Dmax i aktualne QA — [raport E0](I04_5E0_GPU_COSTS.md).')
pattern=r'^- \[ \] \*\*E — warunkowy lokalny `Dmax`:.*$'
replacement='''- [x] **E0 — profil kosztów i Dmax w czasie:** 16/16 procesów w porządku Latin i 6/6 kontroli wyłączonej sondy; 384 snapshoty, brak pominiętych geometrycznych par, kontrola outliera 96,2. W misji dominują gęstość/korekta PBF, przy 16k/32k kontakty ziaren. Dmax do 0,125/0,25/0,4375 w nagranych scenach; snapshoty eksplozji do około 50% więcej kandydatów niż świeży indeks. Jawnie zmierzono narzut; diagnostyka pozostaje OFF. Pełne QA 37/38 i znany błąd sprzężenia opisuje raport E0.
- [ ] **E — warunkowy lokalny `Dmax`:** odroczony po E0; profil nie uzasadnia obecnie dodatkowego indeksu lokalnego. Zachować globalny konserwatywny zakres. Wrócić po potwierdzeniu kosztu cięższych outlierów; potrzebny dowód kompletności także dla sąsiada przeniesionego z odległego kafelka, fallback i porównanie all-pairs przy eksplozji, przekroczeniu kafelków, resecie i destrukcji. Stałe halo 3 × 3 nie jest dowodem.'''
assert re.search(pattern,t,flags=re.M);t=re.sub(pattern,replacement,t,count=1,flags=re.M)
anchor='- [ ] **Raport:**';i=t.index(anchor)
t=t[:i]+'- [ ] **G — punktowe przebiegi PBF:** najpierw wyjaśnić zmienność bazy QA sprzężenia. Po odbiorze bazy porównać grupy PBF 64/128/256 przy pozostawieniu ziaren i redukcji na 256, bez zmiany równań, iteracji i tolerancji. Osobno rozważyć brak aliasowania tylko dla dowiedzionych, rozdzielonych banków Jacobi. Zachować referencję, pełne testy i pomiar narzutu/okna; nie zakładać przyspieszenia.\n'+t[i:]
t=t.replace('Następne jest przypisanie kosztu przebiegów i pomiar chwilowego Dmax przed decyzją E.', '[E0](I04_5E0_GPU_COSTS.md) przypisało koszt i zbadało trajektorie: lokalny E odroczony, PBF wskazane do punktowego G. Najpierw wymaga wyjaśnienia baza QA sprzężenia (37/38).')
anchor='\n\nPo każdej zakończonej iteracji dopisać:'
t=t.replace(anchor,'\n| I04.5-E0 | Diagnostyka zakończona 2026-10-30; pełne QA otwarte | [Raport E0](I04_5E0_GPU_COSTS.md): 16/16 pomiarów, 6/6 kontroli, 384 snapshoty; sondy OFF. Dominujące PBF/ziarna, Dmax do 0,4375 po eksplozjach. Pełne QA 37/38; powtórka E0 PASS, to samo sprzężenie FAIL na D | Najpierw wyjaśnienie bazy QA sprzężenia, potem punktowy PBF G. Lokalny E warunkowy; cel 1,5 ms otwarty |'+anchor)
anchor='\n## 8. Materiały pomocnicze'
t=t.replace(anchor,'\n- **2026-10-30, E0:** odraczamy lokalny Dmax po profilu trajektorii i kosztów. Wąskie gardła: PBF w misji i kontakt ziaren w 16k/32k. Sonda passes ma znaczny koszt CPU, bounds/snapshots zwiększają GPU; wszystkie pozostają OFF. Pełny CTest 37/38 ujawnił znaną zmienność sprzężenia, odtworzoną również na D. Zachowujemy nieudaną próbę, tolerancje i limity. Najpierw baza QA, potem eksperyment rozmiaru grupy wyłącznie PBF; I05 nie jest rozpoczęte.\n'+anchor)
p.write_text(t)

p=s/'README.md';t=p.read_text();anchor='\n[Plan rozwoju GPU, wspólnego świata, wody i efektów]'
t=t.replace(anchor,'\n**I04.5-E0 — profil kosztów i Dmax:** **16/16 pomiarów**, **6/6 kontroli**, 384 snapshoty. Profil wskazuje PBF w misji i kontakt ziaren w dużych scenach; lokalny Dmax odroczono, sondy pozostają OFF. Pełne QA **37/38**: zmienność wcześniejszego testu sprzężenia odtworzono także na niezmienionym D. Najpierw wymaga wyjaśnienia baza QA, potem punktowe strojenie PBF. [Raport E0](docs/I04_5E0_GPU_COSTS.md).\n'+anchor)
t=t.replace('Aktualny zakres po review: profil kosztów przebiegów i chwilowego Dmax przed warunkowym E.', 'Aktualny zakres po review: wyjaśnienie zmienności QA sprzężenia po E0, następnie punktowy eksperyment PBF G. Lokalny E pozostaje warunkowy.')
p.write_text(t)

p=s/'docs/REVIEW_I04_ROZSTRZYGNIECIA.md';t=p.read_text()
t=re.sub(r'^Status: .*$', 'Status: **A/F wdrożone; B/C/D zbadane; E0 zakończone jako diagnostyka (16/16 pomiarów, 6/6 kontroli). Pełne QA 37/38: wcześniejsze sprzężenie wymaga wyjaśnienia, ten sam błąd wystąpił na D. Domyślne full/direct, opcje C i sondy OFF. Lokalny E odroczony; po odbiorze bazy QA planowany punktowy PBF G. Cel 1,5 ms nadal otwarty**. [A/F](I04_5_GPU_STABILIZATION.md), [C](I04_5C_GPU_WORK.md), [B](I04_5B_GPU_TRANSPORT.md), [D](I04_5D_GPU_NEIGHBOR_READS.md), [E0 i aktualne QA](I04_5E0_GPU_COSTS.md).',t,count=1,flags=re.M)
t=t.replace('D zbadane, full domyślne; E odroczone do profilu w czasie','D zbadane; E0 zbadało trajektorie i koszty, E odroczone')
t=t.replace('\nBadanie D porównało', '\nStan po D, przed E0: badanie D porównało')
t+='\nE0 wykonało 16/16 pomiarów i 6/6 kontroli, z 384 snapshotami bez pominiętych geometrycznych par. Profil wskazał PBF i kontakty ziaren; lokalnego Dmax nie wdrożono. Pełne QA 37/38 zachowuje błąd sprzężenia: powtórka E0 przeszła, niezmienione D odtworzyło ten sam błąd. Nie zmieniono tolerancji. Najpierw baza QA, potem punktowy eksperyment PBF G. [Raport E0](I04_5E0_GPU_COSTS.md).\n'
p.write_text(t)

p=r/'outputs/03_PLAN_PRAC.md';t=p.read_text();t=t.replace('Następne jest przypisanie kosztu przebiegów i pomiar chwilowego Dmax przed warunkowym E.', 'Diagnostyka [E0](strefa-zero/docs/I04_5E0_GPU_COSTS.md): 16/16 pomiarów, 6/6 kontroli, 384 snapshoty; lokalny E odroczony. Pełne QA 37/38: najpierw wyjaśnienie zmienności sprzężenia odtworzonej także na D, potem punktowy eksperyment PBF G.')
p.write_text(t)

package=r/'outputs/StrefaZero-Windows-x64-I04_5-E0'
(package/'README.md').write_text('''# Strefa Zero — diagnostyczne I04.5-E0

Windows x64, 2026-10-30. Uruchom `StrefaZero.exe`. Misja wymaga OpenGL 4.3; interfejs/CPU obsługuje OpenGL 3.3. Baza v0.7.0-i04 + lokalne A/F/C/B/D/E0.

Dodano diagnostykę kosztów przebiegów oraz Dmax w czasie. **Zwykła gra ma sondy OFF, full/direct i obie opcje C OFF.** Profilowanie nie jest optymalizacją fizyki: cel 1,50 ms pozostaje otwarty.

Pełny CTest **37/38**: nie przeszedł wcześniejszy test sprzężenia, który odtworzył ten sam błąd na niezmienionym D. Powtórka E0 przeszła; nie uznano tego za pełny odbiór QA. Nowe kontrole diagnostyki 3/3 PASS. To paczka diagnostyczna z jawnym otwartym QA, bez nowego tagu wydania.

```powershell
.\\StrefaZero.exe --gpu-cost-check --profile .\\qa-cost
.\\tools\\profile-cost.ps1 -Executable "$PWD\\StrefaZero.exe" -Output "$PWD\\results" -Repetitions 4
python .\\tools\\analyze-stabilization.py --input .\\results
python .\\tools\\analyze-cost.py --input .\\results
```

Tryby `--gpu-cost-profile --gpu-cost-mode off|passes|bounds|snapshots` mają osobno mierzony narzut. Off jest referencją wydajności. Passes dodaje timestampy i może istotnie zwiększyć koszt CPU; bounds/snapshots wykonują kopie GPU, a snapshots rezerwuje około 83,41 MiB stagingu. Dane CPU są czytane po partii. Duże sceny snapshotów mają niezależny indeks CPU jako referencję, małe testy all-pairs.

Wyniki własnych kontroli paczki: `QA.txt`. Integralność: `SHA256SUMS.txt`. Raport i dane w projekcie: `docs/I04_5E0_GPU_COSTS.md`, `docs/gpu-i04-5e0/`.
''')
(package/'QA.txt').write_text(f'''Strefa Zero — diagnostyczne I04.5-E0, 2026-10-30
EXE SHA256: {source['exe_sha256']}

Release MSVC /W4 /WX, IPO: PASS. Shadery identyczne z D.
Pełny CTest: 37/38, kod 8, 330.27 s. FAIL: gpu.coupling, geometryczne porównanie local z referencją.
Nowe testy sondy AoS/SoA/auto immutable: 3/3 PASS, wszystkie tryby max_state_error=0.
Powtórka gpu.coupling na E0: PASS. Kontrola na niezmienionym D: FAIL, ten sam komunikat.
Nie zmieniono testu, limitów ani tolerancji; pełny wynik 37/38 pozostaje otwarty.
Seria kosztów: 16/16 procesów, 192 wiersze, zero ostrzeżeń; 384 snapshoty i 768 wierszy przestrzennych bez pominiętych par.
Kontrola wyłączonej sondy D/E0: 6/6 procesów, 108 wierszy, zero ostrzeżeń.

Kontrole paczki:
--gpu-cost-check --gpu-storage auto: kod 0 PASS.
--gpu-profile-check: kod 0 PASS.
--opengl33 --gpu-cost-check: oczekiwany kod 77 PASS.
Widoczne okno: kod 0, wynik 3, brak utraty czasu. Fokus {window['focused_frames']}/{window['analyzed_frames']}; ta próba nie zalicza odbioru okna z fokusem.

Domyślne full/direct, obie opcje C i nowa diagnostyka OFF. Lokalnego Dmax nie wdrożono.
Cel GPU 1,50 ms, baza QA sprzężenia i przyczyna historycznej zmienności nadal otwarte.
EXE i assety odpowiadają finalnemu buildowi/źródłom. To paczka diagnostyczna, bez nowego tagu wydania.
Raport w projekcie: docs/I04_5E0_GPU_COSTS.md i docs/gpu-i04-5e0/.
''')
print('E0 report, plan and diagnostic package QA updated; full QA remains 37/38.')
