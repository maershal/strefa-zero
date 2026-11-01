# I04.5-D — odczyty sąsiadów i aktualny strumień posortowany

Prace: 2026-10-30. Baza: lokalne I04.5-A/F/C/B na `6f318ea`. Historyczne [A/F](I04_5_GPU_STABILIZATION.md), [C](I04_5C_GPU_WORK.md) i [B](I04_5B_GPU_TRANSPORT.md) pozostają niezmienione.

Zaimplementowano i porównano trzy sposoby odczytu sąsiadów. **18/18 procesów porównawczych zakończyło się poprawnie; domyślne pozostaje `full`.** Wąski odczyt ma niewielką przewagę mediany misji, ale nie daje podobnego zysku w większych scenach. Cache posortowany zwiększa koszt scen 16k/32k i zajmuje dodatkowe 2 MiB. Nowe warianty pozostają eksperymentalne. Cel 1,50 ms/tick oraz odbiór całego I04.5 są otwarte.

## Co porównano

| `--gpu-neighbor-reads` | Dane odczytywane przez interakcje | Dodatkowy magazyn GPU |
| --- | --- | ---: |
| `full` | dotychczasowy `Grain` według stabilnego ID | 0 B |
| `narrow` | aktualne XY pozycji, XY kompensacji oraz velocity/mass/type z tablic ID | 0 B |
| `sorted` | te same pola w aktualizowanym strumieniu według wpisu indeksu komórek | 2 097 152 B |

Zmiana dotyczy kontaktu ziaren, gęstości/diagnostyki PBF, korekty PBF i XSPH. Odtwarzanie prędkości nadal czyta pełny stan, ponieważ wymaga także poprzedniej pozycji i historii kompensacji. Kolejność działań i reguły fizyki pozostają zachowane. Sam zapis węższego odczytu w GLSL nie dowodzi zmniejszenia ruchu pamięci: kompilator może usuwać nieużywane pola również z `full`. Nie mierzono sprzętowych transakcji pamięci ani kodu maszynowego shaderów.

Wariant `sorted` korzysta z dwóch banków po 32768 wpisów × 32 B. Wpis zawiera `position.xy`, `residual.xy` i pełne `motion` (prędkość, odwrotna masa, typ). Stabilne ID pozostaje w dotychczasowym `order[]`; gather używa bezpośrednio swojego bieżącego numeru wpisu. Mapa `sortedIndex[id]` służy producentom zapisującym zmieniony stan.

Scatter inicjalizuje oba banki ze stanu autorytatywnego. Każdy późniejszy producent przez `storeParticle()` aktualizuje odpowiedni bank wraz z zapisem właściwych cząstek; wybór banku odpowiada ping-pongowi, a zapis in-place zachowuje bieżący bank. Odświeżane są również kompensacja i prędkość, a nie tylko pozycja. Edycje członkostwa, spawn i usuwanie unieważniają stary indeks; nowy ID nie jest publikowany pod starym wpisem mapy odwrotnej. Odbudowa aktywnych slotów unieważnia generację przed dalszą pracą. Przy nieważnym cache odczyt korzysta ze stanu autorytatywnego.

Cache mieści się w istniejącym SSBO 3, za `order[]`, mapą odwrotną i anchorami, przed metadanymi sprzężenia. Offset metadanych uwzględnia pełne dwa banki; anchorów nie usunięto. Nadal używane jest osiem wiązań SSBO. Bufory świata rosną z 8 821 464 do 10 918 616 B, bez stagingu uploadu/HUD i pamięci sterownika. Nie dochodzi nowy dispatch; dodatkowe zapisy wykonują istniejący producenci. Aktualizacja uniformów banków ma własny koszt CPU. Dla indeksu linked żądanie `sorted` jawnie przechodzi na `narrow` i zapisuje efektywny tryb w metadanych.

## Walidacja aktualności i fizyki

