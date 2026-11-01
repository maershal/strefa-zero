from pathlib import Path
import json,re
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero';d=r/'work/i04-5q-results'
a=json.loads((d/'coupling-qa-analysis.json').read_text());qa=json.loads((d/'ctest-summary.json').read_text());w=json.loads((d/'package-window-summary.json').read_text())
frozen=json.loads((d/'source-state.json').read_text());perf=json.loads((d/'performance-controls.json').read_text());package_checks=json.loads((d/'package-checks.json').read_text())
assert qa['tests']==38 and qa['passed']==38 and not qa['failed']
assert a['legacy_identical_reference']['rejected']==1 and a['paired']['trials']==12 and a['paired']['disabled_faults_rejected']==12
assert all(x['exit_code']==(77 if 'gl33' in name else 0) and not x['timed_out'] for name,x in package_checks.items())
def n(x,digits=5):return f'{x:.{digits}f}'.replace('.',',')
report=f'''# I04.5-Q — wiarygodna weryfikacja sprzężenia

Prace: 2026-10-30. Baza: lokalne A/F/C/B/D/E0 na `6f318ea`. **Pełny CTest 38/38 w nowym protokole Q; 12/12 powtórek, 1080 wspólnych stanów i 12/12 odrzuconych prób z celowo wyłączonym sprzężeniem.** Zmieniono metodę odbioru testu, nie solver. Nie oznacza to zaliczenia dawnego porównania niezależnych trajektorii ani zapewnienia bitowej deterministyczności całej symulacji.

## Co powodowało fałszywe odrzucenie

Stara reguła porównywała po 90 krokach trzy niezależne światy: referencję A, referencję B i local. Progi wynosiły `max(minimum, 2 × różnica A/B)`. Jedna przypadkowo zbieżna para referencji mogła więc ustawić próg dużo ostrzej niż inna para, mimo identycznej implementacji. Był to problem kalibracji kryterium, nie dowód wykrycia konkretnej usterki sprzężenia.

Dodano kontrolę A/A: trzeci świat także używa referencji, z tymi samymi ustawieniami, sceną, wejściem i regułą oceny. **Stara reguła odrzuciła 1 z 16 takich prób.** W próbie 9 różnica centroidu A/B wyniosła 0,00341257, próg `max(0,02; 2 × 0,00341257)` dał 0,02, a różnica trzeciej identycznej referencji względem A wyniosła 0,023376. Samo to wystarczyło do odrzucenia.

To wynik tej serii na jednym GPU, nie oszacowanie ogólnej częstości błędów. Seria powstała na wstępnym buildzie kontrolnym `{frozen['reference_control_build_sha256']}`. [Wszystkie 16 wyników](gpu-i04-5q/reference-only-results.json), [próba 9](gpu-i04-5q/reference-only/9/coupling-tests.txt), [rekonstrukcja reguły](gpu-i04-5q/coupling-qa-analysis.json), [hash kontroli](gpu-i04-5q/control-build.json).

Kod zawiera kompakcję aktywnych ID i reakcji przez atomiki oraz sumowanie float według uzyskanej kolejności. Stałe drzewo redukcji nie ustala kolejności jego wejścia. To mechanizm zgodny z obserwowaną zmiennością; nie izolowano wszystkich możliwych źródeł różnic GPU. Dowód A/A dotyczy bezpośrednio wadliwości reguły odbioru. [Referencja i test](../src/gpu/coupling_validation.cpp), [kompakcja ID](../assets/shaders/granular.comp), [redukcja referencyjna](../assets/shaders/mission.comp), [kolejka reakcji](../assets/shaders/particle_common.glsl).

## Nowy protokół Q i jego granice

| Własność | Dawna reguła | Q |
| --- | --- | --- |
| Dane wejściowe porównania | trzy niezależne długie trajektorie | ten sam autorytatywny stan przed każdym krokiem |
| Referencja kontrolna | jedna różnica dwóch pełnych trajektorii kalibruje próg | powtórka referencji kontroluje odtwarzanie, bez rozszerzania progów |
| Próg | losowo zależny od pojedynczej pary | stałe najniższe dotychczasowe minima |
| Zakres | local względem referencji | referencja, local i produkcyjne combined |
| Negatywna kontrola fizyki | brak celowo usuniętego sprzężenia w tej scenie | rzeczywiste wyłączenie sprzężenia musi zostać odrzucone |

Referencyjny świat nadal wykonuje ciągłą trasę 90 ticków. Scena ma 1443 cząstki cieczy/ziaren, podłogę i kontakt z kapsułą; w ticku 45 postać jest przenoszona jak w dawnym teście. Przed każdym krokiem zapisuje się pełny stan cząstek i postaci. Repliki otrzymują go przez produkcyjne ładowanie danych i testową funkcję odtworzenia stanu misji, z własną generacją oraz zachowanym licznikiem ticków silnika. Następnie każda wykonuje ten sam rzeczywisty krok GPU i to samo wejście.

Odtwarzanie dotyczy tej sceny bez pocisków i zdarzeń destrukcji. Nie jest ogólnym zapisem sesji ani realizacją I05. Pola masy i materiału każdego aktywnego ID muszą pozostać identyczne; stan musi być skończony, kolejki opróżnione, bez przepełnienia, a realne reakcje obecne w local i combined. Pozycję porównuje się z kompensacją. Odtwarzanie i synchroniczne odczyty występują wyłącznie w walidacji, nie w pętli gry ani benchmarku.

Nie podniesiono wartości progów: **0,05 dla średniej różnicy cząstek, 0,02 dla średniego przesunięcia wektora cząstek i 0,15 dla postaci**. Teraz obowiązują jako stałe budżety każdego kroku i skumulowanego defektu na wspólnych stanach. Sumuje się nieujemną średnią różnicę pozycji, a osobno wektory średniego przesunięcia cząstek i postaci; normy tych sum także muszą zmieścić się w budżetach. Centroid jest średnią arytmetyczną aktywnych cząstek, nie środkiem masy.

**Zmienił się eksperyment i kryterium odbioru.** Skumulowany defekt przy wspólnych stanach nie jest granicą rozbieżności dwóch niezależnych, nieliniowych trajektorii. Dawna trasa 90 kroków nadal sprawdza skończoność, ID i rzeczywiste reakcje oraz zapisuje stare różnice; jej statystyczny iloraz jest diagnostyką. W serii nowych testów dawny warunek liczbowy nie przeszedł w {a['paired']['legacy90_diagnostic_rejections']}/12 prób — ten wynik zachowano. Nie przedstawiamy 38/38 Q jako naprawy ani zaliczenia dawnego warunku.

Można jawnie uruchomić dawną regułę dla local albo kontrolę trzech referencji:

'''
report+=r'''```powershell
.\StrefaZero.exe --gpu-coupling-check --profile .\qa-q
.\StrefaZero.exe --gpu-coupling-legacy-check --profile .\qa-legacy
.\StrefaZero.exe --gpu-coupling-control --profile .\qa-reference-only
python .\scripts\analyze-coupling-qa.py --input '<katalog surowych serii Q>'
```

'''
report+='''Dwa ostatnie tryby zachowują dawną regułę i mogą zwrócić błąd. Standardowe `--gpu-coupling-check` korzysta z Q. Nie zmieniono shaderów, równań, iteracji ani ustawień sterownika. Nie dodano synchronizacji do normalnej gry.

## Wyniki i wykrywanie rzeczywistej usterki

Wykonano 12 osobnych natywnych procesów. Każdy sprawdził 90 checkpointów w czterech rolach, łącznie 4320 porównań. Poniżej największe wartości skumulowanych defektów w tych 12 próbach. Wszystkie wyniki, granice per krok i sumy zostały niezależnie uzgodnione przez skrypt analizy CSV.

| Rola | Suma średnich błędów pozycji | Norma sumy przesunięć centroidu | Norma sumy przesunięć postaci | Wynik |
| --- | ---: | ---: | ---: | --- |
'''
for role,label in [('reference_repeat','Powtórka referencji'),('local','Local'),('combined','Combined'),('disabled_fault','Sprzężenie wyłączone celowo')]:
 x=a['paired']['maxima'][role];outcome='12/12 odrzucone, oczekiwane' if role=='disabled_fault' else '12/12 PASS'
 report+=f"| {label} | {x['mean_sum']:.8g} | {x['center_bias']:.8g} | {x['actor_bias']:.8g} | {outcome} |\n"
