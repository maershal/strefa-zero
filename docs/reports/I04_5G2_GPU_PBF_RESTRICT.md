# I04.5-G2 — kwalifikatory dostępu do rozdzielonych banków PBF

Data: 2026-10-30. Baza: lokalna, niezmieniona paczka [G1](I04_5G_GPU_PBF_GROUPS.md), EXE `1a1f53457ca590187417818f24d42bbea93d11c7b29bc5e7d8fea7320f918d5e`. Ostatni commit repozytorium nadal opisuje Q; zastany zakres G1 zachowano. EXE G2 `333287c4063fa722eb4314fd70c0f6509cc92479c7890ee1da772bf1b0443af8`. [Stan builda i źródeł](../measurements/gpu-i04-5g2/build-state.json), [stan wejściowy G1](../measurements/gpu-i04-5g2/base-source-state.json).

## Zakres i kontrakt

Dodano eksperymentalne `--gpu-pbf-restrict on|off`, domyślnie **OFF**. Przy programach specjalizowanych ON zmienia wyłącznie korektę PBF i XSPH (`liquids.comp`, operacje 1 i 4). Wejściowy bank otrzymuje `readonly restrict`, wyjściowy `writeonly restrict`. Gęstość/lambda, diagnostyka, ziarna, teren, postać, redukcje oraz in-place spawn/blast/debris pozostają bez tych kwalifikatorów. Żądanie w trybie ubershaderów ma efektywne OFF, jawnie raportowane.

`restrict` jest obietnicą o dostępie do pamięci, a nie sprawdzeniem wykonywanym przez GPU; złamanie obietnicy daje niezdefiniowane wyniki. Reguły kwalifikatorów dla członków i bloków SSBO opisuje [GLSL 4.30, §4.10–4.11, strony drukowane 83–85](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.30.pdf). W tej implementacji oba banki są pełnymi, odrębnymi obiektami buforów, wiązanymi przez `glBindBufferBase`.

Produkcja rozwiązuje rzeczywiste ID baz przed `UseProgram`, zmianą wiązań i dispatch. Dla oznaczonego programu kontrola wymaga niezerowych banków 0/1 oraz braku ich aliasowania z każdym innym z ośmiu wiązań SSBO. Brak zapytań o stan GL w kroku. Programy in-place nie otrzymują tej deklaracji ani kontroli. Bariery i harmonogram solvera są zachowane; nie dodano dispatchu, bufora GPU ani odczytu zwrotnego w runtime. Wariant ON obejmuje również koszt tej kontroli CPU; nie zakładamy, że jest darmowa.

Specjalizacja powstaje przy tworzeniu programu przez ścisłą zmianę dwóch deklaracji kanonicznego `particle_storage.glsl`, dla obu gałęzi AoS/SoA. Wymagane są dokładnie dwa wystąpienia każdej deklaracji, inaczej tworzenie programu kończy się błędem wymagającym przeglądu ABI. Nie ma drugiej kopii układu cząstki. OFF przekazuje dotychczasowy tekst GLSL bez zmian; pliki shaderów na dysku mają te same hashe co G1. To ogranicza też ponowną kompilację do programów rzeczywiście objętych zmianą.

## Poprawność

**Pełny CTest: 52/52 PASS, kod 0**, 593.03 s sumy przypadków; czas ścienny 593,13 s. Osiem nowych przypadków obejmuje kontrakt, ciecze, misję, Q i cztery układy AoS/SoA × sorted/reused z alternatywnymi odczytami oraz opcjami C. Dotychczasowe grupy 64/128, CPU, UI, transport, sąsiedztwo, trasy 30/60/144 FPS i GL 3.3 nadal przechodzą. [Log](../measurements/gpu-i04-5g2/ctest.log), [JUnit](../measurements/gpu-i04-5g2/ctest.xml), [podsumowanie](../measurements/gpu-i04-5g2/ctest-summary.json).

Test kontraktu wstrzykuje aliasy Input/Output z każdym innym SSBO oraz brak wiązania banku, w obu kierunkach ping-pong i AoS/SoA. **128/128 odrzuceń przed jakimkolwiek wywołaniem GL**, bez zmiany autorytatywnych cząstek. Test akceptuje tylko dedykowany błąd kontroli produkcyjnej; dodatkowy bezpiecznik uniemożliwia wysłanie błędnego dispatchu nawet po regresji kontroli. Po odrzuceniach przechodzą zwykły krok, wybuch i edycja/spawn/erase. Sprawdzono domyślne OFF i fallback ubershaderów.