`--gpu-hot-check` porównuje `narrow`, `sorted` oraz `sorted` z obiema opcjami C z referencyjnym odczytem `full`. Małe sceny obejmują głęboką penetrację, kontakt mieszany na granicy kafelka, korektę 96,2 komórki, kompensację i prędkość, cienką ścianę i narożnik. Sprawdzane są stabilne ID/materiały i wszystkie pola aktywnych cząstek, z limitem błędu 0,0001. Walidacja obejmuje także spawn/erase, pełne 32768 slotów, eksplozję, reset, metadane sprzężenia oraz fallback linked.

Niezależny audyt kopiuje bieżący bank cache i mapę do stagingu, po czym porównuje bitowo geometrię i motion z autorytatywnym stanem wszystkich aktywnych ID. **Audyt nie buduje indeksu i nie naprawia cache.** Sprawdza też zakres i unikalność wpisów. Kopie i synchroniczny odczyt występują tylko w walidacji, poza normalnym runtime i pomiarem wydajności.

Końcowa wstępna kontrola SoA/reused z `storage=auto` przeszła: **88 audytów bitowych, maksymalny błąd stanu 0**, trzy warianty oraz fallback linked. Instrumentacja `sorted` przeszła kontrolę elapsed/timestamp/off i braku początkowych kopii przy wyłączonym HUD. [Walidacja](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/hot-final-soa-reused-immutable/hot-tests.txt), [błędy stanu](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/hot-final-soa-reused-immutable/hot-errors.csv), [instrumentacja](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/profile-sorted/instrumentation-tests.txt).

**Pełny CTest: 35/35 PASS, kod 0, 1048.27 s.** Siedem nowych testów obejmuje cztery kombinacje AoS/SoA × sorted/reused, pełną misję narrow i sorted oraz walidację z automatycznym immutable. Każdy z pięciu testów cache wykonał 88 audytów bitowych. Przeszły także wcześniejsze testy B/C, kompletności sąsiedztwa, sprzężenia, UI, GL 3.3 i CPU. Nie zmieniono dotychczasowych limitów ani tolerancji. [Log](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/ctest.log), [JUnit](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/ctest.xml), [podsumowanie](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/ctest-summary.json).

Nowe kombinacje shaderów podczas pierwszej kompilacji wydłużyły część testów; wyniki przepustowości wykluczają inicjalizację. Kolekcja QA obejmuje tylko pliki świeższe niż początek bieżącego przebiegu. [Proweniencja](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/qa/collection-provenance.json). Sukces obecnej sesji nie unieważnia historycznych błędów C ani nie dowodzi usunięcia zmienności między sesjami.

## Protokół i wyniki przepustowości

```powershell
.\StrefaZero.exe --gpu-hot-check --profile .\qa-hot
.\scripts\profile-hot.ps1 -Executable '<gra>\StrefaZero.exe' -Output '<nowy katalog>' -Variants full,narrow,sorted -Repetitions 6 -SkipWindows
python .\scripts\analyze-stabilization.py --input '<wyniki>'
```

GTX 1650, NVIDIA 610.47, OpenGL 4.3, Release MSVC `/W4 /WX`, IPO. Każdy z 18 natywnych procesów mierzy świeży świat misji, 16k i 32k; rotowany porządek Latin powtórzono dwukrotnie. Każda scena: 120 ticków rozgrzewki, 240 pomiarowych i 120 próbek renderu. Render 960 × 576, elapsed, liczniki i diagnostyka ON, sprzężenie combined, SoA/reused, storage direct, okres HUD 2, obie opcje C OFF. Drain/poll co 16 ticków są poza CPU submit, ale w czasie ściennym. Kompilacja świata i końcowa walidacja nie wchodzą do czasu kroku.

Reguła natywnej kompilacji potwierdza `/W4 /WX /GL`, C++23 i Release. [Konfiguracja](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/build-config.json), [log budowania](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/build.log).