report+='''| Budżet | 0,05 | 0,02 | 0,15 | każdy krok i suma |

Kontrola usterki używa istniejącego wyłączenia sprzężenia w prawdziwych shaderach GPU, nie zmiany zapisanych liczb. Została odrzucona w każdej próbie. Pozostają też wcześniejsze testy kolejki: pusta partia, ogony grup, pełne dwa kanały 65536, hierarchia 64/128/256, podparcie, replay i reset. Wzmocniono niezależny test: znana suma reakcji musi nie tylko trafić do `last_sum`, lecz także faktycznie przesunąć postać w wolnej przestrzeni z błędem <0,00001 względem sumy double na CPU. [Dane każdej powtórki](gpu-i04-5q/paired-repeat/), [podsumowanie i maksima](gpu-i04-5q/coupling-qa-analysis.json), [implementacja replay](../src/gpu/coupling_replay.cpp).

**Pełny CTest: 38/38 PASS, kod 0, 302,20 s w protokole Q.** Przeszły również misje, warianty transportu i C/D, diagnostyka E0, sąsiedztwo, fizyka, UI, CPU i GL 3.3. Historyczne 37/38 E0 pozostaje bez zmian w jego raporcie. [Log](gpu-i04-5q/ctest.log), [JUnit](gpu-i04-5q/ctest.xml), [podsumowanie](gpu-i04-5q/ctest-summary.json), [kolekcja świeżych plików](gpu-i04-5q/qa/collection-provenance.json).

## Kontrola zwykłej gry i paczki

Porównano niezmienione E0 i Q w dotychczasowym `--gpu-stabilization-profile`: po trzy osobne procesy, naprzemienny porządek tworzący ABBA. **6/6 kod 0, 108 wierszy analizy, zero ostrzeżeń.** GTX 1650, NVIDIA 610.47, GL 4.3, Release MSVC /W4 /WX i IPO. Elapsed, liczniki ON, HUD auto/2, 120 ticków rozgrzewki i 240 pomiarowych na świeżą scenę; full/direct, opcje C i diagnostyka kosztów OFF.

Mediana trzech median, ms/tick; p95 jest medianą trzech percentyli, nie percentylem połączonych danych.

| Build | GPU misja | Misja p95 | GPU 16k | GPU 32k | CPU submit misji |
| --- | ---: | ---: | ---: | ---: | ---: |
'''
for name in ('E0','Q'):
 vals=[perf[scope][name]['gpu_step_ms']['median_of_medians'] for scope in ('throughput_mission','throughput_stress_16k','throughput_stress_32k')]
 p95=perf['throughput_mission'][name]['gpu_step_ms']['median_of_p95'];cpu=perf['throughput_mission'][name]['cpu_submit_ms']['median_of_medians']
 report+=f'| {name} | {n(vals[0])} | {n(p95)} | {n(vals[1])} | {n(vals[2])} | {n(cpu)} |\n'