Źródła odczytano z faktycznie dołączonych obiektów shaderów OpenGL przez `GetAttachedShaders`/`GetShaderSource`, wyłącznie w walidacji. ON i OFF różnią się tylko powyższymi kwalifikatorami w dwóch programach; pozostałe trzy programy cieczy są identyczne. Zachowano pary źródeł dla AoS/SoA: [kontrola i osiem plików GLSL](../measurements/gpu-i04-5g2/binding-source-check/pbf-restrict-tests.txt). Porównanie wykonano przed zapisem; zapis tekstowy używa zakończeń wierszy Windows. To dowód danych przekazanych kompilatorowi, nie dowód konkretnej optymalizacji kodu maszynowego.

Nośnik Q pozostaje przy PBF 256 i bez kwalifikatorów; repliki otrzymują badany wariant. Budżety kroku i skumulowanego defektu nie zostały zmienione. G2 OFF i ON mają po próbie z CTest i dwóch dodatkowych; ON 64/128 dodatkowo sprawdza współdziałanie z G1:

| Tryb / PBF | Procesy Q | Checkpointy / porównania | Wyłączenia sprzężenia odrzucone |
| --- | ---: | ---: | ---: |
| off_256 | 3/3 | 270 / 1080 | 3/3 |
| on_256 | 3/3 | 270 / 1080 | 3/3 |
| on_64 | 1/1 | 90 / 360 | 1/1 |
| on_128 | 1/1 | 90 / 360 | 1/1 |


Łącznie **8/8 procesów, 720 checkpointów, 2880 porównań, 8/8 wykrytych rzeczywistych wyłączeń sprzężenia**. [Niezależnie przeliczone sumy](../measurements/gpu-i04-5g2/q-summary.json). Dawna reguła 90-krokowych niezależnych trajektorii odrzuciła 3/8 prób diagnostycznych i nadal pozostaje dostępna jawnie. Wynik Q nie oznacza deterministyczności ani zaliczenia dawnego kryterium.

## Pomiary i zmienność bazy

GTX 1650, NVIDIA 610.47, OpenGL 4.3, Release MSVC `/W4 /WX`, IPO. W każdej serii: G1/G2 OFF/G2 ON × sześć powtórzeń, wszystkie sześć permutacji porządku. Pozycje i skierowane sąsiedztwo równoważone wewnątrz bloków po trzy procesy; brak gwarancji identycznych warunków systemowych. G2 OFF/ON używają jednego EXE, G1 osobnego niezmienionego EXE. Każda scena ma 120 ticków rozgrzewki i 240 próbek; general/PBF/reduction 256, full/direct, reused/SoA, combined, HUD auto/2, elapsed, C i sondy OFF. Poll/drain co 16 ticków poza CPU submit, ale w czasie ściennym partii. Próby GPU wykonywano pojedynczo.

Analyzer sprawdza kody i faktyczne argumenty procesów, żądany/efektywny tryb, grupy 256, liczebność, skończoność, próbki CPU i wyłączenie sond. Kontroluje niezmienność EXE, przypięty hash G1 i wspólne pliki GLSL. Każda seria ma 216 wierszy analizy i **17280 surowych wierszy CPU/liczników**; czasy GPU zachowano jako percentyle 240 próbek, bez indywidualnych czasów GPU. [Negatywne kontrole analyzera](../measurements/gpu-i04-5g2/analyzer-fixture-checks.json), [składnia skryptów](../measurements/gpu-i04-5g2/script-syntax.json).

Najpierw G1 osiągnęła **2,846752 ms** GPU misji w kontroli przed uruchomieniem kandydata. Po pełnym QA pierwsza seria dała około **12 ms na tym samym G1**, podobnie na obu G2. Następne kontrole G1 wróciły do **2,823328 ms** przy domyślnych i **2,830592 ms** przy jawnych grupach 256. Między tymi kontrolami nie zmieniono badanych EXE ani ustawień GPU. Zachowano [kontrolę początkową](../measurements/gpu-i04-5g2/baseline-before-candidate/summary.csv), [świeże kontrole](../measurements/gpu-i04-5g2/condition-control), [notatkę](../measurements/gpu-i04-5g2/condition-note.json). To obserwacja zmienności niezmienionej bazy, nie identyfikacja przyczyny.