Wszystkie procesy używały EXE SHA256 `cca9aba9e226d786b55e5da05ddbc14357b1f6556404fec097032d67d7f7ee7a`. **18/18 kod 0, bez timeoutu; analyzer schema 5: 324 wiersze, zero ostrzeżeń.** Analyzer sprawdza także żądany/efektywny odczyt sąsiadów; dopuszcza fallback sorted→narrow tylko dla linked. Składnia PowerShell 5.1 oraz kontrole niezgodnych trybów i fallbacku przeszły. [Procesy i dokładne komendy](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/throughput/runs.csv), [analiza](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/throughput/analysis.csv), [składnia](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/script-syntax.json), [kontrole analyzera](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/analyzer-fixture-checks.json).

Poniżej mediana sześciu median GPU, ms/tick. Kolumna p95 jest medianą sześciu percentyli; nie jest percentylem połączonej próbki.

| Odczyt | Misja | 16k | 32k | Misja p95 | Misja CPU submit mediana / p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| full | 2,84058 | 1,78693 | 3,19414 | 3,34070 | 0,00700 / 0,02315 |
| narrow | 2,77818 | 1,78653 | 3,17888 | 3,17915 | 0,00700 / 0,02575 |
| sorted | 2,86290 | 1,92035 | 3,33792 | 3,31869 | 0,00710 / 0,01850 |

Narrow skrócił medianę misji o 2,20%, 16k o 0,02%, a 32k o 0,48%. Zakresy median misji nakładają się: full 2,77830–2,89501 ms, narrow 2,69856–2,83059 ms. Sorted wydłużył medianę misji o 0,79%, 16k o 7,47%, 32k o 4,50%. Czas ścienny na tick misji: full 2,93131 ms, narrow 2,84774 ms, sorted 2,95031 ms; dla 16k cache zwiększył go z 1,76556 do 1,92973 ms. [Wszystkie percentyle i próbki](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/throughput/comparison.json).

Nie wykonano testu istotności ani pomiaru na drugim GPU. Porównanie potwierdza koszt obecnej implementacji cache; nie dowodzi, że każdy inny układ posortowany będzie wolniejszy. Niewielka przewaga wąskiego odczytu w jednej scenie nie wystarcza do zmiany domyślnej ścieżki. Nie łączymy tych danych ze starszymi seriami w jedną średnią.

Liczniki misji potwierdzają identyczną liczbę dispatchy (średnio 140,07/tick), barier (140,32), budów indeksu (2) i redukcji postaci (26). Sorted zwiększa liczbę aktualizacji uniformów z 3 do 19 i obserwowanych wywołań API z 535,89 do 551,89/tick. Timery nie zgubiły mierzonego ticka. Mediana sześciu czasów tworzenia świata misji: full 68,94 ms, narrow 67,23 ms, sorted 68,71 ms; są to uruchomienia ze znanymi shaderami w cache sterownika. [Pamięć, inicjalizacja i liczniki](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/accounting.json).

## Dmax i warunkowe E

Przed zmianami uruchomiono istniejący `--gpu-neighbor-profile` na zachowanym buildzie B (`dec2c499…1387526b`): pięć układów × trzy sceny × trzy powtórzenia. Testy all-pairs, dużych korekt, widoczności, pojemności i edycji przeszły. Dla produkcyjnego SoA/reused końcowy globalny Dmax we wszystkich dziewięciu snapshotach wyniósł **0,0625 komórki**. Misja miała medianę 27 kandydatów, p95 39, p99 44, maksimum 45–46 wobec mediany 12 rzeczywistych sąsiadów; 16k/32k: 25 kandydatów wobec 10 sąsiadów. [Pełna baza](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/baseline-neighbors/summary.csv), [testy](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/baseline-neighbors/neighbor-tests.txt).

To końcowe snapshoty, nie maksimum całej trajektorii po eksplozji. Mały końcowy Dmax nie wyklucza chwilowych outlierów. Ta seria miała timery grupowe ON; jej czasów GPU nie traktujemy jako bezpośredniej bazy do nowej serii z timerami grupowymi OFF.

