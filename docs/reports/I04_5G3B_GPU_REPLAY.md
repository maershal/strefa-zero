# I04.5-G3-B — odtworzenie wolnego reżimu i porównanie pracy sceny

Data: 2026-10-30. **Odtworzono przejście tej samej G1 z około 2,77 do 11,34 ms/tick po pełnym wcześniejszym obciążeniu QA + Q. Zebrano ślady Dmax/kandydatów w wolnym reżimie. Późniejsza kontrola wróciła do około 2,8 ms już przed pokazaniem okna. Nie ma nowej optymalizacji ani ustalonej przyczyny systemowej. Całe G3 i cel 1,5 ms pozostają otwarte.**

G1 EXE SHA-256 `1a1f53457ca590187417818f24d42bbea93d11c7b29bc5e7d8fea7320f918d5e`; użyta do wszystkich pomiarów kosztu. G2 `333287c4063fa722eb4314fd70c0f6509cc92479c7890ee1da772bf1b0443af8` i sześć testowych EXE CPU pozostają niezmienione. Nie przebudowano C++/GLSL i nie zmieniono sterownika, zasilania ani ustawień GPU. [Stan wejściowy](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/base-state.json), [kontrola binariów i źródeł](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/runtime-integrity.json).

## Jedno odtworzenie pełnego preludium

[Plan](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/protocol.json) zapisano przed pierwszą próbą: raz 52 testy oraz sześć dodatkowych Q w dawnym porządku, dwie kontrole G1 przed i OFF/OFF/bounds/OFF/snapshots/OFF po. Sygnał diagnostyczny: pierwsze dwie mediany misji po preludium >6 ms. To etykieta do przechwycenia, bez zmiany kryteriów poprawności. Cała zapisana sekwencja sond i kontroli jest wykonywana również przy braku sygnału; brak ponawiania dla korzystnego wyniku.

**52/52 PASS**, bez pominięć, suma czasów przypadków 445,42 s. Zachowano kolejność przypadków, flagi, roboczy katalog i limity z oryginalnego CTest. Profile/logi przekierowano do nowych katalogów, bez ponownego użycia starych wyników. Zewnętrzny supervisor ma limit 1500 s dla CTest i 180 s dla pojedynczej pozostałej próby. Dodatkowe Q: OFF/ON, ON/OFF przy domyślnym PBF256, następnie ON64 i ON128. [Lista i oryginalne argumenty](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/qa-schedule.json), [JUnit](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/ctest.xml), [podsumowanie QA](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/ctest-summary.json), [log CTest](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/session/full-qa/process-stdout.txt).

Łącznie cztery Q wewnątrz QA i sześć dodatkowych: **10/10 PASS, 900 checkpointów, 3600 porównań, 10/10 rzeczywistych wyłączeń sprzężenia odrzuconych**. Dawna diagnostyka niezależnych trajektorii przeszła w tych dziesięciu procesach; to wynik tej serii, bez zmiany progów i bez odwołania wcześniejszych niepowodzeń. [Wyniki Q](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/q-summary.json).

[Supervisor](../../scripts/profiling/profile-variance.ps1) obsługuje `replay` oraz osobne `visibility`; [analyzer](../../scripts/profiling/analyze-variance.py) weryfikuje także 52 przypadki, transkrypt CTest, kolejność i flagi Q. SMI/WMI i obserwacja Win32 mają własny koszt. WMI działa przez całe preludium; okna obserwujemy dla bezpośrednio uruchamianych aplikacji, nie dla dzieci procesu CTest. WMI nie przypisuje PID dzieci CTest do konkretnych przypadków; zachowano dane bez takiej interpretacji.

## G1 przed i po QA

GTX 1650, NVIDIA 610.47. Cztery stałe sceny, 120 ticków rozgrzewki i 240 pomiarowych, general/PBF/reduction 256, full/direct, reused/SoA, combined, elapsed, HUD auto/2, C OFF. Sondy tylko w jawnie nazwanych procesach. Osiem profili w serii głównej daje **7680 surowych wierszy CPU/liczników**. Każdy GPU percentyl dotyczy 240 próbek jednego procesu; indywidualnych czasów GPU stary EXE nie zapisuje.

Misja: GPU oraz CPU jako mediana / p95 w ms/tick. Ścienny czas to średnia partii, z drain/poll co 16 ticków; drain jest poza CPU submit. Nie sumujemy median faz CPU.