Wykonano jedną dodatkową pełną serię w odtworzonych warunkach; obu serii nie łączono i żadnej nie usunięto. Nie przypisujemy różnicy 2,8↔12 ms kwalifikatorom G2, DWM, temperaturze, rezydencji VRAM ani sterownikowi bez dalszego dowodu. Telemetria obejmuje cały proces, także tworzenie świata i przejścia bezczynności; nie jest pomiarem konkretnego dispatchu.

Poniżej mediana sześciu median / mediana sześciu p95, ms/tick; zmiana par to mediana procentowych zmian median w odpowiadających powtórzeniach.

### Pierwsza seria — obserwowany wolniejszy przebieg bazy

| Scena | G1 | G2 OFF | G2 ON | ON vs OFF, mediana zmian par |
| --- | ---: | ---: | ---: | ---: |
| mission | 12,05741 / 13,53014 | 12,06563 / 13,53736 | 12,12938 / 13,62842 | 0,79% |
| stress_16k | 4,94499 / 6,52400 | 4,97061 / 6,46963 | 4,95976 / 6,49403 | 0,54% |
| stress_32k | 9,20440 / 10,98445 | 9,07984 / 11,00677 | 9,19450 / 11,09579 | 1,15% |
| mission_blast | 13,09902 / 14,61459 | 13,07125 / 14,62299 | 13,02802 / 14,46398 | -0,15% |


[Procesy](../measurements/gpu-i04-5g2/matrix-slow/runs.csv), [agregaty](../measurements/gpu-i04-5g2/matrix-slow/aggregate.csv), [liczniki i wiek HUD](../measurements/gpu-i04-5g2/matrix-slow/counter-summary.json). GPU G1 nie było stabilną referencją względem wcześniejszych sesji; CPU submit również był znacznie wyższy. Nie jest to dowód kosztu kontroli aliasów.

### Pełna powtórka po świeżych kontrolach G1

| Scena | G1 | G2 OFF | G2 ON | ON vs OFF, mediana zmian par |
| --- | ---: | ---: | ---: | ---: |
| mission | 2,83077 / 3,25038 | 2,81512 / 3,32418 | 2,81293 / 3,21909 | -0,27% |
| stress_16k | 1,78075 / 1,98427 | 1,78944 / 1,99346 | 1,78878 / 1,99827 | -0,07% |
| stress_32k | 3,18669 / 3,64504 | 3,16608 / 3,59710 | 3,18419 / 3,61619 | 0,30% |
| mission_blast | 3,02514 / 3,42730 | 3,01110 / 3,49176 | 3,02294 / 3,47576 | 0,39% |


[Procesy](../measurements/gpu-i04-5g2/matrix/runs.csv), [wszystkie GPU/CPU/p99/maksima/czas ścienny](../measurements/gpu-i04-5g2/matrix/aggregate.csv), [liczniki i pamięć](../measurements/gpu-i04-5g2/matrix/counter-summary.json). Nie łączono percentyli ani próbek między procesami i nie wykazano istotności statystycznej. Wnioski dotyczą badanych warunków, nie ogólnej naprawy wydajności.

## Widoczna gra i paczka

**6/6 widocznych prób okna**, G2 OFF/ON po trzy powtórzenia, kolejność OFF/ON, ON/OFF, OFF/ON, elapsed, VSync OFF, limit 60 FPS, debug/counters ON. Każda próba osiągnęła wynik 3, co najmniej 900 klatek i 1700 ticków, z działającą pauzą i bez odrzuconego czasu. To kontrole działającej gry na relokowanej paczce G2, z grupami 256 i pełnym sprzężeniem.

| Wariant | Mediana / p95 całej klatki, ms | Fokus / analizowane klatki | Zminimalizowane |
| --- | ---: | ---: | ---: |
| g2off | 16.66840 / 16.66990 | 2610 / 2610 | 0 |
| g2on | 16.66850 / 16.66910 | 2610 / 2610 | 0 |

W tabeli mediana trzech median i mediana trzech p95; pierwsze 30 renderowanych klatek pominięto. Przechwytywanie obrazu jest wliczone, największa klatka 48.74280 ms. Limit 60 FPS i czas oczekiwania nie pozwalają przypisać podobnej klatki przyspieszeniu fizyki. Fokus i minimalizację raportujemy jawnie.

