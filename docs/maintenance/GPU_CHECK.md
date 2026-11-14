# Kontrola GPU na Windows: benchmark scen i soak

Procedura do wykonania ręcznie na laptopie z kartą NVIDIA (GTX 1650). Daje dwa wyniki: koszt każdej sceny w oknie
(podstawa decyzji o treści i optymalizacjach) oraz odpowiedź, czy i kiedy pojawia się wolny reżim z
[G3-B](../reports/I04_5G3B_GPU_REPLAY.md) (fizyka GPU ×2,7–4,1) i które powody ograniczenia zegara są wtedy aktywne.
Zasady pomiaru: sekcja „Protokół pomiaru” w `CLAUDE.md`.

## 1. Przygotowanie

1. Zasilacz podłączony, plan zasilania zrównoważony albo wysoka wydajność (raport go zapisze).
2. Zamknij przeglądarki, komunikatory i gry; wyłącz nagrywanie ekranu i nakładki (GeForce Experience, Discord).
3. Zbuduj wersję Release w PowerShell 7.2 lub nowszym z katalogu repozytorium:

   ```powershell
   pwsh ./scripts/build.ps1 -Configuration Release -SkipTests
   ```

4. Zostaw laptop bezczynny przez 5 minut, żeby GPU ostygło.
5. Benchmark otwiera widoczne okno. Nie minimalizuj go, nie zasłaniaj innym oknem, nie przełączaj pulpitu
   wirtualnego i nie używaj komputera do końca pomiaru. Scena, w której okno choć przez jedną klatkę było ukryte,
   zminimalizowane, zasłonięte albo ukryte przez DWM, dostaje status `invalid`. Klatki z zminimalizowanym oknem
   (symulacja wtedy stoi) liczą się do okna pomiaru sceny, więc minimalizacja nie skraca ani nie omija pomiaru.
   Rozgrzewka nie ma wierszy klatek, ale okno poza ekranem w jej trakcie liczy kolumna `warmup_away_frames`
   i ono też unieważnia scenę.
   Na Windows SDL 3.4 nie zgłasza zasłonięcia (`SDL_EVENT_WINDOW_OCCLUDED`), więc benchmark sprawdza tam co klatkę
   także `IsIconic` (liczone jako `minimized`) i `DwmGetWindowAttribute(DWMWA_CLOAKED)` (kolumna `cloaked`). Okna
   zasłoniętego innym oknem na Windows nie da się w ten sposób wykryć: licznik `unfocused_frames` (wypisywany też
   w wierszu podsumowania jako „bez fokusu”) pokazuje, że ktoś używał komputera, ale nie unieważnia pomiaru.
6. Zamknięcie okna (albo Ctrl+C w konsoli, czyli `SDL_EVENT_QUIT`) przerywa cały benchmark, a nie tylko bieżącą
   scenę: przerwana scena dostaje status `failed` z powodem „przerwany zamknięciem okna (QUIT)”, kolejne sceny nie
   startują, a kod wyjścia to 1.

## 2. Benchmark wszystkich scen (z VSync i bez)

Każde polecenie trwa ok. 32 × 70 s ≈ 37 min (60 s pomiaru + 10 s rozgrzewki na scenę). Telemetria zapisuje się
obok wyników:

```powershell
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false) # polskie znaki w summary.txt
Import-Module ./scripts/profiling/GpuTelemetry.psm1
foreach ($vsync in 'on', 'off') {
    $out = "gpu-check/benchmark-vsync-$vsync"
    New-Item -ItemType Directory $out -Force | Out-Null
    $telemetry = Start-GpuTelemetry -Path "$out/telemetry.csv"
    ./build/release/StrefaQA.exe --scene-benchmark all --seconds 60 --vsync $vsync --profile $out |
        Tee-Object "$out/summary.txt"
    Stop-GpuTelemetry $telemetry
    Start-Sleep -Seconds 300 # ostygnięcie przed drugim przebiegiem
}
```

- Kod wyjścia 0: wszystkie sceny mają status `ok`.
- Kod 1: co najmniej jedna scena ma status `invalid` (okno niewidoczne) albo `failed` (błąd trybu). Pozostałe sceny są
  zmierzone, a powód jest w kolumnie `status_reason`.
- Kod 77: brak OpenGL 4.3 (w profilu jest wtedy `gpu-skipped.txt`, a CTest `gpu.scene_benchmark_content` też
  jest pomijany).