| Próba | GPU mediana / p95 | CPU submit mediana / p95 | Średnia ścienna |
| --- | ---: | ---: | ---: |
| pre-off-1 | 2.751008 / 2.809504 | 0.005000 / 0.014000 | 2.772023 |
| pre-off-2 | 2.788928 / 2.855680 | 0.004700 / 0.010100 | 2.815507 |
| post-off-1 | 11.287520 / 11.605600 | 0.023800 / 0.083200 | 11.371527 |
| post-off-2 | 11.421536 / 11.730304 | 1.594800 / 2.883900 | 11.509466 |
| post-bounds | 11.834944 / 12.123456 | 0.048000 / 0.199200 | 11.944882 |
| post-off-3 | 11.319200 / 11.591712 | 1.783800 / 3.260900 | 11.406950 |
| post-snapshots | 11.899456 / 12.372992 | 0.069500 / 0.204000 | 12.255865 |
| post-off-4 | 11.365312 / 11.784992 | 0.023500 / 0.158300 | 11.471773 |

Dwie początkowe kontrole: **2,751008 i 2,788928 ms**. Cztery OFF po preludium: **11,287520–11,421536 ms**, mediana median **11,342256 ms**. Wszystkie przekraczają 6 ms; [etykieta przechwycenia](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/session/capture-signal.json) jest prawdziwa. Wzrost potwierdza czas ścienny, a nie tylko zapytania GPU. CPU w dwóch wolnych kontrolach ma około **0,024 ms**, w dwóch pozostałych **1,59–1,78 ms**. Wysoki czas głównego wątku CPU nie jest zatem koniecznym objawem tego wolnego reżimu; nie wyklucza to pracy sterownika ani oczekiwań na innych wątkach.

Misja nadal ma medianę **140 dispatchy i barier**, maksimum 142, **2 budowy indeksu, 26 redukcji postaci**, wiek HUD 20/max28 i **8821476 B** urządzenia. Stan jest skończony, 3096 cząstek pozostaje 3096; blast kończy z 3114. To nie dowód pełnej identyczności stanów. [Surowe wyniki i wszystkie p99/maksima](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/session/analysis.csv), [liczniki/fazy CPU](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/session/counter-analysis.csv).

## Praca indeksu w rzeczywiście wolnych próbach

Bounds i snapshots zostały otoczone kontrolami OFF, które wszystkie pozostały wolne. Ich czasy zawierają narzut sond: misja **11,834944 / 11,899456 ms**, wobec OFF około 11,3 ms. **193984 wpisy Dmax**, **96 przechwyceń stanu na potrzeby rekonstrukcji**, po dwa promienie, **192 rekonstrukcje bez pominiętych par**. Histogramy mają pełną liczebność. Duże sceny sprawdza świeża siatka CPU; mały adversarial all-pairs jest odrębnym przypadkiem QA.

W obydwu pełnych śladach maksimum Dmax wynosi **0,0625 / 0,25 / 0,25 / 0,4375** dla mission/16k/32k/blast. Żaden wpis nie przekracza 1. Dla promienia 2 maksimum stosunku globalnego okna do świeżej siatki to **1,04302 / 1,02687 / 1,03069 / 1,35498**. [Ślady](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/session/bounds-analysis.csv), [rekonstrukcje](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/session/snapshot-analysis.csv), [podsumowanie](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/candidate-summary.json).

Porównano odpowiadające sobie etykiety/ticki/ordinal/promienie ze starszą szybką G1 z G3-A, po 48 wierszy na scenę. Poniżej stosunek liczby globalnych kandydatów wolna/szybka:

| Scena | Minimum / mediana / maksimum | Identyczne bajty histogramu |
| --- | ---: | --- |
| mission | 0.992669 / 0.999151 / 1.001853 | NIE |
| stress_16k | 1.000000 / 1.000000 / 1.000000 | TAK |
| stress_32k | 1.000000 / 1.000000 / 1.000000 | TAK |
| mission_blast | 0.928508 / 1.000395 / 1.138522 | NIE |

Dla 16k i 32k **zarówno CSV rekonstrukcji, jak i pełne badane histogramy są bajtowo identyczne** przy około 2,7–2,8× wzroście median GPU w kontrolach OFF przed/po QA. W misji liczba kandydatów w przechwyceniach zmienia się najwyżej o około 0,2% w górę, przy około 4,1× czasie GPU; blast ma większą zmienność. Nie ma tu śladu eksplozji Dmax/liczby kandydatów wyjaśniającej wielokrotny wzrost czasu. [Porównanie i hashe danych wejściowych](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/candidate-state-comparison.json).

