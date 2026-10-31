# I04.5-G — niezależny rozmiar grup PBF

Data: 2026-10-30. Baza: opublikowane Q, commit `7e85553109c35d390a151454011fa8836db44045`. To badanie kształtu uruchomień; nie zmienia równań, liczby podkroków/iteracji, tolerancji ani polityki zaległości zegara. Decyzję opisuje koniec raportu.

## Zakres

Dodano `--gpu-pbf-size 64|128|256`. Dotyczy gęstości/lambdy, korekty, diagnostyki gęstości i XSPH w `liquids.comp` (operacje 0/1/2/4). Zerowanie diagnostyki (3) nadal ma jeden wątek w programie specjalizowanym. Ziarna, predykcja i redukcje pozostają na 256 w porównaniu G.

Brak flagi zachowuje dotychczasowe dziedziczenie `--gpu-local-size`; zwykła gra nadal efektywnie używa 256. Warianty 64/128 są jawne i eksperymentalne. Nie dodano wskazówek `restrict`, zmian Dmax ani nowych metod fizyki.

Osobny argument indirect PBF powstaje na GPU z rzeczywistego `aliveCount`: `ceil(count/PBF_LOCAL_SIZE)`, także zero dla pustego świata. Argumenty są w istniejącym buforze Free, słowa 34–36; dotychczasowe offsety zachowano. Pojemność tego bufora wzrosła o **12 B**, bez dodatkowego SSBO, dispatchu, bariery ani odczytu CPU w kroku. Publikacja argumentów korzysta z dotychczasowej bariery command po przebiegu 12. Cząstki nadal mają stabilne adresy ID.

## Odtworzenie

Windows PowerShell 5.1, Release MSVC `/W4 /WX`, IPO, GTX 1650, NVIDIA 610.47, OpenGL 4.3. EXE G SHA256 `1a1f53457ca590187417818f24d42bbea93d11c7b29bc5e7d8fea7320f918d5e`; Q `50dd13f0f4999f1aca209c1fc4783483aba0454a31995dc36578949335c7fdb4`. Hashe źródeł/shaderów/skryptów: [build-state](gpu-i04-5g/build-state.json), konfiguracja i sprzęt: [środowisko natywnego harnessu](gpu-i04-5g/matrix/g256-1/environment.json).

```powershell
StrefaZero.exe --gpu-execution-check --profile '<QA-dispatch>'
StrefaZero.exe --gpu-liquid-test --gpu-pbf-size 64 --profile '<QA-liquid-64>'
StrefaZero.exe --mission-test --gpu-pbf-size 64 --profile '<QA-mission-64>'
StrefaZero.exe --gpu-coupling-check --gpu-pbf-size 64 --profile '<QA-Q-64>'
# Powtórzyć fizykę, misję i Q dla 128 oraz Q dla 256.
.\scripts\profile-pbf.ps1 -Executable '<G>\StrefaZero.exe' -BaselineExecutable '<Q>\StrefaZero.exe' -Output '<nowy katalog>'
python .\scripts\analyze-pbf.py --input '<nowy katalog>'
```

## Poprawność

**Pełny CTest: 44/44 PASS w protokole Q**, kod 0, 1434,50 s; czas obejmuje tworzenie wcześniej nieużywanych wariantów shaderów. Sześć nowych przypadków obejmuje ciecz, misję i Q dla 64/128; domyślne 256 przechodzi istniejące testy. Zachowano testy tras 30/60/144 FPS, szczelności cienkich ścian, gęstości CPU, zachowania mas/ID, resetów, transportu, C/D, CPU, UI i GL 3.3. [Log](gpu-i04-5g/ctest.log), [JUnit](gpu-i04-5g/ctest.xml), [podsumowanie](gpu-i04-5g/ctest-summary.json).

Rozszerzony test wykonania sprawdza rozmiary PBF 64/128/256 przy ogólnym i redukcyjnym 256: 0, 1, rozmiar−1/rozmiar/rozmiar+1, 255/256/257 oraz 32768 aktywnych cząstek. Nieciągłe ID są permutowane po pełnej puli. Sprawdza rzeczywiste argumenty GPU i niezależną sumę gęstości Poly6 double na CPU dla każdego aktywnego ID, także na końcu niepełnej grupy. [Wynik](gpu-i04-5g/qa/execution-profile/execution-tests.txt).

Q otrzymuje teraz wybraną konfigurację zamiast tworzyć domyślną bez względu na CLI. Ciągły nośnik referencyjny używa PBF 256, repliki wybranego rozmiaru. Dzięki temu wspólne checkpointy obejmują także różnicę względem 256. Wartości stałych budżetów i rzeczywista negatywna kontrola pozostają takie jak w Q. Każdy rozmiar ma próbę z pełnego CTest i dwie kolejne w niezależnych procesach:

| PBF | Powtórki Q | Checkpointy / porównania | Wykryte wyłączenia sprzężenia |
| --- | ---: | ---: | ---: |
| 64 | 3/3 | 270 / 1080 | 3/3 |
| 128 | 3/3 | 270 / 1080 | 3/3 |
| 256 | 3/3 | 270 / 1080 | 3/3 |