report+=f'''
Q nie jest optymalizacją solvera. Nie wykonano testu istotności i nie przypisujemy różnic czasu zmianie metody QA; misja ma nieco niższą medianę, ale wyższe p95. Shadery i pozostałe assety są identyczne z E0. Cel GPU 1,50 ms nadal otwarty. [Pełne zakresy](gpu-i04-5q/performance-controls.json), [procesy](gpu-i04-5q/disabled-control/runs.csv), [build](gpu-i04-5q/build-config.json).

Własne kontrole paczki: `--gpu-coupling-check` i `--gpu-profile-check` kod 0; wymuszone `--opengl33 --gpu-coupling-check` oczekiwane 77. Widoczne okno: 900 klatek, 1798 ticków, wynik 3, brak utraty czasu; fokus {w['focused']}/{w['frames']}, minimalizacja {w['minimized']}. Mediana pełnej klatki po 30 klatkach rozgrzewki {n(w['median_ms'])} ms, p95 {n(w['p95_ms'])} ms, p99 {n(w['p99_ms'])} ms, maksimum {n(w['max_ms'])} ms, wliczając przechwytywanie obrazu. To jedna próba, nie sześć wymaganych do odbioru całego I04.5.

KHR_debug id131186 według fazy odbioru: runtime {w['debug']['phase_runtime_id131186']}, validation {w['debug']['phase_validation_id131186']}; logger zgubił {w['debug']['dropped']} komunikatów. Asynchroniczny callback nie dowodzi przyczyny historycznych spowolnień ani stałej rezydencji VRAM. [Kontrole paczki](gpu-i04-5q/package-checks.json), [okno i debug](gpu-i04-5q/package-window-summary.json).

EXE SHA256 `{frozen['exe_sha256']}`. EXE i assety odpowiadają finalnemu buildowi/źródłom; ZIP ma własne sumy SHA256 i kontrolę CRC. To paczka diagnostyczna, bez nowego tagu wydania. Zweryfikowano niezmienność {frozen['historical_files_verified']} plików historycznych raportów/danych i sumy paczek A/B/C/D/E0. [Stan źródeł i hashy](gpu-i04-5q/source-state.json).

## Decyzja

Q przywraca użyteczny odbiór sprzężenia w jawnie zmienionym protokole. Długie trajektorie nadal mogą różnić się liczbowo; nie zamknięto wszystkich przyczyn tej zmienności ani dawnego problemu warunków GPU. Następny etap to punktowy eksperyment PBF G, z Q wykonywanym w każdej konfiguracji. Lokalne Dmax pozostaje odroczone, a I05 czeka na ocenę całego I04.5. Wyniki i deklaracje dostarczonego review nadal są materiałem do weryfikacji, nie automatycznym odbiorem.
'''
(s/'docs/I04_5Q_GPU_COUPLING_QA.md').write_text(report)