`scene-benchmark-info.txt` jest zapisywany na dysk przed pierwszą sceną, więc zostaje także po awarii albo zabiciu
procesu. Na końcu dochodzą do niego `result=completed|quit` i `scenes_measured=<liczba>`, a przy kodzie wyjścia sceny innym
niż 0 (np. 77) `result=exit_code_<kod>`; brak `result=` oznacza przerwany proces. Plik ma `schema=3` (nowe pola
okna i `result=`). Pola `window_iconic` i `window_cloaked` opisują stan okna na starcie.

`--vsync on` pokazuje, jak gra działa u gracza; `--vsync off` (bez ogranicznika klatek) mierzy koszt. Rozmiar okna
można ustalić opcją `--test-window-size 1600 900`; `scene-benchmark-info.txt` zapisuje faktyczny rozmiar, skalę DPI
i jakości z ustawień domyślnych.

Misja odtwarza zapisaną trasę (1340 ticków ≈ 11 s). W dłuższym oknie trasa zaczyna się od nowa. `route_reset=1`
ma klatka po tej, która zażądała restartu (jej `frame_ms` zawiera przeładowanie świata), i nie wchodzi do statystyk
czasu klatki; liczba restartów jest w kolumnie `route_resets`.

## 3. Soak 15 minut

```powershell
pwsh ./scripts/profiling/soak.ps1 -Executable build/release/StrefaQA.exe -Output gpu-check/soak -SingleProcess `
    -Baseline gpu-check/benchmark-vsync-off/scene-benchmark.csv
```

Skrypt powtarza przebiegi po 17 scenach GPU (12 scen laboratorium, misja, 4 sceny termiki, po 10 s pomiaru,
VSync off) przez 15 minut i przez cały czas zbiera telemetrię `nvidia-smi` co 500 ms.

- `-SingleProcess` uruchamia wszystkie przebiegi w jednym procesie StrefaQA (`--loop-minutes`). Bez tej opcji każdy
  przebieg to nowy proces, a restart procesu i przerwa między przebiegami mogą przerwać wolny reżim.
- `-Baseline` to `scene-benchmark.csv` z przebiegu na ostygniętym GPU z tymi samymi ustawieniami. Ten sam `--vsync`
  daje porównywalne mediany fizyki.

Pomiar sceny jest oznaczany według trzech niezależnych kryteriów:

| Kryterium | Warunek |
| --- | --- |
| względne | mediana fizyki GPU ≥ 2× najlepszy przebieg tej sceny w soaku (`-SlowFactor`) |
| bezwzględne | mediana fizyki GPU ≥ 2× mediana tej sceny w `-Baseline` |
| telemetryczne | większość próbek poza P0 albo mediana zegara pamięci poniżej 90% maksimum sesji |

Samo kryterium względne nie wykryje reżimu, który trwa od pierwszego przebiegu. Kryterium telemetryczne oznacza też
lekkie sceny, w których sterownik sam obniża P-stan. Wyniki: `soak-report.md`, `soak-summary.csv`, `telemetry.csv`,
`environment.json` i katalog `run/` (albo `pass-NN/` bez `-SingleProcess`).

## 4. Benchmark termiki (opcjonalnie)

Macierz ABBA etapów termiki (`off`, `heat`, `heat_gas`, `full`) w 4 scenach (`cold`, `fire`, `oil`, `quench`), 3 bloki.
Bloki są pętlą zewnętrzną, a każdy blok losuje własną kolejność scen (Fisher–Yates na SplitMix64, ta sama kolejność
na każdym kompilatorze). Wcześniej kolejność była stała (`cold → fire → oil → quench`), więc ostatnia scena mierzyła
nagrzany laptop: etap gazu, który nie zależy od sceny, trwał w „gaszeniu” 2,36× dłużej. Pomiar trwa ok. 10 min na szybkim GPU, w wolnym reżimie dłużej:

```powershell
pwsh ./scripts/profiling/profile-thermal.ps1 -Executable build/release/StrefaQA.exe -Output gpu-check/thermal `
    -Suite benchmark -Telemetry
python scripts/profiling/analyze-thermal.py gpu-check/thermal --controls
```