[Przeliczone z surowych CSV sumy i maksima](gpu-i04-5g/q-summary.json). To kontrola defektów na wspólnych stanach, nie dowód zgodności długich niezależnych trajektorii. Stary losowy iloraz pozostaje diagnostyczny: odrzucił 3/9 długich tras (po jednej dla każdego rozmiaru). Nie traktujemy wyniku Q jako zaliczenia starej reguły; pełne wyniki zachowano.

## Koszt

**Końcowa seria porównawcza: 16/16 procesów**, cztery warianty × cztery powtórzenia, cztery sceny w każdym procesie. Kolejność Williams równoważy pozycje i poprzedzający wariant. Q jest niezmienioną opublikowaną paczką, G 256 mierzy koszt samego rozdzielenia, G 128/64 zmieniają tylko rozmiar rodziny PBF w tym samym EXE. Próby GPU wykonywano pojedynczo. Pierwsza próba skryptu uruchomiła tylko cztery kontrole Q, a nowe flagi G odrzuciła lista dozwolonych argumentów przed startem procesu. Dopisano flagę i powtórzono całą zrównoważoną serię w świeżym katalogu. Dodatkowa pełna seria miała poprawne podsumowania, lecz brakowało plików CPU/liczników w katalogach scen. Nadzorca tworzy teraz te katalogi przed startem każdej wersji, a analyzer wymaga 240 wierszy i konfiguracji każdej sceny. Całą serię powtórzono; [notatka o eksporcie](gpu-i04-5g/raw-export-note.json). Bezpośrednie użycie `--gpu-cost-profile` wymaga przygotowania czterech katalogów scen albo użycia dołączonego harnessu. Wstępne próby nie wchodzą do poniższych porównań; [log i wyjaśnienie](gpu-i04-5g/matrix-retry-note.json) zachowują problem skryptu, bez zmian runtime i progów.

Każda scena: 120 ticków rozgrzewki, 240 pomiarowych; elapsed, SoA/reused, full/direct, combined, HUD auto/2, C OFF, sondy kosztów i grupowe timery OFF. `glFinish`/poll co 16 ticków poza CPU submit, ale w czasie ściennym partii. Eksplozje jak w E0. Analyzer sprawdza rzeczywiste argumenty procesu, efektywny PBF, stałe 256 pozostałych grup, liczbę próbek, liczebność, skończoność, wyłączenie sond i niezmienność EXE. [Kolejność](gpu-i04-5g/matrix/schedule.csv), [procesy](gpu-i04-5g/matrix/runs.csv), [analiza](gpu-i04-5g/matrix/analysis.csv).

Poniżej **mediana czterech median / mediana czterech p95**, ms GPU/tick. Nie łączono próbek w pozorny wspólny percentyl.

| Scena | Q | G 256 | G 128 | G 64 |
| --- | ---: | ---: | ---: | ---: |
| mission | 2,86483 / 3,29963 | 2,83006 / 3,27168 | 2,86413 / 3,28110 | 2,82413 / 3,26274 |
| stress_16k | 1,79469 / 2,14184 | 1,78902 / 2,06240 | 1,79630 / 2,06416 | 1,79877 / 1,98933 |
| stress_32k | 3,21443 / 3,66622 | 3,21752 / 3,70477 | 3,21552 / 3,67013 | 3,22499 / 3,69786 |
| mission_blast | 3,07936 / 3,56976 | 3,04909 / 3,51029 | 3,04432 / 3,50349 | 3,04160 / 3,51480 |


Mediana zmian median w odpowiadających sobie powtórzeniach; ujemna wartość oznacza krótszy czas:

| Scena | G 256 vs Q | G 128 vs G 256 | G 64 vs G 256 |
| --- | ---: | ---: | ---: |
| mission | -0,60% | 1,83% | -0,17% |
| stress_16k | -0,37% | -0,06% | 0,08% |
| stress_32k | 0,10% | 0,23% | 0,28% |
| mission_blast | -1,52% | 0,05% | -0,16% |


Pełne CPU, p99, maksima i czas ścienny są w [agregatach](gpu-i04-5g/matrix/aggregate.csv). Wynik pojedynczego pomiaru Q przed zmianą (misja GPU 2,78950 ms) zachowano [osobno](gpu-i04-5g/before-change/summary.csv); nie jest podstawą porównania nowych wariantów. Nie wykazano istotności statystycznej ani naprawy historycznej zmienności warunków GPU. Składnia PowerShell 5.1 i trzy negatywne kontrole analyzera przeszły: [składnia](gpu-i04-5g/script-syntax.json), [odrzucone błędne dane](gpu-i04-5g/analyzer-fixture-checks.json). Zachowano 15360 wierszy surowych kroków CPU/liczników; czasy GPU są zapisane jako percentyle 240 próbek na proces/scenę, bez pojedynczych surowych czasów GPU. [Liczniki i pamięć](gpu-i04-5g/counter-summary.json).

## Okno i paczka