p=s/'docs/PLAN_ROZWOJU_GPU.md';t=p.read_text()
t=re.sub(r'^Status: .*$', 'Status: **A/F wdrożone; B/C/D zbadane; E0 zakończone jako diagnostyka; Q zakończone ze zmianą metody odbioru sprzężenia. 38/38 w protokole Q, 12/12 powtórek, 12/12 wykrytych wyłączeń sprzężenia. Stara reguła odrzuciła identyczne referencje 1/16 i pozostaje diagnostyką. Solver bez zmian; cel 1,5 ms otwarty. Domyślne full/direct, opcje C i sondy OFF. Następne PBF G; lokalny E odroczony, I05–I11 nadal zaplanowane**.',t,count=1,flags=re.M)
t=re.sub(r'^Aktualne QA po E0:.*$', 'Aktualne QA: **38/38 w zmienionym protokole Q**. Stałe najniższe progi stosuje się do wspólnych checkpointów i skumulowanego defektu. To nie zaliczenie dawnej reguły porównania niezależnych trajektorii: nadal jest raportowana i dostępna jawnie. Historyczne 37/38 E0 pozostaje w raporcie.',t,count=1,flags=re.M)
t=t.replace('a koszty przebiegów, Dmax i aktualne QA — [raport E0](I04_5E0_GPU_COSTS.md).','koszty przebiegów i Dmax — [raport E0](I04_5E0_GPU_COSTS.md), a zmianę metody odbioru sprzężenia i aktualne QA — [raport Q](I04_5Q_GPU_COUPLING_QA.md).')
anchor='- [ ] **G — punktowe przebiegi PBF:**';i=t.index(anchor)
t=t[:i]+'- [x] **Q — wiarygodna weryfikacja sprzężenia:** wykazano odrzucenie identycznych referencji przez starą regułę (1/16). Wprowadzono odtwarzanie wspólnych stanów przed krokiem, stałe najniższe progi i skumulowane budżety, kontrolę rzeczywiście wyłączonego sprzężenia oraz niezależne sprawdzenie zastosowania reakcji do postaci. 12/12 powtórek, 1080 checkpointów, 12/12 odrzuconych usterek, pełne QA 38/38 w nowym protokole. Stare porównanie 90 kroków jest diagnostyką i nadal może nie przejść; solver bez zmian.\n'+t[i:]
t=t.replace('najpierw wyjaśnić zmienność bazy QA sprzężenia. Po odbiorze bazy porównać grupy PBF','po odbiorze Q porównać grupy PBF')
t=t.replace('Zachować referencję, pełne testy i pomiar narzutu/okna; nie zakładać przyspieszenia.', 'Wykonywać Q w każdej konfiguracji. Zachować referencję, pełne testy i pomiar narzutu/okna; nie zakładać przyspieszenia.')
t=t.replace('Najpierw wymaga wyjaśnienia baza QA sprzężenia (37/38).', '[Q](I04_5Q_GPU_COUPLING_QA.md) zmieniło niewiarygodną regułę odbioru: 38/38 w nowym protokole, bez zmiany solvera. Następne G; zmienność długich trajektorii pozostaje jawna.')
anchor='\n\nPo każdej zakończonej iteracji dopisać:'
t=t.replace(anchor,'\n| I04.5-Q | Zakończone 2026-10-30; metoda QA zmieniona jawnie | [Raport Q](I04_5Q_GPU_COUPLING_QA.md): identyczna referencja odrzucona 1/16 przez starą regułę, 12/12 nowych powtórek, 1080 checkpointów, wszystkie wyłączenia sprzężenia wykryte; 38/38 Q. Solver bez zmian, stary warunek nadal diagnostyczny | Punktowe PBF G, z Q dla każdej konfiguracji. Cel 1,5 ms i przyczyny zmienności długich trajektorii otwarte |'+anchor)
anchor='\n## 8. Materiały pomocnicze'
t=t.replace(anchor,'\n- **2026-10-30, Q:** zmieniamy metodę akceptacji sprzężenia po wykazaniu odrzucenia identycznych referencji 1/16. Nie podnosimy wartości progów: najniższe dawne minima ograniczają krok i skumulowany defekt na wspólnych stanach. Prawdziwe wyłączenie sprzężenia jest wykrywane. 38/38 dotyczy nowego protokołu; nie oznacza zaliczenia starego ilorazu ani deterministyczności solvera. Następne PBF G; I04.5 pozostaje otwarte.\n'+anchor)
p.write_text(t)