- `-Telemetry` zapisuje obok wyników `telemetry.csv` z `GpuTelemetry.psm1` co 100 ms: P-stan, zegar grafiki
  (`clocks.current.graphics`, kolumna `graphics_mhz`) i pamięci (`clocks.current.memory`), moc i jej limit,
  temperaturę oraz aktywne powody ograniczenia (`clocks_event_reasons.*` albo `clocks_throttle_reasons.*`, zależnie
  od sterownika). `-TimeoutMinutes` (domyślnie 90) przerywa zawieszony pomiar.
- `-Seed N` (albo zmienna `STREFA_BENCHMARK_SEED`) powtarza kolejność scen wcześniejszego pomiaru; bez niej każdy
  pomiar losuje nowe ziarno. Ziarno i kolejność są w `thermal-benchmark-info.txt` (`seed=`, `block_<n>_scenes=`).

Pliki wyników (wszystkie mają kolumny `seed` i `sequence`, czyli numer odcinka w kolejności wykonania):

- `thermal-samples.csv`: wiersz na tick odcinka ABBA (bez zmian poza `seed`, `sequence`);
- `thermal-pool-segments.csv`: stan sum termiki po odcinku oraz `start_utc_unix_ns`, `end_utc_unix_ns` (łączenie
  z `telemetry.csv`);
- `thermal-pass-samples.csv`: odcinek profilu po każdej scenie bloku (120 ticków, `ThermalOptions::profile`), kolumna
  `block` oraz `dispatches` i `barriers` (liczone od końca poprzedniego ticku termiki: zdarzenia, kernele solvera,
  kondensacja poprzedniego kroku, gumka i odczyt sum; bez prezentacji, rysowania, inspektora, resetu sceny
  i wymuszonego odczytu sum przez `read_thermal`). Każdy tick ma dwa
  wiersze: wiersz CPU (`cpu_ms`, liczniki) i wiersz GPU (`heat_ms`, `gas_ms`, `reaction_ms`);
- `thermal-op-samples.csv`: znacznik czasu po każdym kernelu ticku w trybie profilu: `index` (kolejność w ticku),
  `op` (numer programu termiki) i `gpu_ms` (różnica z poprzednim znacznikiem);
- `thermal-profile-segments.csv`: granice UTC odcinków profilu.

Benchmarki termiki, oświetlenia i efektów czytają dane diagnostyczne (sumy termiki, bufory świata, liczniki puli
efektów) przez świeży bufor `GL_STREAM_READ` (`gpu/diagnostic_read.hpp`), nigdy wprost z żywego SSBO, żeby sterownik
nie przenosił buforów symulacji do pamięci hosta między odcinkami. Benchmark termiki czyta same sumy
(`read_thermal(ThermalRead::totals)`). Kolumny `id131186_runtime` i `id131186_readback` w
`thermal-pool-segments.csv`, `thermal-profile-segments.csv`, `lighting-sources.csv` i `vfx-debug-segments.csv` liczą
komunikaty sterownika 131186 (NVIDIA: migracja bufora) w tickach i klatkach odcinka (faza `runtime` w
`gl-debug.txt`) oraz w przygotowaniu i odczytach wokół niego (faza `validation`). Są puste bez `--gl-debug`.
Oczekiwana wartość `id131186_runtime` to 0. Podział na fazy jest przybliżony: dziennik używa asynchronicznego wyjścia
debugowania, więc sterownik może dostarczyć komunikat już po przełączeniu fazy. Przed każdym przełączeniem benchmark
czeka na GPU (`glFinish`), co czyni to mało prawdopodobnym, ale specyfikacja OpenGL tego nie gwarantuje. Suma obu
kolumn jest dokładna. Bez `--gl-debug` benchmark nie dodaje oczekiwania przed mierzonymi odcinkami.