Granice dowodu: przechwycenia obejmują wybrane fazy/ticki, a Dmax wszystkie rejestrowane zapytania indeksowe. Stary G1 nie utrwala pełnych surowych buforów cząstek/indeksu na dysku. Równość histogramu nie oznacza bitowej równości wszystkich cząstek. Stan z osobnej próby sondowanej nie jest stanem sąsiedniego OFF. Nie zamykamy wszystkich hipotez o pracy fizyki, ale obecne dane nie uzasadniają lokalnego Dmax jako naprawy tego skoku.

## Powrót szybkiego reżimu i okno

Po zakończeniu zapisanej serii przygotowano osobną, ograniczoną próbę: **3 × H/V/V/H**, potem prawdziwe widoczne okno misji i końcowe H. [Plan follow-up](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/visibility-protocol.json), [procesy](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/visibility/runs.csv). Między końcem rekordu ostatniej wolnej próby i startem pierwszej kolejnej minęło **142,69 s**; był to również nowy proces PowerShell. **Pierwsze H było już szybkie: 2,762752 ms, przed jakąkolwiek widoczną próbą.** [Odstęp i nierozdzielone czynniki](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/transition-gap.json).

Nie przypisujemy więc powrotu pokazaniu okna, samemu upływowi czasu ani nowemu supervisorowi. Nie było pomiaru ciągłego między seriami. W follow-up siedem ukrytych kontroli dało **2,762752–2,885632 ms**, sześć widocznych **2,807552–2,855776 ms**. Próby widoczne cost nie prowadzą regularnej pętli present/zdarzeń i nie są testem FPS.

Wartości poniżej to mediana median / mediana p95 GPU; osobne kolumny pozostają oddzielnymi warunkami, bez łączenia próbek:

| Scena | Przed QA, 2 OFF | Po QA, 4 OFF | Follow-up H, 7 OFF | Follow-up V, 6 OFF |
| --- | ---: | ---: | ---: | ---: |
| mission | 2.769968 / 2.832592 | 11.342256 / 11.667952 | 2.800800 / 2.863104 | 2.829296 / 2.886656 |
| stress_16k | 1.767408 / 1.844384 | 4.777680 / 5.285280 | 1.772160 / 1.848224 | 1.764192 / 1.849920 |
| stress_32k | 3.114016 / 3.240144 | 8.758400 / 9.422992 | 3.147776 / 3.280928 | 3.142336 / 3.267440 |
| mission_blast | 2.923424 / 3.056016 | 12.231088 / 12.792528 | 2.967872 / 3.088384 | 2.997312 / 3.140752 |

W widocznych próbach obserwator potwierdził okno, lecz **nie potwierdził fokusu**; cały follow-up pozostał w tle. Priorytet był Normal, zasilanie AC=1, brak minimalizacji w zebranych próbkach. To nie porównanie foreground/background. [Stany i koszt obserwatora](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/host-summary.json), [agregaty](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/all-aggregates.json).

Funkcjonalny smoke widocznej misji przeszedł: **900 klatek, 1798 ticków, wynik 3, poprawna pauza, zero odrzuconego czasu**. Mediana/p95/p99/max całej klatki **16,6689 / 16,6696 / 16,7317 / 17,1898 ms**, VSync OFF, limit 60 FPS. Po pominięciu 30 pierwszych klatek fokus **0/870**, minimalizacja **0/870**. Nie zaliczamy tego do bramki porównywalnych prób z fokusem. KHR_debug id=131186: runtime 0, walidacja 1, zgubione komunikaty 0. [Wynik okna](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/visibility/window-analysis.json).

## Telemetria i dalsza hipoteza