p=s/'README.md';t=p.read_text();anchor='\n[Plan rozwoju GPU, wspólnego świata, wody i efektów]'
t=t.replace(anchor,'\n**I04.5-Q — metoda QA sprzężenia:** stara reguła odrzuciła identyczne referencje 1/16. Nowe odtwarzanie wspólnych stanów i stałe budżety przeszły **12/12** powtórek, wykrywając każde celowe wyłączenie sprzężenia. **38/38 w nowym protokole Q**; dawna reguła pozostaje diagnostyką, solver nie został zmieniony. Następne jest punktowe PBF G. [Raport Q](docs/I04_5Q_GPU_COUPLING_QA.md).\n'+anchor)
t=t.replace('Aktualny zakres po review: wyjaśnienie zmienności QA sprzężenia po E0, następnie punktowy eksperyment PBF G.', 'Aktualny zakres po review: punktowy eksperyment PBF G po jawnej zmianie metody QA w Q.')
t=t.replace('**I04.5-E0 — profil kosztów i Dmax:**','**Stan po I04.5-E0, przed Q — profil kosztów i Dmax:**')
p.write_text(t)

p=s/'docs/REVIEW_I04_ROZSTRZYGNIECIA.md';t=p.read_text()
t=re.sub(r'^Status: .*$', 'Status: **A/F wdrożone; B/C/D zbadane; E0 zakończone; Q zmieniło metodę odbioru sprzężenia po kontroli identycznych referencji (1/16 odrzuconych). 38/38 w nowym protokole Q, 12/12 powtórek i 12/12 wykrytych usterek. Dawna reguła nadal diagnostyczna, solver bez zmian. Domyślne full/direct, opcje C i sondy OFF. Następne PBF G; lokalny E i cel 1,5 ms otwarte**. [A/F](I04_5_GPU_STABILIZATION.md), [C](I04_5C_GPU_WORK.md), [B](I04_5B_GPU_TRANSPORT.md), [D](I04_5D_GPU_NEIGHBOR_READS.md), [E0](I04_5E0_GPU_COSTS.md), [Q i aktualne QA](I04_5Q_GPU_COUPLING_QA.md).',t,count=1,flags=re.M)
t=t.replace('\nE0 wykonało 16/16', '\nStan po E0, przed Q: E0 wykonało 16/16')
t+='\nQ wykazało fałszywe odrzucenie identycznych referencji przez dawną regułę (1/16). Metodę odbioru zmieniono jawnie na wspólne checkpointy i stałe budżety; 12/12 powtórek i wykryte wszystkie celowe wyłączenia sprzężenia. Pełne 38/38 dotyczy nowego protokołu. Nie jest to zaliczenie dawnego ilorazu ani naprawa deterministyczności fizyki; stare wyniki pozostają. [Raport Q](I04_5Q_GPU_COUPLING_QA.md).\n'
p.write_text(t)

p=r/'outputs/03_PLAN_PRAC.md';t=p.read_text();lines=t.splitlines()
lines[2]='Aktualizacja odsyłacza 2026-10-30: bieżący plan techniczny to [PLAN_ROZWOJU_GPU.md](strefa-zero/docs/PLAN_ROZWOJU_GPU.md), a [rejestr review](strefa-zero/docs/REVIEW_I04_ROZSTRZYGNIECIA.md) przypisuje uwagi do etapów. [Q](strefa-zero/docs/I04_5Q_GPU_COUPLING_QA.md) zmieniło metodę odbioru sprzężenia po wykazaniu fałszywego odrzucenia identycznych referencji: 38/38 w nowym protokole, 12/12 powtórek i wykryte wszystkie celowe wyłączenia sprzężenia. Solver bez zmian, stara reguła nadal diagnostyczna. Następne punktowe PBF G. Domyślne full/direct, opcje C i sondy OFF; lokalny E oraz cel 1,5 ms pozostają otwarte. I05–I11 nadal zależą od odbioru. Poniżej zachowano ogólny podział produktu.'
p.write_text('\n'.join(lines)+'\n')