KHR_debug id131186 według fazy odebrania callbacku: {'initialization': 0, 'runtime': 0, 'capture': 0, 'validation': 6, 'shutdown': 0}; zgubione komunikaty: 0. Faza odbioru asynchronicznego komunikatu nie jest dowodem przyczyny ani miejsca fizycznej pamięci. [Procesy](../measurements/gpu-i04-5g2/windows/runs.csv), [pełne podsumowanie](../measurements/gpu-i04-5g2/window-summary.json). Archiwum zachowuje CSV/logi; duże BMP z okien pominięto.

Paczka `StrefaZero-Windows-x64-I04_5-G2.zip` ma własne kontrole: [wyniki](../measurements/gpu-i04-5g2/package-checks.json), `QA.txt` i `SHA256SUMS.txt` w paczce. Obejmuje te same zasoby i testowany EXE; tryb ON jest jawny. Nie jest nowym tagiem wersji gry. Zachowano hashe 6558 wcześniejszych raportów/danych oraz paczek A–G1; [integralność historii](../measurements/gpu-i04-5g2/historical-integrity.json).

## Odtworzenie

```powershell
StrefaZero.exe --gpu-pbf-restrict-check --profile '<binding-QA>'
StrefaZero.exe --gpu-liquid-test --gpu-pbf-restrict on --profile '<liquid-QA>'
StrefaZero.exe --mission-test --gpu-pbf-restrict on --profile '<mission-QA>'
StrefaZero.exe --gpu-coupling-check --gpu-pbf-restrict on --profile '<Q-ON>'
.\scripts\profile-pbf-restrict.ps1 -Executable '<G2>\StrefaZero.exe' -BaselineExecutable '<G1>\StrefaZero.exe' -Output '<nowy katalog>'
python .\scripts\analyze-pbf-restrict.py --input '<nowy katalog>'
```

## Decyzja

**G2 zakończono jako badanie poprawnego, kontrolowanego wariantu; `pbf_restrict` pozostaje domyślnie OFF.** Nie potwierdzono powtarzalnego przyspieszenia. W powtórce z szybszą bazą mediana median misji G2 OFF/ON to 2,815120/2,812928 ms: około 0,08% różnicy; mediana zmian odpowiadających powtórzeń −0,27%. Nie uzyskano zysku w misji z eksplozjami. Sceny 16k/32k zawierają same ziarna i kontrolują niezmienioną ścieżkę, bez oczekiwanego zysku od kwalifikatorów PBF. CPU submit misji OFF/ON ma medianę median 0,00815/0,00840 ms i medianę p95 0,04985/0,03805 ms. Nie przypisujemy tych małych różnic konkretnej optymalizacji kompilatora.

Pierwsza pełna seria miała około 12 ms także na niezmienionym G1. Zachowano ją wraz z surowymi licznikami i telemetrią; nie mieszano jej z powtórką około 2,8 ms i nie traktowano odzyskania szybszego przebiegu jako efektu nowego kodu. Sama obserwacja stanów P0/P8, zegarów lub wolnej pamięci nie identyfikuje przyczyny. Historyczna zmienność została odtworzona wewnątrz jednej sesji, ale nie została naprawiona.

Pamięć logiczna buforów GPU pozostaje 8 821 476 B w G1 i obu G2. Brak nowych przebiegów i barier; CPU wykonuje dodatkowy preflight wyłącznie dla dwóch kwalifikowanych programów. Pełny zbiór kosztów, liczników i wieku HUD zawierają obie serie. Funkcjonalna poprawność 52/52, odrzucone błędne wiązania i Q nie stanowią odbioru celu **1,50 ms** ani gwarancji stabilnego czasu w każdej sesji. Full/direct, grupy 256, opcje C i sondy OFF pozostają ustawieniami bazowymi.

**Następne G3: przypisanie zmienności niezmienionej bazy.** Odtwarzać stałą sekwencję procesów G1 i kontrolować przejście przed/po na tym samym EXE, z zapisem trybu okna/fokusu, timerów, czasu CPU i telemetrii. Osobne, jawnie kosztowne przechwycenia Dmax/liczby kandydatów i stanu sceny powinny odróżnić większą pracę fizyki od opóźnień transportu/zlecania. Nie zakładać przyczyny DWM, temperatury, VRAM ani sterownika i nie zmieniać parametrów systemowych dla uzyskania korzystnego wyniku. Kolejne wskazówki dla PBF czekają na wiarygodne warunki; lokalny Dmax pozostaje odroczony. I05 nie zostało rozpoczęte, a odbiór całego I04.5 nadal jest otwarty.