**8/8 widocznych prób okna**, Q/G256/G128/G64 po dwa powtórzenia w odwróconej kolejności, elapsed, VSync OFF, limit 60 FPS, debug/counters ON. Każda próba osiągnęła wynik 3, co najmniej 900 klatek i 1700 ticków, z działającą pauzą i bez odrzuconego czasu. To kontrole działającej gry na paczce G oraz niezmienionej Q.

| Wariant | Mediana / p95 całej klatki, ms | Fokus / analizowane klatki | Zminimalizowane |
| --- | ---: | ---: | ---: |
| q | 16.66865 / 16.66950 | 1740 / 1740 | 0 |
| g256 | 16.66880 / 16.67015 | 1740 / 1740 | 0 |
| g128 | 16.66875 / 16.66960 | 1740 / 1740 | 0 |
| g64 | 16.66865 / 16.66935 | 1740 / 1740 | 0 |

W tabeli mediana dwóch median i mediana dwóch p95; pierwsze 30 renderowanych klatek pominięto. Przechwytywanie obrazu jest wliczone, największa klatka 50.28210 ms. Limit 60 FPS i czas oczekiwania nie pozwalają przypisać podobnej klatki przyspieszeniu fizyki. Fokus i minimalizację raportujemy jawnie.

KHR_debug id131186 według fazy odebrania callbacku: {'initialization': 0, 'runtime': 0, 'capture': 0, 'validation': 8, 'shutdown': 0}; zgubione komunikaty: 0. Faza odbioru asynchronicznego komunikatu nie jest dowodem przyczyny ani miejsca fizycznej pamięci. [Procesy](gpu-i04-5g/windows/runs.csv), [pełne podsumowanie](gpu-i04-5g/window-summary.json). Archiwum zachowuje CSV/logi; duże BMP z okien pominięto.

Zweryfikowano niezmienność 4930 wcześniejszych plików raportów/danych i sumy paczek A–Q: [integralność historii](gpu-i04-5g/historical-integrity.json).

Paczka diagnostyczna `StrefaZero-Windows-x64-I04_5-G.zip` ma własne sumy SHA256 i kontrolę CRC. Nie zmienia wersji gry ani opublikowanego wydania Q. Własne kontrole paczki są w [package-checks](gpu-i04-5g/package-checks.json); skrypty pomiaru i analizy znajdują się również w jej katalogu tools.

## Decyzja

**Badanie rozmiarów grup G1 zakończono; domyślne PBF 256 pozostaje bez zmian.** Dodano niezależny parametr i kontrole jego poprawności, lecz nie wykazano powtarzalnego przyspieszenia uzasadniającego zmianę domyślną.

W końcowej serii misja ma 2,830064 ms dla G 256 i 2,824128 ms dla G 64 (około 0,21% różnicy median agregatów; mediana zmian w odpowiadających powtórzeniach −0,17%). Dla G 128 jest 2,864128 ms, mediana zmian względem G 256 +1,83%. 16k/32k nie poprawiają się spójnie po zmniejszeniu PBF. Próba wstępna z samymi podsumowaniami sugerowała około 1% dla 64; nie przyjmujemy jej za stabilny wynik po pełnej powtórce. Porównanie z Q samo nie izoluje rozmiaru PBF — temu służy G 256.

CPU submit misji: mediana median G 256 0,01065 ms, mediana p95 0,05755 ms; dla 64 odpowiednio 0,00805/0,06000 ms. Nie przypisujemy tej zmienności przyspieszeniu shaderów. Liczniki pierwszego powtórzenia każdej konfiguracji w misji: mediana 140 dispatchy i barier, maksimum 142, 2 budowy indeksu i 26 wywołań redukcji na tick. Pamięć logiczna buforów urządzenia Q 8 821 464 B, G 8 821 476 B; to +12 B, nie pomiar fizycznej rezydencji VRAM. HUD w nieograniczonym benchmarku, z poll co 16 ticków, ma medianę wieku 20 i maksimum 28 ticków w tych próbach. Nie jest bieżącym stanem używanym do rozwiązywania kontaktów; kontrakt krytycznych zdarzeń nadal należy do I06.

Cel GPU **1,50 ms pozostaje otwarty**, podobnie przyczyna historycznej zmienności między sesjami. Osiem bieżących okien przeszło z pełnym fokusem i bez utraty czasu; nie dowodzi to usunięcia dawnych spowolnień. Full/direct, opcje C i sondy OFF pozostają domyślne; lokalny Dmax jest odroczony.

Następna odrębna próba **G2**: wskazówki braku aliasowania tylko dla udowodnionych, rozdzielonych banków Jacobi w PBF. Obecne wywołania `liquid_dispatch` wiążą różne banki; nie wolno przenieść wskazówek na in-place spawn/blast/debris. Najpierw jawny kontrakt wiązań i kontrola usterki, potem Q oraz A/B z sondami OFF. To hipoteza bez obietnicy przyspieszenia. Porządkowanie wspólnego eksportera i jawne błędy zapisu pozostają częścią konsolidacji harnessów w I05; przy reprodukcji starych EXE zachować przygotowanie katalogów. I05 nie zostało rozpoczęte i czeka na ocenę całego I04.5.

