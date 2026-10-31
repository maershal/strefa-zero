# I04.5-G3-A — obserwacja zmienności zamrożonej bazy

Data: 2026-10-30. **Zakończono ograniczoną próbę diagnostyczną i narzędzia. Nie odtworzono wcześniejszego przejścia do około 12 ms, więc przypisanie przyczyny G3 pozostaje otwarte. Nie ma nowego przyspieszenia ani nowego EXE.**

Baza to niezmieniona paczka G1, SHA-256 `1a1f53457ca590187417818f24d42bbea93d11c7b29bc5e7d8fea7320f918d5e`. G2 `333287c4063fa722eb4314fd70c0f6509cc92479c7890ee1da772bf1b0443af8` służy wyłącznie jako obciążenie/test poprawności pomiędzy pomiarami G1. [Stan wejściowy](gpu-i04-5g3/base-state.json), [niezmienność obu EXE i bieżącego runtime](gpu-i04-5g3/runtime-integrity.json). Źródła C++/GLSL i ustawienia domyślne z G1/G2 nie zostały zmienione w G3. Pełne 52/52 QA pozostaje wynikiem G2; nie przedstawiamy go jako nowego wykonania.

## Protokół i zakres narzędzi

Dodano [supervisor PowerShell 5.1](../scripts/profile-variance.ps1), [obserwator Win32 w C#](../scripts/variance-window-observer.cs), [osobny odczyt silników GPU Windows](../scripts/variance-gpu-observer.ps1) i [analyzer](../scripts/analyze-variance.py). Supervisor przypina hashe EXE, zapisuje plan przed pierwszym uruchomieniem, prawdziwe PID/argumenty/czas/kod procesu i kończy tylko własne procesy po timeout. Katalogi scen tworzy przed uruchomieniem, aby stary G1 wyeksportował surowe CPU/liczniki. Nie ustawia priorytetu, zasilania, fokusu ani parametrów sterownika i nie zamyka innych aplikacji.

Kolejno, bez nakładania natywnych prób GPU:

1. Dwie świeże kontrole G1 przez dotychczasowy harness: domyślne oraz jawne grupy 256. Misja **2,840576 / 2,820096 ms**, kody 0. [Dane](gpu-i04-5g3/initial-control/).
2. Cztery G1 OFF w układzie obserwator okna **OFF/ON/ON/OFF**; NVIDIA SMI we wszystkich, worker liczników silników GPU wyłączony. [Ustalona kolejność](gpu-i04-5g3/controls/schedule.csv), [analiza](gpu-i04-5g3/controls/analysis.json).
3. Jedna ustalona seria **21 procesów**: walidacja sond G1, dwa G1 OFF, Q G2 ON/PBF64 i kontrola G1, Q ON/PBF128 i kontrola G1, bounds/OFF, snapshots/OFF, passes/OFF, widoczne/widoczne/ukryte OFF, prawdziwy smoke okna/OFF, 20 s przerwy/OFF, drugi snapshots/OFF. [Plan](gpu-i04-5g3/reproduction/schedule.csv), [rzeczywiste procesy](gpu-i04-5g3/reproduction/runs.csv). To podzbiór potencjalnych bodźców; nie powtórzono całego dawnego preludium 52 testów CTest.

Łącznie **25/25 procesów głównego protokołu**, plus **2/2 kontroli początkowych**. Koszt: 120 ticków rozgrzewki i 240 pomiarowych na każdą z czterech scen, general/PBF/reduction 256, full/direct, reused/SoA, combined, HUD auto/2, elapsed, C OFF. Sondy wyłącznie w jawnie nazwanych próbach. Zachowano **20160 surowych wierszy CPU/liczników** głównego protokołu, **22080** z kontrolami początkowymi. GPU ma percentyle per proces ze 240 próbek; stary G1 nie zapisuje indywidualnych czasów GPU. Nie sklejamy percentyli procesów.

Observer co około 250 ms odczytuje własne okna, widoczność, minimalizację, PID pierwszoplanowego procesu, prostokąt zewnętrzny, skumulowany CPU/working set, priorytet oraz AC/baterię. Bez tytułów okien i nazw obcych aplikacji. Pomiar czasu obserwatora zachowano: ma własny koszt, podobnie jak SMI i WMI. OFF/ON mediana dwóch median GPU misji **2,867168 / 2,867936 ms**, czas ścienny **2,946310 / 2,946642 ms**; dwie pary nie dowodzą zerowego narzutu. Kontrola ta nie izoluje dodatkowego kosztu workera WMI, obecnego w całej serii 21 procesów. [Agregaty](gpu-i04-5g3/aggregates.json).

## GPU, CPU i czas ścienny

GTX 1650, sterownik 610.47. Poniżej mediana / p95, ms/tick, dla misji; czas ścienny to średnia całej partii. Drain/poll co 16 ticków jest poza CPU submit, ale wewnątrz czasu ściennego partii. Surowy profil `total_ms` i `cpu_submit_ms` mają nieco inne granice; nie utożsamiamy ich ani nie sumujemy median faz.

| Próba | GPU mediana / p95 | CPU submit mediana / p95 | Średnia ścienna |
| --- | ---: | ---: | ---: |
| hidden-before-1 | 2.897920 / 3.304864 | 0.007900 / 0.034600 | 2.969994 |
| hidden-before-2 | 2.820160 / 3.368896 | 0.008100 / 0.057300 | 2.945067 |
| hidden-after-q64 | 2.816000 / 3.364864 | 0.008400 / 0.052200 | 2.925652 |
| hidden-after-q128 | 2.785088 / 3.311616 | 0.007800 / 0.026100 | 2.873604 |
| bounds | 3.292960 / 3.756192 | 0.021500 / 0.094100 | 3.395823 |
| hidden-after-bounds | 2.888320 / 3.300960 | 0.008100 / 0.023000 | 2.973972 |
| snapshots-1 | 3.244128 / 3.928160 | 0.022300 / 0.083100 | 3.476027 |
| hidden-after-snapshots | 2.876192 / 3.410304 | 0.012700 / 0.071300 | 2.991353 |
| passes | 2.854912 / 3.345888 | 0.692300 / 1.113300 | 2.939407 |
| hidden-before-visible | 2.839744 / 3.282112 | 0.007900 / 0.017900 | 2.916700 |
| visible-1 | 2.875392 / 3.217408 | 0.011700 / 0.066200 | 2.945727 |
| visible-2 | 2.842624 / 3.207168 | 0.013000 / 0.077400 | 2.919448 |
| hidden-after-visible | 2.873344 / 3.259040 | 0.756500 / 1.132200 | 2.954277 |
| hidden-after-window | 2.850560 / 3.253760 | 0.007800 / 0.027400 | 2.923287 |
| hidden-after-idle | 2.829856 / 3.332192 | 0.007800 / 0.028200 | 2.921034 |
| snapshots-2 | 3.281856 / 3.824896 | 0.016000 / 0.081700 | 3.483831 |
| hidden-closing | 2.863296 / 3.391520 | 0.011800 / 0.072700 | 2.967750 |

W 11 ukrytych kontrolach bez sond zakres median GPU to **2,785088–2,897920 ms**, mediana median **2,850560 ms**, mediana p95 **3,311616 ms**. Widoczne OFF daje **2,842624–2,875392 ms**. W układzie H/V/V/H ostatni ukryty proces ma CPU submit **0,7565 ms** przy GPU **2,873344 ms**; następna kontrola po prawdziwym oknie ma CPU **0,0078 ms** i GPU **2,850560 ms**. Sam wzrost czasu CPU nie wystarcza do rozpoznania historycznego skoku GPU. Nie dowodzi też, że poprzednie widoczne okno było przyczyną; wystąpiła jedna taka sekwencja, bez ablacji przyczyny.

Próba `passes` również ma wysokie CPU **0,6923 ms**, a GPU **2,854912 ms**. Bounds/snapshots podnoszą GPU do około **3,24–3,29 ms**. To narzut sond, nie wynik gołej fizyki i nie naprawa ani odtworzenie reżimu około 12 ms. Wszystkie cztery sceny oraz p99/maksima są w [analizie](gpu-i04-5g3/reproduction/analysis.csv); [CPU i liczniki](gpu-i04-5g3/reproduction/counter-analysis.csv).

Ponowne przeliczenie starych sześciu G1 z każdej serii G2 potwierdza zmianę głównie wewnątrz zlecania solvera. Poniżej mediana sześciu median surowych faz CPU, ms:

| Faza / licznik | Dawna wolna seria | Dawna szybka seria |
| --- | ---: | ---: |
| total_ms | 1,542025 | 0,007800 |
| solver_ms | 1,383575 | 0,006200 |
| prepare_ms | 0,090525 | 0,000750 |
| poll_ms | 0,034925 | 0,000500 |
| timer_begin_ms / timer_end_ms | 0,002400 / 0,003650 | 0,000100 / 0,000100 |
| Dispatch / bariery, mediana (maksimum) | 140 (142) / 140 (142) | 140 (142) / 140 (142) |
| Budowy indeksu / redukcje postaci | 2 / 26 | 2 / 26 |
| Wiek HUD, mediana / maksimum | 20 / 28 | 20 / 28 |

Pamięć urządzenia misji pozostaje **8821476 B**. Zestawienie nie udowadnia identycznej geometrii/kandydatów ani konkretnego blokującego wywołania GL. [48 scen z dawnych procesów G1](gpu-i04-5g3/historical-g1-cpu.json), [agregaty](gpu-i04-5g3/historical-g1-cpu-aggregate.json). Dane historyczne odczytano bez zmiany ich plików.

## Dmax i kompletność kandydatów

Bounds i dwie serie snapshots zachowały **290976 wpisów Dmax** przed zapytaniami indeksowymi, **192 przechwycenia**, po dwa promienie, razem **384 rekonstrukcje** i histogramy. Każda rekonstrukcja ma zero pominiętych par geometrycznych; dla dużych scen odniesieniem jest niezależna świeża siatka CPU. All-pairs dotyczy małej walidacji adversarial, nie całych 32k. Powtarzające się grain-only 16k/32k mają zgodne badane histogramy; misja mieszana i wybuch różnią się.

Poniżej maksimum stosunku liczby kandydatów globalnego okna do świeżej siatki, promień 2; obok maksimum Dmax w przechwyceniach. Dwie wartości odpowiadają dwóm osobnym procesom snapshots:

| Scena | Maks. global/fresh, 1 / 2 | Maks. Dmax przechwyceń, 1 / 2 | Pominięte pary, 1 / 2 |
| --- | ---: | ---: | ---: |
| mission | 1.043700 / 1.045323 | 0.062500 / 0.062500 | 0 / 0 |
| stress_16k | 1.026872 / 1.026872 | 0.062500 / 0.062500 | 0 / 0 |
| stress_32k | 1.030688 / 1.030688 | 0.062500 / 0.062500 | 0 / 0 |
| mission_blast | 1.351685 / 1.193408 | 0.437500 / 0.250000 | 0 / 0 |

Pełny ślad przed dispatchami osiąga maksima **0,125 / 0,25 / 0,25 / 0,4375** dla mission/16k/32k/blast; w drugim snapshots blast osiąga 0,375. Żaden z tych śladów nie przekracza 1. [Ślady](gpu-i04-5g3/reproduction/bounds-analysis.csv), [rekonstrukcje](gpu-i04-5g3/reproduction/snapshot-analysis.csv), [podsumowanie kandydatów](gpu-i04-5g3/candidate-summary.json).

To dowód kompletności i kosztu okien w zbadanych próbach z sondami. **Nie ma przechwycenia wolnego reżimu około 12 ms.** Stary G1 zapisuje wyniki rekonstrukcji, histogramy i ślad Dmax; surowych buforów cząstek/indeksu nie utrwala na dysku. Nie można odtworzyć z nich pełnego stanu ani przypisać stanu z próby sondowanej do sąsiedniej próby OFF. Równe liczby dispatchy nie wykluczają różnic pracy wewnątrz shaderów. Lokalny Dmax nadal wymaga osobnego dowodu i A/B.

## Stan okna, GPU i działająca gra

Ukryte próby cost nie miały widocznych ani zminimalizowanych okien w próbkach; obie widoczne miały po **17 próbek widoczności i fokusu**, bez minimalizacji. Zasilanie AC=1 i priorytet Normal we wszystkich zachowanych próbkach. Widoczny, nieograniczony cost nie prowadzi regularnej pętli zdarzeń/present, więc nie jest testem FPS ani pełną ablacją wpływu DWM/fokusu. Znaczniki plików scen przybliżają koniec eksportu z rozdzielczością poll; nie są momentami konkretnego dispatchu. [Stany i koszt obserwatora](gpu-i04-5g3/host-summary.json).

Definicje Win32: [PID właściciela okna](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid), [okno pierwszoplanowe](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow), [enumeracja okien](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumwindows). Okno pierwszoplanowe może być chwilowo NULL; prostokąt `GetWindowRect` może być wirtualizowany przez DPI i nie jest rozdzielczością renderowania.

Worker Windows zachował **171 odczytów i 155 niezerowych wierszy instancji silników**. Zawarte w czasach procesów próbki innych PID miały maksimum instancji 3% 3D i 3% Copy. Nie sumujemy procentów między silnikami/LUID ani wieloma PID; brak wiersza nie jest dowodem zera. Wykluczamy znane PID innych prób protokołu jako odrębną kategorię. Liczniki są asynchroniczne, obejmują inicjalizację i mogą zachowywać zakończone PID; nie dają dokładnego nakładania z tickami. [Dane](gpu-i04-5g3/reproduction/gpu-engines.csv), [grupowanie](gpu-i04-5g3/reproduction/gpu-engine-analysis.csv), [czas odczytów](gpu-i04-5g3/reproduction/gpu-engine-polls.csv).

SMI obejmuje cały proces, nie tylko pomiar. Przykładowo pierwsza ukryta kontrola miała 62–67°C i zegar 1395–1905 MHz; ostatnia 72–75°C i 1845–1890 MHz, nadal przy około 2,9 ms. Brak dowodu przyczyny termicznej/rezydencji/throttlingu; same zakresy nie diagnozują GPU. [Zakresy wszystkich procesów](gpu-i04-5g3/nvidia-summary.json).

Prawdziwe widoczne okno G1: **900 klatek, 1798 ticków, wynik 3, pauza działa, zero odrzuconego czasu**. Mediana/p95/p99/maksimum całej klatki **16,6689 / 16,6695 / 16,6956 / 17,0219 ms**, limit 60 FPS, VSync OFF. Po pominięciu pierwszych 30 klatek fokus **870/870**, minimalizacja **0/870**. KHR_debug id=131186: runtime 0, walidacja 1, zgubione komunikaty 0; nie jest to dowód naprawy migracji. [Pełna kontrola](gpu-i04-5g3/reproduction/window-control/), [analiza okna](gpu-i04-5g3/reproduction/window-analysis.json).

## Weryfikacja, artefakty i dalszy zakres

- Natywna walidacja G1: OFF/passes/bounds/snapshots, reset generacji i jawne przepełnienie pojemności — PASS; [log](gpu-i04-5g3/reproduction/validation/cost-tests.txt).
- Oba Q G2 przeszły: 180 checkpointów, 720 porównań i 2/2 wykryte wyłączenia sprzężenia. Stara diagnostyka 90 kroków pozostaje jawna, bez zmiany kryteriów Q.
- Analyzer sprawdza plan, kody/PID, hashe, rzeczywiste flagi/tryby, grupy, skończoność, 240 ticków każdej sceny, narzut sond, Dmax, kompletność rekonstrukcji i histogramów, stan smoke oraz działanie workera. **13/13 negatywnych kontroli odrzuconych**: m.in. inny EXE/grupa, brak ticka, NaN, sonda w OFF i pominięty sąsiad. [Kontrole](gpu-i04-5g3/analyzer-negative-checks.json).
- PowerShell **5.1.26100.9711**: składnia obu skryptów, kompilacja C# i odczyt Win32 — PASS. [Wynik](gpu-i04-5g3/script-syntax.json).
- **8550 historycznych plików i wszystkie osiem wcześniejszych zestawów sum paczek bez zmian**; [kontrola integralności](gpu-i04-5g3/historical-integrity.json). Nowe dane mają własny [manifest](gpu-i04-5g3/SHA256SUMS.txt); manifest główny obejmuje także narzędzia i raport.

Przenośny pakiet `outputs/StrefaZero-Diagnostics-I04_5-G3.zip` zawiera narzędzia, ten raport i dane, bez nowego runtime. Wymaga lokalnych zamrożonych paczek G1/G2 o wskazanych hashach. Nic z G3 nie opublikowano na GitHubie. Przykład Windows PowerShell z katalogu repo/pakietu diagnostycznego:

```powershell
.\scripts\profile-variance.ps1 -BaselineExecutable 'C:\pomiar\G1\StrefaZero.exe' -Output 'C:\pomiar\controls' -Protocol controls
.\scripts\profile-variance.ps1 -BaselineExecutable 'C:\pomiar\G1\StrefaZero.exe' -TriggerExecutable 'C:\pomiar\G2\StrefaZero.exe' -Output 'C:\pomiar\reproduction' -Protocol reproduction -ObserveGpuEngines
python .\scripts\analyze-variance.py --input 'C:\pomiar\controls'
python .\scripts\analyze-variance.py --input 'C:\pomiar\reproduction'
```

**Decyzja:** G3-A zamknięte jako ograniczony eksperyment z wynikiem negatywnym odtworzenia; całe G3 otwarte. Kolejne G3-B ma odtworzyć pełne historyczne preludium 52 testów + Q na zamrożonych binariach, z gotowym obserwatorem, a po rzeczywistym spowolnieniu zachować kontrole OFF i osobno bounds/snapshots/OFF. Z góry ustalić budżet i sygnał diagnostyczny (np. dwie mediany misji >6 ms; to próg przechwycenia, nie kryterium QA), nie powtarzać w nieskończoność aż do pożądanego wyniku. Jeśli powróci wolny reżim, potrzebny jest pomiar dokładnego zlecania/oczekiwania GL lub zewnętrzny ślad per PID, skorelowany w czasie z pracą sceny; obecne eksporty końca scen nie wystarczą do przypisania konkretnego wywołania. Nie podmieniamy przy tym mierzonego G1 ani nie przenosimy dowodu z osobnego EXE na starą bazę. Cel 1,5 ms, lokalny E i odbiór całego I04.5 pozostają otwarte; I05 nie rozpoczęto.