package=r/'outputs/StrefaZero-Windows-x64-I04_5-Q'
(package/'README.md').write_text(r'''# Strefa Zero — diagnostyczne I04.5-Q

Windows x64, 2026-10-30. Uruchom `StrefaZero.exe`. Misja wymaga OpenGL 4.3; interfejs/CPU obsługuje OpenGL 3.3. Baza v0.7.0-i04 + lokalne A/F/C/B/D/E0/Q.

Naprawiono metodę testowania sprzężenia, bez zmiany solvera. Stara reguła odrzuciła identyczne referencje 1/16. Nowe wspólne checkpointy, stałe budżety i kontrola celowo wyłączonego sprzężenia przeszły 12/12 powtórek. **Pełne 38/38 dotyczy nowego protokołu Q.** Nie oznacza zaliczenia starego porównania niezależnych trajektorii ani deterministycznej fizyki.

```powershell
.\StrefaZero.exe --gpu-coupling-check --profile .\qa-q
.\StrefaZero.exe --gpu-profile-check --profile .\qa-profile
```

Stara reguła jest dostępna przez `--gpu-coupling-legacy-check`; kontrola identycznych referencji przez `--gpu-coupling-control`. Oba wymagają `--profile` i mogą zwrócić błąd starego kryterium. Standardowy test obejmuje prawdziwy krok GPU przy wspólnych stanach, local i combined, zachowanie ID/masy, kolejki oraz wykrycie celowo usuniętego sprzężenia.

Domyślne full/direct, opcje C i sondy kosztów OFF. Cel GPU 1,50 ms pozostaje otwarty; następny etap to punktowe PBF G. To paczka diagnostyczna, bez nowego tagu wydania.

Własne kontrole: `QA.txt`. Integralność: `SHA256SUMS.txt`. Raport i dane w projekcie: `docs/I04_5Q_GPU_COUPLING_QA.md`, `docs/gpu-i04-5q/`. Skrypt `tools/analyze-coupling-qa.py` analizuje komplet surowych serii Q.
''')
(package/'QA.txt').write_text(f'''Strefa Zero — diagnostyczne I04.5-Q, 2026-10-30
EXE SHA256: {frozen['exe_sha256']}

Release MSVC /W4 /WX, IPO: PASS. Solver, shadery i assety bez zmian względem E0.
Pełny CTest: 38/38 PASS, kod 0, 302.20 s W ZMIENIONYM PROTOKOLE Q.
Stara reguła odrzuciła identyczne referencje 1/16; pozostała diagnostyką i jawną opcją legacy.
38/38 Q nie oznacza zaliczenia starego kryterium ani usunięcia niedeterministyczności trajektorii.
Wspólne checkpointy: 12/12 powtórek, 1080 stanów, 4320 porównań.
Celowo wyłączone sprzężenie odrzucono 12/12; pozytywne reference/local/combined przeszły stałe budżety.
Progi numeryczne: .05/.02/.15, najniższe dawne minima, dla kroku i skumulowanego defektu.
Niezależny test faktycznego zastosowania sumy reakcji do postaci: błąd <.00001.
Porównanie zwykłego runtime E0/Q: 6/6 procesów, 108 wierszy, zero ostrzeżeń.

Kontrole paczki: coupling kod 0; instrumentation kod 0; GL3.3 oczekiwany kod 77.
Widoczne okno: kod 0, 900 klatek, 1798 ticków, wynik 3, brak utraty czasu; fokus {w['focused']}/{w['frames']}.
Jedna próba nie zalicza sześciu prób odbioru całego I04.5.

Domyślne full/direct, opcje C i sondy OFF. Cel GPU 1,50 ms i długie trajektorie nadal otwarte.
EXE/assety odpowiadają finalnemu buildowi/źródłom. To kompilacja diagnostyczna, bez nowego tagu.
Raport: docs/I04_5Q_GPU_COUPLING_QA.md i docs/gpu-i04-5q/ w projekcie.
''')
print('Q report, plan, review pointers and package documentation written.')