**E odroczono:** brak dowodu, że globalne rozszerzenie okna dominuje w zwykłej misji. Przed lokalnym Dmax należy zebrać rozkład Dmax i pracy kandydatów w czasie, szczególnie przy eksplozjach, oraz koszt poszczególnych przebiegów. Dopiero potwierdzony koszt uzasadnia projekt i dowód kompletności dla sąsiada przeniesionego z odległego kafelka. Globalny konserwatywny zakres nadal obowiązuje; nie zastąpiono go stałym halo.

## Okno, paczka i decyzja

Wykonano sześć porównawczych prób widocznego okna full/narrow (po trzy, porządek naprzemienny tworzący ABBA), a osobno jedną diagnostyczną sorted. Wszystkie siedem procesów zakończyło się kodem 0: po 900 klatek, wynik misji 3, bez utraty czasu. VSync OFF, limit 60 FPS, elapsed, liczniki i KHR_debug ON. Nie wymuszano fokusu; odnotowano go w 6090/6090 analizowanych klatkach, minimalizacja: 0.

| Odczyt / liczba prób | Mediana pełnej klatki | Zakres p95 prób | Największa klatka |
| --- | ---: | ---: | ---: |
| full / 3 | 16,6686 ms | 16,6692–16,6696 ms | 49,1930 ms |
| narrow / 3 | 16,6687 ms | 16,6697–16,6703 ms | 49,3107 ms |
| sorted / 1 | 16,6685 ms | 16,6696–16,6696 ms | 50,9796 ms |

Statystyki liczą pełny czas klatki po pierwszych 30 klatkach i obejmują testowe przechwycenie obrazu. W tabeli mediana jest medianą wyników prób. Dane bez przechwytywania oraz wszystkie p99 i maksima są zapisane osobno. [Pełne wyniki](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/window-summary.json), [porządek sześciu prób](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/windows-comparison/runs.csv), [osobna próba sorted](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/window-sorted-diagnostic/runs.csv). Trzy próby każdego wariantu nie stanowią sześciu prób odbioru jednej nowej ścieżki i nie zamykają pozostałych celów I04.5.

Łączne komunikaty migracji id=131186 według fazy odbioru callbacku: initialization 0, runtime 0, capture 0, validation 7, shutdown 0. Logger zgubił 0 komunikatów. Asynchroniczna dostawa może dotyczyć wcześniejszej pracy; faza odbioru nie dowodzi przyczyny ani fizycznej rezydencji pamięci. Callback pozostał aktywny.

Paczka `StrefaZero-Windows-x64-I04_5-D` przeszła własny `--gpu-hot-check --gpu-storage auto` oraz `--gpu-profile-check --gpu-neighbor-reads sorted` (oba kod 0). Wymuszony `--opengl33 --gpu-hot-check` zwrócił oczekiwane 77. EXE i assety odpowiadają finalnemu buildowi/źródłom. ZIP ma własny manifest SHA256 i kontrolę CRC; jest kompilacją diagnostyczną, bez nowego tagu wydania. [Kontrole paczki](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/package-checks.json), [walidacja](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/package-hot-check/hot-tests.txt), [zapis źródeł i EXE](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5d/source-state.json). Zweryfikowano niezmienność 2182 plików historycznych raportów/danych oraz sumy zewnętrznych paczek A/B/C.

**Decyzja:** `neighbor_reads=full`, `storage=direct`, obie opcje C OFF. Warianty narrow/sorted są dostępne do jawnych porównań. Badanie D zakończono jako eksperyment, E pozostaje warunkowe. Cel GPU <1,50 ms, robocze p95 <2 ms i przyczyna zmienności między sesjami nadal są otwarte. I05 czeka na ocenę całego I04.5. Następny eksperyment powinien przypisać koszt przebiegom i zbadać chwilowe rozszerzenia sąsiedztwa przed dodawaniem kolejnego indeksu. Propozycje i deklaracje QA w dostarczonym review nadal są materiałem do weryfikacji.
