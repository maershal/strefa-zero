# Z01 — skąd bierze się koszt ticka GPU

Pomiar z 2026-11-10 na GTX 1650 (laptop, i5-9300HF, sterownik 610.47, OpenGL 4.3), narzędzie
`StrefaQA --gpu-check cost-model` (`src/gpu/cost_model.cpp`, `src/gpu/synthetic_passes.cpp`). Procedura:
[GPU_CHECK.md, sekcja 4b](../maintenance/GPU_CHECK.md#4b-model-kosztu-ticka-gpu-z01-opcjonalnie). Surowe dane (CSV,
telemetria, `run-spec.json`, manifest SHA-256):
[z01-cost-model-2026-11-10](https://github.com/maershal/strefa-zero-measurements/tree/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10)
w repozytorium pomiarów (patrz „Dane”).

## Odpowiedź w skrócie

Teza „koszt ticka = liczba przebiegów” **jest prawdziwa tylko jako korelacja, a jej mechanizm jest inny, niż
zakładała hipoteza (b)**. Przy skali gry (do ~4 tys. cząstek) tick jest stały i składa się z przebiegów po 14–30 µs.
Z tego tylko ~1,4 µs na przebieg to koszt samego przebiegu po stronie GPU (pusty dispatch z barierą), a dostarczanie
przez sterownik nie dokłada prawie nic: seria wysłana do bezczynnego GPU trwa na GPU tyle samo co seria wysłana
za długim przebiegiem, który trzyma GPU zajęte (różnica ≤ 0,3 µs na przebieg, w jednej serii 1,4 µs), a zlecenie
przebiegu kosztuje CPU ~0,01 µs. Resztę czasu przebiegu zajmuje jego własna praca, która przy kilku tysiącach wątków jest ograniczona
opóźnieniem zależnych odczytów pamięci, a nie przepustowością. Dlatego zmniejszanie pracy w przebiegu (phase gating,
wąskie odczyty, grupy PBF, restrict) nie pomagało, a jawne API z jednym buforem komend usunęłoby co najwyżej ~2 µs
na granicy przebiegu, czyli ok. 10–15% ticka misji, nie więcej.

Hipoteza (a) jest więc bliższa prawdy, ale w uściślonej postaci: minimalny czas przebiegu wyznacza łańcuch zależnych
odczytów wewnątrz przebiegu (opóźnienie), a nie stały narzut przebiegu. Hipoteza (b) w trybie domyślnym sterownika
(„Threaded optimization” = Auto) nie znajduje potwierdzenia. Wyjątek: gdy coś wymusi synchronizację CPU–GPU, każdy
przebieg kosztuje dodatkowo ~22–41 µs (pełna podróż w obie strony), co przy 41–140 przebiegach daje 0,9–5,7 ms; to
odpowiada 0,7–1,5 ms „gdy coś wymusi synchronizację” z [I09](I09_GPU_VFX.md).

## Warunki pomiaru

| Pozycja | Wartość |
| --- | --- |
| Zasilanie, plan | sieć (bateria 94%), „Zrównoważony” |
| GPU na starcie | 50–51 °C, P8, po 5 min bezczynności |
| Okno, VSync | okno StrefaQA bez prezentacji klatek (narzędzie nie wywołuje `SDL_GL_SwapWindow`), więc VSync nie dotyczy; okno nie było zminimalizowane |
| „Threaded optimization” | domyślne „Auto” (nie zmieniane; narzędzie tego nie odczytuje) |
| Kolejność | 3 bloki, w każdym nowa losowa kolejność serii i punktów sweepu (SplitMix64 + Fisher–Yates, ziarno 4521618711065972830) |
| Powtórzenia | seria syntetyczna 10× w odcinku × 3 bloki; punkt sweepu 120 ticków rozgrzewki + 240 mierzonych × 3 bloki, świeży `ParticleWorld` na punkt |
| Telemetria | 13 678 próbek co 100 ms; brak `hw_*` i `sw_thermal_slowdown`; `sw_power_cap` w 113 z 432 odcinków (raportowane, nie dyskwalifikuje); maks. 70 °C |
| Kwalifikacja | 432/432 odcinków dla czasów GPU, 349/432 także dla czasów CPU (83 odcinki z wolną sondą CPU po odcinku) |
| Czas | 51 min, w tym 41 min czekania na schłodzenie (95 odcinków) |

Pierwsza próba pełnego planu ujawniła zjawisko, którego telemetria GPU nie widzi: po ~40 s ciągłego obciążenia GPU
ten laptop spowalnia CPU do ~5× (sonda CPU: 14,4 ms → 73 ms) bez żadnego powodu ograniczenia w `nvidia-smi`
(wspólne chłodzenie). Dlatego narzędzie ma bramkę schładzania: przed każdym odcinkiem czeka (do 180 s), aż sonda CPU
wróci do 15% najlepszego odczytu, a potem rozgrzewa GPU, żeby odcinek nie startował w P8. Analizator osobno kwalifikuje
czasy GPU (telemetria, zegar w tickach sweepu, sonda GPU) i czasy zależne od CPU (dodatkowo sonda CPU). Pierwsze dwie
próby (bez bramki i bez rozgrzewki GPU) nie wchodzą do wyników; ich dane są w repozytorium pomiarów obok właściwego
przebiegu.

## 1. Ile kosztuje przebieg pusty, trywialny i prawdziwy?

Koszt jednego przebiegu to nachylenie prostej dopasowanej do czasu serii M = 1, 16, 64, 256 przebiegów (każdy
przebieg: dispatch + `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`). „cold”: seria trafia do bezczynnego GPU,
więc rozpiętość `GL_TIMESTAMP` obejmuje też każdą przerwę w dostarczaniu; „primed”: przed serią stoi przebieg
trwający ~13 ms, więc cała seria czeka już w kolejce, a rozpiętość to czysty koszt GPU; „finish_each”: `glFinish` po
każdym przebiegu.

| Przebieg | GPU cold [µs] | GPU primed [µs] | ścienny cold [µs] | ścienny finish_each [µs] | CPU zlecenie [µs] |
| --- | --- | --- | --- | --- | --- |
| pusty + bariera | 1,4 | 1,4 | 1,6 | 23,1 | 0,01 |
| indirect z 0 grup + bariera | 1,3 | 1,0 | 1,5 | 26,1 | 0,01 |
| trywialny: łańcuch T=256, L=16 | 10,8 | 9,4 | 10,9 | 35,9 | 0,01 |
| łańcuch T=256, L=256 | 117,6 | 117,6 | 117,9 | 158,2 | 0,01 |
| łańcuch T=4096, L=16 | 53,8 | 53,8 | 53,9 | 95,0 | 0,01 |
| prawdziwy, ziarna N=1000 (0,576 ms / 41 przebiegów) | 14,0 | — | 15,7 | — | 0,1 |
| prawdziwy, woda N=1000 (1,539 ms / 51 przebiegów) | 30,2 | — | 31,7 | — | 0,08 |
| prawdziwy, misja (2,84 ms / 140, [I04.5E0](I04_5E0_GPU_COSTS.md)) | 20,3 | — | — | — | — |

„Prawdziwy” to czas ticka podzielony przez liczbę dispatchy, czyli średnia, nie pomiar pojedynczego przebiegu.
Wnioski:

- Pusty przebieg z barierą kosztuje GPU 1,4 µs i tyle samo w czasie ściennym: sterownik nie dokłada przerw (cold =
  primed), a CPU zleca go w 0,01 µs. Indirect z zerem grup nie jest tańszy w sposób, który miałby znaczenie (1,0–1,3 µs).
- Pusty narzut to 5–10% przebiegu prawdziwego. Przebieg prawdziwy kosztuje tyle co łańcuch 16–32 zależnych
  odczytów na wątek przy kilkuset wątkach.
- Wymuszona synchronizacja (`finish_each`) dokłada 22–41 µs na przebieg. To jest mechanizm „0,7–1,5 ms”
  z I09: nie dostarczanie przebiegów po jednym, tylko pełna podróż CPU→GPU→CPU, jeśli coś (odczyt, zapytanie bez
  `GL_QUERY_RESULT_NO_WAIT`, mapowanie) ją wymusi.

## 2. Czy koszt serii małych przebiegów idzie za L (opóźnienie), czy za T·L (przepustowość)?

Kernel łańcucha: każdy z T wątków robi L zależnych odczytów SSBO po losowym cyklu 2^20 indeksów (4 MiB, więcej niż
L2 karty TU117), więc każdy odczyt to opóźnienie pamięci. Poprawność sprawdza przejście CPU po tym samym cyklu.

| T | L | T·L | µs / przebieg (primed) | µs na krok łańcucha (÷L) | ns na odczyt (÷T·L) |
| --- | --- | --- | --- | --- | --- |
| 256 | 16 | 4 096 | 9,4 | 0,589 | 2,30 |
| 256 | 256 | 65 536 | 117,6 | 0,459 | 1,79 |
| 4 096 | 16 | 65 536 | 53,8 | 3,363 | 0,82 |
| 4 096 | 256 | 1 048 576 | 808,9 | 3,160 | 0,77 |
| 32 768 | 16 | 524 288 | 409,6 | 25,602 | 0,78 |
| 32 768 | 256 | 8 388 608 | 6 465,4 | 25,255 | 0,77 |

- Przy T = 256 koszt idzie za **L**: 16× dłuższy łańcuch = 12,5× dłuższy przebieg, ~0,5 µs na krok (opóźnienie
  pamięci). Ta sama praca T·L = 65 536 kosztuje 117,6 µs jako T=256, L=256 i 53,8 µs jako T=4096, L=16.
- Od T = 4096 koszt idzie za **T·L**: 0,77–0,82 ns na odczyt bez względu na podział (przepustowość).
- Granica leży między 256 a 4096 wątków na przebieg, czyli tam, gdzie gra pracuje dziś (wątek na cząstkę, N ≈ 1–4 tys.).

Ta sama łączna praca w jednym przebiegu zamiast w M = 64 (GPU, ms):

| T | L | 64 przebiegi | 1 przebieg, łańcuch L·64 | 1 przebieg, T·64 wątków |
| --- | --- | --- | --- | --- |
| 256 | 16 | 0,602 | 0,465 (−23%) | 0,209 (−65%) |
| 256 | 256 | 7,543 | 7,392 (−2%) | 3,227 (−57%) |
| 4 096 | 16 | 3,437 | 3,219 (−6%) | 3,261 (−5%) |
| 32 768 | 16 | 26,230 | 25,815 (−2%) | 26,065 (−1%) |
| 32 768 | 256 | 413,719 | 412,623 (−0,3%) | 412,672 (−0,3%) |

Połączenie przebiegów przy zachowanej zależności (łańcuch L·64) oszczędza ~2,1 µs na granicy przebiegu i nic więcej.
Duży zysk (−57 do −65%) daje tylko równoległość: ta sama praca rozłożona na więcej niezależnych wątków. Przy
przepustowości (T ≥ 4096) łączenie nie daje prawie nic.

## 3. Tryb synchroniczny sterownika i próg przepustowości

**Synchronizacja.** W trybie domyślnym (Auto) koszt przebiegu cold minus primed wynosi od −0,7 do +0,3 µs w 7 z 8
rodzajów serii; wyjątek to łańcuch T=256, L=16: +1,4 µs (15%), i to tylko przy M = 256 (przy M ≤ 64 serie są równe,
0,602 wobec 0,599 ms), więc to raczej zegar GPU niż dostarczanie. Czas ścienny serii cold różni się od GPU o 0,1–0,2 µs
na przebieg: GPU nie czeka na przebiegi.
Wymuszony `glFinish` po przebiegu kosztuje 22–41 µs. Zlecenie ticka sweepu kosztuje CPU 0,004–0,007 ms (211 wywołań GL
ziaren, 253 wody), tyle samo co 0,008 ms w I04.5E0.

**Naturalny eksperyment.** W 7 punktach sweepu ten sam punkt był mierzony raz przy szybkim, raz przy spowolnionym CPU
(sonda CPU ~1,7× wolniejsza po odcinku). Stosunek czasu GPU ticka: mediana 0,99 (0,89–1,00). Wolniejszy CPU, a więc
wolniejszy sterownik, nie wydłuża ticka GPU.

**Powtórka z wyłączoną „Threaded optimization”** (do wykonania przez właściciela, #81, [GPU_CHECK.md 4b](../maintenance/GPU_CHECK.md#4b-model-kosztu-ticka-gpu-z01-opcjonalnie),
`-Plan part1 -ThreadedOptimization off`). Jak odczytać `summary.md` tej serii względem tabeli z punktu 1:

- „CPU zlecenie” wzrośnie (sterownik pracuje w wątku aplikacji). Samo to nie rozstrzyga niczego.
- Rozstrzyga różnica **GPU cold − GPU primed** na przebieg w `analysis.json` (`series_fits`, pole `ms_per_pass`).
  Przy Auto wynosi ona 0,05 µs (pusty), 0,3 µs (indirect) i 1,4 µs (łańcuch T=256, L=16). Jeśli przy Off wzrośnie
  o kilka µs na przebieg (rząd kosztu pustego przebiegu lub więcej), to w trybie synchronicznym GPU czeka na
  dostarczenie i hipoteza (b) działa w tym trybie; wtedy wynik gry zależy od ustawienia sterownika, a jawne API (jeden
  bufor komend) usunęłoby tę różnicę.
- Jeśli cold ≈ primed także przy Off, sterownik nie jest wąskim gardłem w żadnym trybie i (b) odpada całkiem.
- Czas ścienny cold na przebieg porównaj z 1,6 µs (pusty) i 10,9 µs (łańcuch): wzrost przy cold ≈ primed oznacza
  koszt CPU, nie GPU.

**Próg przepustowości.** Ten sam zbiornik (300 kolumn między ścianami, cząstka na komórkę od dna; rośnie tylko
wysokość), tylko ziarna albo tylko woda, mediana 3 bloków:

| N | ziarna GPU [ms] | ziarna ścienny [ms] | woda GPU [ms] | woda ścienny [ms] |
| --- | --- | --- | --- | --- |
| 1 000 | 0,576 | 0,645 | 1,539 | 1,618 |
| 2 000 | 0,664 | 0,762 | 1,625 | 1,734 |
| 4 000 | 0,707 | 0,830 | 1,774 | 1,900 |
| 8 000 | 1,004 | 1,078 | 2,487 | 2,630 |
| 16 000 | 1,949 | 2,041 | 4,971 | 5,065* |
| 32 000 | 4,164 | 4,302 | 9,344 | 9,443* |

Ziarna: 41 dispatchy i 42 bariery na tick, woda: 51 i 52, bez zmiany z N. * Wszystkie 3 bloki wody 16k i 32k miały
wolną sondę CPU po odcinku; podany czas ścienny to mediana mimo to (czasy GPU są zakwalifikowane).

Od 1 do 4 tys. cząstek tick rośnie o 23% (ziarna) i 15% (woda), czyli jest stały. Od 8 tys. rośnie liniowo
(16k → 32k: ×2,14 ziarna, ×1,88 woda). Prosta dopasowana do 8k–32k przecina koszt przy 1k (0,58 / 1,54 ms) przy
**ok. 5,1 tys. ziaren i 4,4 tys. cząstek wody** (`crossover_n` w `sweep-fit.csv` i w `analyze-cost-model.py`: 5127
i 4362): powyżej tick jest ograniczony przepustowością. Ciecz powyżej
7662 cząstek jest teraz zmierzona do 32 000 (limit `capacity` = 32 768).

## 4. Ekstrapolacja na 64k i 128k

**To jest ekstrapolacja poza zakres pomiaru** (świat ma dziś pojemność 32 768 cząstek; większa pojemność zmieni też
siatkę sąsiadów i może zmienić koszt).

| Materiał | Dopasowanie | a [ms] | b [ms / 1000] | R² | 64k [ms] | 128k [ms] |
| --- | --- | --- | --- | --- | --- | --- |
| ziarna | a + b·N, 1k–32k (6 punktów) | 0,286 | 0,1166 | 0,983 | 7,9 | 15,6 |
| ziarna | a + b·N, 8k–32k (3 punkty) | −0,104 | 0,1326 | 0,999 | 8,6 | 17,3 |
| woda | a + b·N, 1k–32k (6 punktów) | 0,906 | 0,2588 | 0,990 | 17,9 | 34,8 |
| woda | a + b·N, 8k–32k (3 punkty) | 0,300 | 0,2839 | 0,999 | 18,9 | 37,5 |

Prosta po całym zakresie źle opisuje płaski początek (dla wody przewiduje 1,17 ms przy 1k zamiast 1,54), więc do
ekstrapolacji właściwsze jest nachylenie z 8k–32k. Wzrost 16k → 32k jest lekko ponadliniowy dla ziaren (×2,14), więc
liczby dla 64k i 128k są raczej dolnym oszacowaniem. Przy budżecie ticka 8,33 ms (120 Hz) 64k ziaren mieści się na
granicy, a 64k wody przekracza budżet ponad 2×.

## Co z tego wynika

- Licznik przebiegów pozostaje dobrą miarą kosztu przy skali gry, bo każdy przebieg niesie własny łańcuch opóźnień,
  ale dźwignią nie jest sama liczba dispatchy. Zmniejszenie liczby przebiegów przez połączenie zależnych kroków w jeden
  shader oszczędza ok. 2 µs na granicy (dla 41 przebiegów ok. 0,09 ms z 0,58 ms). Dźwignią jest skrócenie łańcucha
  zależnych odczytów w przebiegu albo rozłożenie pracy na więcej niezależnych wątków.
- Jawne API z jednym buforem komend nie jest największą dźwignią w trybie domyślnym sterownika: nie ma przerw do
  usunięcia. Ocenę trzeba powtórzyć po serii z „Threaded optimization” = Off.
- Każda wymuszona synchronizacja w ticku kosztuje ~22–40 µs na każdy następny przebieg ticka; to zostaje głównym
  ryzykiem dla ścieżki gorącej.
- Pomiary na tym laptopie muszą sprawdzać stan CPU, nie tylko GPU: dławienie CPU ~5× nie ma śladu w `nvidia-smi`.
  `CalibrationProbe` (`src/gpu/synthetic_passes.hpp`, biblioteka `strefa_diagnostics`) jest do tego gotowa dla
  benchmarku termiki; benchmark scen jest dziś w `strefa_app` i dostanie ją po przeniesieniu QA poza `StrefaZero`
  (#39, #80).

## Ograniczenia

- Jeden komputer, jeden sterownik, jedna konfiguracja „Threaded optimization”. Liczby z llvmpipe (CTest
  `gpu.cost_model`, plan `quick`) niczego nie rozstrzygają.
- Cykl łańcucha (4 MiB, losowy) mierzy najgorszy przypadek opóźnienia (DRAM i TLB). Prawdziwe kernele trafiają częściej
  w L2, więc ich krok łańcucha jest krótszy niż 0,46–0,59 µs; wniosek o reżimie (opóźnienie przy setkach wątków,
  przepustowość od kilku tysięcy) od tego nie zależy, sama granica może się przesunąć.
- „Prawdziwy przebieg” to średnia ticka na dispatch, nie czas pojedynczego przebiegu; rozkład na przebiegi daje
  `StrefaQA --gpu-cost-profile` (I04.5E0).
- Sweep co 16 ticków wywołuje `glFinish`, bo bez tempa gry 32 timery ticka nie nadążają; czas ścienny zawiera te
  synchronizacje (różnica ścienny − GPU: 0,07–0,13 ms na tick).
- Bramka schładzania zmienia przebieg pomiaru (GPU stygnie między odcinkami), ale nie mierzony odcinek: przed każdym
  GPU jest rozgrzewane, a sonda GPU wszystkich 432 odcinków mieściła się w 2% najszybszej (3,42–3,49 ms).

## Dane

Folder [z01-cost-model-2026-11-10](https://github.com/maershal/strefa-zero-measurements/tree/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10)
(manifest [SHA256SUMS.txt](https://github.com/maershal/strefa-zero-measurements/blob/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10/SHA256SUMS.txt)):

- `run-3/` — wynik raportu: [summary.md](https://github.com/maershal/strefa-zero-measurements/blob/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10/run-3/summary.md),
  [analysis.json](https://github.com/maershal/strefa-zero-measurements/blob/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10/run-3/analysis.json),
  [series.csv](https://github.com/maershal/strefa-zero-measurements/blob/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10/run-3/series.csv),
  [sweep.csv](https://github.com/maershal/strefa-zero-measurements/blob/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10/run-3/sweep.csv),
  [segments.csv](https://github.com/maershal/strefa-zero-measurements/blob/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10/run-3/segments.csv),
  [telemetry.csv](https://github.com/maershal/strefa-zero-measurements/blob/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10/run-3/telemetry.csv),
  [run-spec.json](https://github.com/maershal/strefa-zero-measurements/blob/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10/run-3/run-spec.json).
- `sweep-fit.csv` w danych pochodzi z narzędzia sprzed poprawki po recenzji (kolumna `knee_n` = a/b, nie próg);
  progi i dopasowania 8k–32k w raporcie są z `summary.md`, przeliczonego obecnym `analyze-cost-model.py`.
- `run-1/`, `run-2/` — próby, które ujawniły dławienie CPU ([sondy run-1](https://github.com/maershal/strefa-zero-measurements/blob/1e0c8741339e3206a23219d089be95b176708efe/z01-cost-model-2026-11-10/run-1/segments.csv)); bez wpływu na wyniki.