`analyze-thermal.py` (schemat 2) traktuje etap gazu jako sondę zegara: odcinek profilu, którego mediana `gas_ms`
odbiega od mediany wszystkich odcinków o więcej niż 10% (`--gas-tolerance`), jest dyskwalifikowany razem z próbami
tej sceny w tym bloku. Z `telemetry.csv` dyskwalifikuje też próbę albo odcinek profilu, w którym był aktywny
`hw_slowdown`, `hw_thermal_slowdown`, `hw_power_brake_slowdown` albo `sw_thermal_slowdown`, albo którego mediana zegara
grafiki jest poniżej 90% maksimum sesji (`--clock-fraction`). Ten drugi warunek łapie wolny reżim GTX 1650 w laptopie:
po dojściu do 85 °C karta schodzi do 1395 MHz i połowy limitu mocy (ok. 23 W z 50 W) przy P0 i bez żadnego powodu
ograniczenia w `nvidia-smi`, i zostaje tam także po ostygnięciu do 64 °C; etap gazu trwa wtedy 2,35× dłużej (3,12 zamiast
1,34 ms; jeden przebieg informacyjny bez stygnięcia przed startem, surowe dane: [thermal-benchmark-2026-11-04](https://github.com/maershal/strefa-zero-measurements/tree/0cc0d361c32f7f437327c32127598fdd0d4a55e0/thermal-benchmark-2026-11-04)). Z telemetrią medianą odniesienia etapu gazu są tylko odcinki, które przeszły warunki telemetrii, więc pomiar
spędzony głównie w wolnym reżimie nie robi z niego odniesienia (`gas_reference` w `thermal-summary.json`). `thermal-summary.json` podaje
`disqualified_segments`, `qualified_full_gates` i decyzję `performance_accepted` tylko z prób zakwalifikowanych,
a także `kernel_profiles` (rozkład czasu każdego kernela na scenę) i liczniki dispatchy i barier w `stage_profiles`.

## 4b. Model kosztu ticka GPU (Z01, opcjonalnie)

`StrefaQA --gpu-check cost-model` rozstrzyga, czy koszt ticka siedzi w GPU (minimalny czas krótkiego przebiegu), czy
w dostarczaniu przebiegów przez sterownik ([raport Z01](../reports/Z01_GPU_COST_MODEL.md)). Pełny plan trwa ok. 1 h
na laptopie z GTX 1650, w tym ok. 45 min czekania na schłodzenie: przed każdym odcinkiem narzędzie czeka (do 180 s),
aż sonda CPU wróci do 15% najlepszego odczytu, bo pod ciągłym obciążeniem GPU ten laptop po ok. 40 s zwalnia CPU
nawet 5× bez żadnego powodu ograniczenia w telemetrii GPU:

```powershell
pwsh ./scripts/profiling/profile-cost-model.ps1 -Executable build/release/StrefaQA.exe -Output gpu-check/cost-model `
    -ThreadedOptimization auto
```

- Skrypt działa tylko na Windows (`Win32_Battery`, `powercfg`). Na Linuksie uruchom
  `StrefaQA --gpu-check cost-model --profile <katalog>` bezpośrednio i zbierz telemetrię osobno.
- Skrypt zapisuje `telemetry.csv` (co 100 ms), `run-spec.json` (zasilanie, plan zasilania, temperatura GPU na starcie)
  i uruchamia `analyze-cost-model.py`. Czasy GPU biorą odcinki bez `hw_*`/`sw_thermal_slowdown`, z zegarem
  w tickach sweepu co najmniej 90% maksimum sesji i z sondą GPU w granicach 15% najszybszej; czasy zależne od CPU
  (zlecenie, czas ścienny) wymagają jeszcze sondy CPU w granicach 15% najszybszej. Wynik: `summary.md` (tabele do
  raportu) i `analysis.json`.
- Powtórka z wyłączoną opcją sterownika: w Panelu sterowania NVIDIA › Zarządzaj ustawieniami 3D › Ustawienia programu
  dodaj `StrefaQA.exe` i ustaw „Optymalizacja wątkowa” (Threaded optimization) na „Wył.”, potem
  `-Plan part1 -ThreadedOptimization off -Output gpu-check/cost-model-off`. Po pomiarze przywróć „Automatycznie”.
  Narzędzie nie odczytuje tej opcji; zapisuje tylko to, co podano w `-ThreadedOptimization`.
- `-Seed N` powtarza kolejność wcześniejszego pomiaru. `STREFA_COST_MODEL_PLAN=quick` (CTest `gpu.cost_model`) to te
  same ścieżki kodu w małych rozmiarach, na dowolnym rendererze; jego liczby niczego nie rozstrzygają.

Pliki: `series.csv` (każde powtórzenie serii syntetycznej: czas GPU z pary `GL_TIMESTAMP`, czas CPU zlecenia, czas
ścienny), `segments.csv` (odcinek = jedna seria albo jeden punkt sweepu, z sondami przed i po oraz czasem UTC do
złączenia z telemetrią), `sweep.csv` i `sweep-ticks.csv` (punkt sweepu i jego ticki), `series-summary.csv`,
`series-fit.csv`, `sweep-fit.csv` (podsumowania bez kwalifikacji telemetrią), `cost-model.txt` (renderer, ziarno,
plan). `sweep-fit.csv` i `analyze-cost-model.py` liczą to samo: dopasowanie a + b·N po wszystkich N (`all`) i po
N ≥ N_max/4 (`upper`, zakres liniowy) oraz próg `crossover_n` = (koszt przy najmniejszym N − a) / b, czyli N, od
którego tick jest ograniczony przepustowością.

## 4c. Skróty bitowe termiki (optymalizacje bitowo neutralne)

`StrefaQA --gpu-check thermal-bits --profile <katalog>` uruchamia ustaloną liczbę ticków (domyślnie 600,
`STREFA_THERMAL_BITS_TICKS`, wielokrotność 60) w scenach `cold` (sand_water z termiką), `fire`, `oil`, `quench`,
`phases`, `control` (sand_water bez termiki) i `idle` (pusty świat z termiką) i zapisuje co 60 ticków
(`STREFA_THERMAL_BITS_EVERY`, dzielnik 60) skróty FNV-1a komórek i cząstek termiki oraz cząstek świata (bieżący bank,
wszystkie sloty) i terenu, czytanych przez bufor pośredni. Na końcu: skróty per pole rekordu i próba punktu stałego
(ile słów zmienia jeden kolejny tick, także per pole). W scenie `cold` po próbie idzie jeszcze pędzel grzejący
(10 ticków) i 120 ticków bez wejścia.

Każda scena liczy się dwa razy w jednym procesie. Sceny, które się powtarzają, trafiają do `thermal-bits.txt` i ten
plik porównuje się między dwoma buildami na tej samej maszynie i tym samym sterowniku:

```powershell
./build-a/StrefaQA.exe --gpu-check thermal-bits --profile gpu-check/bits-a
./build-b/StrefaQA.exe --gpu-check thermal-bits --profile gpu-check/bits-b
Compare-Object (Get-Content gpu-check/bits-a/thermal-bits.txt) (Get-Content gpu-check/bits-b/thermal-bits.txt)
```

Pusty wynik `Compare-Object` oznacza bitowo ten sam stan. Skróty nie są wzorcem w repozytorium. Sceny, które się nie
powtarzają, trafiają do `thermal-bits-unstable.txt`, a `thermal-bits-info.txt` podaje dla każdej sceny
`repeat <scena>=yes|no` z pierwszym różnym wierszem. Ich skróty niczego nie dowodzą między buildami.

Stan na `main` (GTX 1650, sterownik 610.47): powtarzają się tylko `fire` i `idle`, czyli sceny bez cząstek. Sceny
z cząstkami rozjeżdżają się po 5–13 tickach, również `control` bez termiki: solver cząstek dopisuje do listy aktywnych
i do indeksu sąsiadów atomowo (`granular.comp`, `neighbors.comp`), więc kolejność w komórce, a z nią kolejność sum
float, zmienia się między przebiegami. Pierwsze różnią się cząstki świata, termika dopiero za nimi. Test
`gpu.thermal_bits` (120 ticków) wymaga powtórzenia `fire` i `idle` oraz tego, że w `idle` kolejny tick nie zmienia
ani jednego słowa. Drugi przebieg każdej sceny to przebieg audytu bramki gazu (`ThermalOptions::gate_audit`: etap
liczy się zawsze), więc powtórzenie `fire` i `idle` dowodzi też, że przebieg z bramką jest bitowo równy przebiegowi bez
pomijania. Wiersze `gate <scena>` w `thermal-bits-info.txt` podają liczbę pominiętych etapów gazu i naruszeń audytu,
które muszą wynosić 0, a pusty świat musi mieć pominięte etapy.

## 4d. Bramka etapu gazu termiki (ABBA w jednym procesie)

`StrefaQA --gpu-check thermal-gate` mierzy dokładną bramkę etapu gazu (`ThermalOptions::gas_gate`,
`assets/shaders/thermal_gate.comp`). Każdy blok losuje kolejność scen `cold`, `idle`, `fire`, `quench`. Każda scena to
kwadrat ABBA: bramka wyłączona, włączona, włączona, wyłączona. Każdy odcinek startuje ze świeżego świata: 120 ticków
rozgrzewki i 240 mierzonych (para `GL_TIMESTAMP` wokół `step()`, czyli cały tick fizyki z termiką). Przed każdym
odcinkiem i po nim działają sondy kalibracyjne Z01 z tą samą bramką stygnięcia co w modelu kosztu (sekcja 4b):

```powershell
pwsh ./scripts/profiling/profile-thermal-gate.ps1 -Executable build/release/StrefaQA.exe -Output gpu-check/thermal-gate
```

Skrypt zapisuje `telemetry.csv`, `run-spec.json` i uruchamia `analyze-thermal-gate.py`. Odcinek się kwalifikuje, gdy
nie ma w nim `hw_*` ani `sw_thermal_slowdown`, mediana zegara grafiki wynosi co najmniej 90% maksimum sesji, a sondy
GPU przed nim i po nim mieszczą się w 15% najszybszej. Kwadrat liczy się tylko wtedy, gdy kwalifikują się wszystkie
cztery odcinki. Jego efekt to średnia (włączona) minus średnia (wyłączona) median odcinków, więc liniowy dryf
w kwadracie się znosi. Wynik: `summary.md` i `analysis.json`. `STREFA_THERMAL_GATE_PLAN=quick` (CTest
`gpu.thermal_gate`) przechodzi te same ścieżki kodu w 1 bloku po 24 ticki, tylko odcinki A i B (mniej światów dla
renderera programowego w nocnym CI); jego liczby niczego nie rozstrzygają.
Dowód bitowy bramki daje `thermal-bits` (sekcja 4c): skróty `fire` i `idle` identyczne z `main`, a przebieg audytu
(`ThermalOptions::gate_audit`) w każdej scenie liczy etapy, które bramka by pominęła, choć zmieniłyby choć jedno
słowo. Ta liczba musi wynosić 0.

## 5. Co odesłać

Wklej do rozmowy albo do opisu PR:

1. Zawartość `gpu-check/benchmark-vsync-on/summary.txt` i `gpu-check/benchmark-vsync-off/summary.txt`
   (po jednym wierszu na scenę).
2. Cały `gpu-check/soak/soak-report.md`: wynik trzech kryteriów, tabelę powodów ograniczenia i tabelę przebiegów
   (≤ 10 wierszy).
3. Z `gpu-check/soak/environment.json`: pola `gpu`, `power_source`, `power_scheme` i `telemetry_reasons_source`.
4. Jeśli był benchmark termiki: wynik `analyze-thermal.py` (ziarno, `performance_accepted`, odcinki zdyskwalifikowane).
5. Czy w trakcie coś się działo (aktualizacja Windows, uśpienie ekranu, odłączony zasilacz, inne okno na wierzchu).

Surowe pliki (`scene-benchmark*.csv`, `telemetry.csv`, `run/`, `pass-*/`) trafiają do repozytorium
[strefa-zero-measurements](https://github.com/maershal/strefa-zero-measurements) w nowym katalogu z własnym
`SHA256SUMS.txt`, a nie do tego repozytorium.

## 6. Tracy, RenderDoc, Nsight na GTX 1650

Narzędzia do szukania przyczyny, np. wolnego reżimu: pokazują oś czasu albo pojedynczą klatkę, ale nie zastępują
benchmarku z sekcji 2–4. Liczby do raportu nadal pochodzą z benchmarku, zgodnie z protokołem pomiaru. Nie uruchamiaj
benchmarku ani soak z podłączonym profilerem: Tracy dokłada 2 kwerendy znacznika czasu na każdy przebieg (ok. 400 na
klatkę), a RenderDoc i Nsight przechwytują wywołania OpenGL. Decyzja i zakres: [ADR 0002](../adr/0002-narzedzia-profilowania.md).

### Tracy (oś czasu CPU i GPU)

1. Pobierz profiler Tracy **v0.14.1** (`Tracy-0.14.1.7z` z wydań `wolfpld/tracy` na GitHubie); klient i serwer
   muszą mieć tę samą wersję protokołu.
2. Zbuduj StrefaQA z Tracy (preset `dev`: RelWithDebInfo, `STREFA_TRACY=ON`, tylko cel `strefa_qa` i jego
   zależności) w terminalu deweloperskim Visual Studio (`Developer PowerShell for VS`):

   ```powershell
   cmake --preset dev
   cmake --build --preset dev
   ```

   Linux/WSL2: `cmake --preset linux-dev && cmake --build --preset linux-dev`. Paczka z tym buildem się nie zbuduje
   (`cmake --install` kończy się błędem), bo klient Tracy otwiera port TCP.
3. Uruchom scenę interaktywnie, np. `build\dev\StrefaQA.exe --gpu-laboratory --scene gpu:<id>
   --gpu-thermal on`, albo misję (`--mission`). Uruchom `tracy-profiler.exe` i połącz się z `localhost` (klient
   nasłuchuje tylko lokalnie; tryb na żądanie, więc dane płyną dopiero po połączeniu).
4. Co oglądać:
   - wiersz `OpenGL`: strefa `physics tick` z przebiegami `granular <op>` (PBD), `liquids <op>` (PBF),
     `neighbors <op>`, `mission <op>`, `thermal tick` z kernelami `thermal <op>`, `lighting tick`, `vfx update`,
     `render`;
   - wątek główny: `Application frame` → `SimulationWorld::advance` → `ParticleWorld::step` (`prepare mission`,
     `solver substep`) i `ThermalSystem::tick`; ramki z `FrameMark` po każdym `present`;
   - wykresy `physics_ms` i `gpu_ms`: te same wartości co HUD (pula kwerend `ParticleWorld`).
   W wolnym reżimie porównaj długość `physics tick` na GPU z odstępem między jego wysłaniem a wykonaniem: rosnący
   odstęp przy tej samej długości stref oznacza kolejkę albo zegar, dłuższe strefy oznaczają wolniejszą pracę GPU.
   Zestaw to z telemetrią z tego samego okna czasu (`GpuTelemetry.psm1`).
   Uwaga: z podłączonym profilerem strefa `tracy::GpuCtx::Collect` (odczyt kwerend po `present`) zajmuje na
   GTX 1650 średnio ok. 10 ms na klatkę. Długości stref GPU są wiarygodne, ale odstępy na osi CPU i FPS są wtedy
   zawyżone.
5. Zrzut ekranu osi czasu z GTX 1650 dołącz do opisu PR. Plik `.tracy` jest surowymi danymi i trafia do
   strefa-zero-measurements, nie do tego repozytorium.

### RenderDoc (jedna klatka, compute i rysowanie)

1. Zainstaluj RenderDoc (testowane API in-app z v1.47; działa każda wersja z API 1.1.2 lub nowszym).
2. Uruchom StrefaQA na jeden z dwóch sposobów:
   - z RenderDoc: *Launch Application*, plik `StrefaQA.exe`, argumenty jak wyżej, katalog roboczy = katalog buildu;
   - bez interfejsu RenderDoc: ustaw `STREFA_RENDERDOC=1` (biblioteka `renderdoc.dll` ze ścieżki albo
     `C:\Program Files\RenderDoc\renderdoc.dll`) albo pełną ścieżkę do biblioteki. Na Linuksie analogicznie
     `librenderdoc.so`.
3. **F11** przechwytuje następną klatkę. Pliki `strefa_*.rdc` trafiają do `<profil>/renderdoc/`, a konsola wypisuje
   ich ścieżkę. Klawisze F12/PrtScn RenderDoc są wyłączone. Bez RenderDoc F11 nic nie robi. Działa tylko w StrefaQA,
   nie w StrefaZero.
4. Programy i bufory mają etykiety obiektów, więc w *Event Browser* widać, który dispatch to który kernel. Grupy
   debugowania (`Physics tick`) pojawiają się tylko w kontekście debug (build Debug albo `--gl-debug`).

### Nsight Graphics (profil sprzętowy na GTX 1650)

1. Nsight Graphics (NVIDIA, darmowy) obsługuje Turing, czyli GTX 1650. Użyj *GPU Trace* do obciążenia jednostek
   i przepustowości pamięci w kernelach compute, a *Frame Debugger* do pojedynczej klatki.
2. Uruchom zwykły build Release (bez Tracy, bez RenderDoc), bo kilka narzędzi przechwytujących OpenGL naraz się
   wyklucza. Ustaw *Activity* = OpenGL i podaj te same argumenty co wyżej.
3. Liczby z GPU Trace opisują jedną klatkę na kontrolowanym zegarze. Nie porównuj ich z benchmarkiem; używaj ich do
   wskazania kernela, który ogranicza czas ticku.

## Jak czytać wyniki

Symulacja ma 120 ticków na sekundę niezależnie od FPS. Przy `--vsync on` (60 Hz) każda klatka wykonuje 2 ticki.
Przy `--vsync off` gra rysuje setki klatek na sekundę, a większość z nich nie wykonuje żadnego ticku. **Mediana czasu
klatki bez VSync opisuje więc głównie klatki bez fizyki** i nie mówi, czy scena zmieści się w budżecie. Do tego służą:

- `frames_0_ticks` / `frames_1_tick` / `frames_2plus_ticks` z medianami czasu klatki osobno dla klatek z 0, 1 i 2+
  tickami;
- `gpu_ms_per_sim_second`: milisekundy GPU na sekundę symulacji, czyli (średnia fizyki + średnia prezentacji) × 120;
  nie zależy od FPS ani VSync;
- `gpu_ms_per_wall_second`: milisekundy GPU na sekundę zegara w tym przebiegu, czyli
  (fizyka + prezentacja) × ticki/s + render × FPS;
- `budget_60fps`: udział budżetu czasu GPU przy grze w czasie rzeczywistym w 60 FPS, czyli
  ((fizyka + prezentacja) × 120 + render × 60) / 1000 ms. Wartość ≥ 1 oznacza, że GPU nie nadąży i zegar zacznie
  pomijać czas (`dropped_seconds`). W wierszu podsumowania to „budżet 60 FPS”.

Sceny przejściowe (np. `gpu:dam-break`, `gpu:avalanche`) zmieniają się w czasie, więc wynik jest dodatkowo
podzielony na pierwsze 10 s okna pomiaru (`early_*`) i resztę (`late_*`): mediana klatki, ticki/s i średni czas GPU
ticku.

## Kolumny wyników

`scene-benchmark.csv` ma jeden wiersz na scenę i przebieg (`pass`):

- `status`: `ok`, `invalid` (okno ukryte, zminimalizowane, zasłonięte albo ukryte przez DWM w części klatek) lub
  `failed` (błąd sceny albo przerwanie przez QUIT), z powodem w `status_reason`; liczniki `hidden_frames`,
  `minimized_frames` (SDL albo `IsIconic`), `occluded_frames`, `unfocused_frames` (brak fokusu nie unieważnia
  pomiaru), `cloaked_frames` (`DWMWA_CLOAKED`, tylko Windows) i `warmup_away_frames` (klatki rozgrzewki i klatka
  otwierająca okno pomiaru z oknem poza ekranem; dwie ostatnie kolumny pliku);
- `window_seconds`: faktyczny czas okna pomiaru;
- czas klatki: mediana, p95, p99 i max;
- rozkłady GPU ze wszystkich zapytań zakończonych w oknie pomiaru (zbierane co klatkę, bez limitu):
  - `physics_*`: komendy, solver, diagnostyka i tick termiki;
  - `presentation_*`: prezentacja termiki, tick oświetlenia, aktualizacja VFX i zdarzenia;
  - `render_*`: przebieg renderowania świata;
  - `*_samples`: liczba próbek; `*_truncated=1`: próbki utracone przez pełną kolejkę;
  - `*_mean_ms`, `*_median_ms`, `*_p95_ms`, `*_p99_ms`;
- metryki kosztu z sekcji wyżej;
- `cpu_step_*`: krok wątku modelu referencyjnego CPU (sceny `lab:`, które nie mają czasu GPU);
- `ticks_per_frame_*`, `ticks_per_second`, `frames_per_second`;
- `dropped_seconds`: czas pominięty przez zegar; w scenach `lab:` jest to spóźnienie wątku;
- `backlog_*`: zaległość zegara;
- `route_resets`: restarty trasy misji;
- `start_utc_unix_ns` i `end_utc_unix_ns`: granice okna pomiaru.

`scene-benchmark-frames.csv` ma wiersz na klatkę: liczbę i sumę próbek GPU zakończonych w tej klatce, flagi okna SDL
(`visible`, `focused`, `minimized`, `occluded`), `route_reset` i `cloaked` (ostatnia kolumna).

`telemetry.csv` ma te same znaczniki `utc_unix_ns` (UTC, ns od 1970), więc okno sceny łączy się z próbkami
telemetrii przez zakres (`Get-GpuTelemetryWindow`). Powody ograniczenia zegara są w kolumnach `reason_*` (1 = aktywny).
`gpu_idle` oznacza tylko, że GPU czekało na pracę. `sw_power_cap` to praca na limicie mocy, zwykła dla laptopa pod
obciążeniem: raportuj go i porównuj pomiary przy tym samym limicie. `hw_slowdown`, `hw_thermal_slowdown`,
`hw_power_brake_slowdown` i `sw_thermal_slowdown` dyskwalifikują okno do porównań; `soak.ps1` oznacza je kolumną
`disqualified`.