SMI w pierwszym szybkim procesie ma zaledwie dwie próbki z utilization ≥90%: zegar 1935 MHz, moc 37–38 W. W wolnych procesach takich próbek jest 19–20 na proces; mediany zegara 1395 MHz i mocy około 20,3–20,5 W, temperatura około 63–64°C, pamięć około 1043 MiB z 4096 MiB. Zakresy obejmują różne sceny, rozgrzewkę i przejścia. Nie są zsynchronizowane z konkretnym dispatch. To **korelacja odmiennego stanu wykonania**, nie rozpoznany power cap, throttling, rezydencja ani dowód błędu sterownika. Powodów ograniczenia zegarów, masek power/QoS i dokładnych oczekiwań GL nie zapisano. [Wszystkie zakresy i liczebności próbek](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/nvidia-summary.json). Zasady zapytań oraz zależność dostępności pól od urządzenia opisuje [dokumentacja NVIDIA SMI](https://docs.nvidia.com/deploy/nvidia-smi/index.html).

Windows GPU engine worker: **794 odczyty / 743 niezerowe wiersze** w serii głównej i **93 / 98** w follow-up. Wyniki są asynchroniczne; obejmują również preludium. PID dzieci CTest nie mapowano, instancje mogą utrzymywać dane zakończonych procesów, więc `other_pid` nie jest dowodem równoległego obcego obciążenia. Nie sumujemy procentów między silnikami/LUID/PID. [Główne grupowanie](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/session/gpu-engine-analysis.csv), [surowe dane](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/session/gpu-engines.csv). Odkrywanie narzędzi na PATH znalazło WPR, nie znalazło czterech pozostałych sprawdzanych profilerów; nie instalowano ani nie uruchamiano zewnętrznego śladu GPU. [Zakres sprawdzenia](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/profiler-availability.json).

## Weryfikacja i decyzja

- Główny supervisor **15/15**, follow-up **14/14**, pełne **52/52 QA**, dodatkowe **6/6 Q**. Łącznie **21 profili kosztu**, **20160 surowych wierszy CPU/liczników** profili, plus osobne dane QA i smoke.
- **9/9 nowych negatywnych kontroli analyzera**: m.in. niepełny plan, inny CTest, zmienione Q/timeout, brak ticka, sonda w OFF, pominięty sąsiad i pominięte QA. Trzy syntetyczne kontrole progu nie są pomiarami wydajności. [Kontrole](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/analyzer-negative-checks.json).
- Końcowy publiczny replay odrzuca zużyty plan, nieseryjne polecenie i istniejące katalogi QA przed uruchomieniem procesu: **3/3**. [Kontrole jednorazowości](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/replay-guard-checks.json). Składnia PowerShell 5.1 przechodzi; sprzątanie własnego drzewa sprawdzono na dwóch wyłącznie syntetycznych procesach, oba zakończone. [Składnia](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/script-syntax-final.json), [sprzątanie](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/owned-tree-cleanup.json).
- Zachowano narzędzia użyte do obu serii; końcowy skrypt dodaje kontrole jednorazowości po pomiarach. [Wersje i hashe](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/tool-source-versions.json). Wyniki mają [własny manifest](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/SHA256SUMS.txt); poprzednie **9524 pliki historyczne i dziewięć zestawów sum paczek bez zmian**, [integralność](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5g3b/historical-integrity.json).

Pakiet `outputs/StrefaZero-Diagnostics-I04_5-G3B.zip` zawiera narzędzia, raport i dane, bez nowego runtime. Zapisane plany replay opisują wykonany pomiar i są jednorazowe. Powtórka wymaga przygotowania nowej listy CTest/profile paths/JUnit na tych samych binariach, z zachowaniem flag i limitów; sam nowy katalog `Output` nie zastępuje nowych ścieżek w planie. Publiczne controls/reproduction/visibility przyjmują świeże Output jak wcześniej.

**Decyzja:** G3-B zakończone jako odtworzenie i porównanie pracy, całe G3 nadal otwarte. Następne G3-C rozdziela **czas bezczynności i nowy proces nadrzędny**, rejestrując od początku powody ograniczenia zegarów oraz dostępny stan power/QoS/priorytetu rodzica i dziecka. Po wykryciu wolnego reżimu: kontrole w tym samym supervisorze przed/po ustalonej przerwie, kontrola ze świeżym supervisorem i powrót do poprzedniego; osobny passes/OFF pomoże rozdzielić koszt przebiegów od zlecania. Plan i budżet ustalić wcześniej, bez zmiany ustawień systemu dla wyniku. Brak dowodu przyczyny pozostaje jawny. Nie wdrażamy lokalnego Dmax ani kolejnego wariantu PBF na podstawie tej zmienności. Cel 1,5 ms i odbiór całego I04.5 pozostają otwarte; I05 nie rozpoczęto.
