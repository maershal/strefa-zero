# I06 — jedna sesja reguł CPU i niezawodny kontrakt GPU

Data: 2026-10-30. Wersja źródeł: 0.7.0, iteracja I06. Implementacja funkcjonalna; końcowa regresja zakończona. Cel wydajności I06 nieodebrany.

## Własność

`game::GameplaySession` nie zawiera `sim::World`, modelu MAC ani kopii kolizji. CPU jest jedynym właścicielem amunicji, zasobu granatów, rezerwacji, cooldownów, przeładowania, HP tarcz, wyniku i ukończenia misji. `ParticleWorld` posiada sesję i używa jej w tej samej ścieżce gry, narzędzi i diagnostyki. Stary `GameSession` pozostaje wyłącznie referencją CPU. `mission.comp` nie nalicza amunicji, HP ani wyniku; dostaje ich stan prezentacyjny.

Fizyczne pociski, granaty, podparcie, kapsuła i destrukcja pozostają na GPU. Początek pocisku wyznacza aktualna lufa, z kontrolą ściany między barkiem i lufą. CPU otrzymuje fizyczne trafienia oraz stan wejścia do wyjścia, a wynik nalicza raz. Zniszczona tarcza zachowuje kształt kolizyjny jako wrak; geometria trafienia nie zależy od chwili odczytu HP na CPU. HP i wynik są dostępne w prezentacyjnym `MissionSnapshot` oraz bezpośrednio w `gameplay().state()`.

## ABI i tożsamość

`WorldCommand` i `WorldEvent` mają po **64 B, align 16**, z nagłówkiem 16 B, danymi całkowitymi 16 B i dwoma payloadami po 16 B. Pełne payloady, ID komendy źródłowej i ID podmiotu nie mieściłyby się w sugerowanych 32 B. Offsety C++ są sprawdzane statycznie; diagnostyka GPU odpytuje rzeczywiste offsety std430 sterownika.

Komenda zawiera generację, docelowy tick, rosnące ID, rodzaj, podmiot, flagi i payload. Zdarzenie zawiera generację, wykonany tick, lokalny numer zdarzenia, rodzaj, ID komendy źródłowej, podmiot, wartość i payload. Tożsamość zdarzenia to **(generacja, tick, numer)**. Numer jest kolejnością w pakiecie, a nie liczbą float. Identyfikatory zapisane w wolnym kanale projektu pocisku są przenoszone bitowo przez `uintBitsToFloat`/`floatBitsToUint`, bez konwersji numerycznej.

Sesja przyjmuje kompletne pakiety kolejno, odrzuca starą generację i deduplikuje powtórzony tick. Przed skutkami sprawdza wszystkie nagłówki i numery zdarzeń. Luka lub przepełnienie pakietu jest błędem, nie częściowym sukcesem. GPU odrzuca ponowne ID komendy. Liczniki ticków i komend zatrzymują się przed zawinięciem; wymagany jest jawny reset generacji.

## Kolejki, publikacja i przeciążenie

`CommandRing` jest dziennikiem **128** oczekujących rezerwacji. Pakiet ticka mieści do **3** nowych komend: karabin, granat i skok. Kontrolki ciągłe oraz prezentacja reguł dotyczą tego samego numerowanego ticka. Publikacja korzysta z istniejącego transportu upload i kolejności kontekstu GL. Bariera SSBO poprzedza kolejne przebiegi fizyki.

`EventRing` ma **32 sloty × 144 zdarzenia**, czyli 4608 miejsc zdarzeń. Jeden pakiet ma 9232 B; staging zajmuje 295424 B, oprócz bufora producenta. Miejsce rezerwuje się przed przygotowaniem komend i krokiem. Każdy tick wykonuje kopię kompletnego pakietu do własnego slotu i zakłada fence; kolejne zapisy producenta są uporządkowane barierą. CPU pyta fence z timeoutem 0. Mapowanie trwałe wymaga obsługi sterownika; GL 4.3 używa bufora staging i odczytu dopiero po gotowym fence. Slot wraca do użycia po konsumpcji całego pakietu. Odczyty HUD nie uczestniczą w potwierdzaniu.

Limit wymiarujemy według maksymalnych emisji w ticku, nie liczby ticków: 128 integracji pocisków, do 3 nowych komend i potwierdzeń, zastąpienie/wykonanie starego skoku, kontakt lufy, detonacja, 3 trafienia wybuchu, agregat destrukcji i obserwacja wyjścia. Konserwatywna granica jest poniżej 144. Opóźnienie do 32 zleconych ticków to około 266,7 ms czasu symulacji; po wyczerpaniu slotów CPU nie wysyła dalszego ticka. Rezerwacje komend: najwyżej 3 × 32 oraz jeden starszy skok, z zapasem do 128.

Backpressure nie zużywa wejścia, amunicji ani numeru ticka. `SimulationWorld` oddaje niezlecone kroki do zegara i zachowuje dług czasu. Żywa pętla okna nie czeka na fence. Jawne odtwarzanie offline może oczekiwać na pojemność kolejki; eksporty i synchroniczne kontrole są poza ścieżką gry. Reset rozlicza rezerwacje przez utworzenie nowego stanu i unieważnia wszystkie stare generacje. Statystyki podają zlecenia, potwierdzenia, stare pakiety, zajętość, maksymalny wiek i backpressure.

## Reguły i wejście

- Karabin: 13 ticków przerwy (108,3 ms), magazynek 30, reload 181 ticków (1508,3 ms). To zachowanie efektywne I05: odejmowanie float od nominalnych 0,1/1,5 s w GLSL dawało 13/181 kroków. Migracja zachowuje tempo, a terminy są teraz liczbami całkowitymi.
- Granat: 30 ticków przerwy (250 ms), zapas 3. Nie da się zużyć kilku granatów przez powtarzanie klatek UI.
- Rezerwacja następuje przed publikacją. GPU potwierdza wykonanie albo odrzuca je przy pełnym magazynie pocisków/granatów. Odrzucenie zwraca zasób dokładnie raz. Cooldown liczymy od próby zlecenia; odrzucenie nie skraca odstępu.
- Skok: coyote time 10 ticków (83,3 ms), jump buffer 12 ticków (100 ms). GPU sprawdza aktualne kontakty terenu i materiałów oraz czas od ostatniego podparcia. Zużycie jest jednokrotne. Nowe żądanie zastępuje stare z jawnym odrzuceniem. Utrata fokusu, pauza i zmiana widoku kasują oczekujący skok przy kolejnym kroku. Zlecony podczas pauzy pojedynczy krok oczekuje na wolny slot.
- `InputBuffer` zachowuje krótkie naciśnięcie strzału, skoku, reloadu i granatu, także gdy klatka nie wykonuje ticka. Akcje jednorazowe nie są powielane przy kilku krokach w klatce.
- Aktywne okno gry pozwala celować poza areną, z clampem celu do granic świata. Plecak i granat nie zależą od samego hover areny. Aktywne kontrolki UI zachowują przechwycenie wejścia.

## Poza i prezentacja

`soldier_pose.glsl` zawiera wspólne równania `SoldierPose`, IK dwóch kości, barku, chwytów i lufy; ten sam plik jest kompilowany jako C++ i GLSL. Renderer używa pozy interpolowanej, a fizyczny strzał pozy aktualnej GPU. Przeniesienie wzorów nie przenosi kolizji na CPU.

## Walidacja i pomiary

Testy dedykowane GPU obejmują także rzeczywisty fallback staging GL 4.3, odrzucenie ponownego ID komendy, 128 jednoczesnych trafień, wygaśnięcie jump buffera, skok na zboczu i ruchomym piasku. Wyniki końcowe regresji są poniżej. Wcześniejsze nieudane próby pozostają w surowych logach. Cel I06 p95 pełnego CPU submit <0,10 ms/tick jest oddzielny od poprawności i od nierozwiązanego celu I04.5.

## Zaległość kanału zdarzeń

Przy celowo opóźnionej obsłudze co trzy klatki maksymalna zarejestrowana liczba niepotwierdzonych ticków wyniosła:

| FPS | Maks. pakiety/ticki | Odpowiednik czasu symulacji |
| --- | ---: | ---: |
| 30 | 12 | 100.0 ms |
| 60 | 6 | 50.0 ms |
| 144 | 3 | 25.0 ms |

To wiek względem zleconych ticków, nie pomiar opóźnienia wejścia zegarem ściennym. Osobny test wypełnił wszystkie 32 sloty; dalsze zlecenie zatrzymało się bez zmiany ticka i amunicji, a po odbiorze wszystkie potwierdzenia były kompletne. [Dane](../measurements/i06/latency-summary.json).

Środowisko walidacji: Intel Core i5-9300HF 2,40 GHz (4 rdzenie / 8 wątków), GTX 1650, NVIDIA 610.47, GL 4.3; MSVC 14.51.36231 i GCC 13.3.0, Release, warnings-as-errors. [Identyfikacja środowiska i EXE](../measurements/i06/environment.json).

## Wyniki końcowe

Windows Release / MSVC z ostrzeżeniami jako błędami: **55/56 PASS**. Niezaliczony `gpu.execution` ignorował wynik nowego API zlecania i liczył także odmowy przy pełnym EventRing jako wykonane ticki. Skorygowano wyłącznie scenariusz obciążenia timerów: odrzucony krok ponawia po jawnym diagnostycznym odbiorze. Kontrola nadal wymaga dokładnie 70 zleceń timerów, kompletności wyników i zawijania FIFO. **Końcowe kontrole 4/4 PASS**: `game.protocol`, `app.shared_session`, `gpu.gameplay`, `gpu.execution`. Nie powtarzano całej serii po tej zmianie scenariusza. Automatyczne porównanie SHA256 potwierdza, że od pełnej regresji zmienił się tylko `src/gpu/execution_validation.cpp`; kod produkcyjny i shadery są identyczne. Pełne okno misji zaliczyło limit 60 s. Zachowano oba EXE SHA256 i nieudaną próbę.

Końcowe kontrole GCC: **3/3 PASS** (protokół, zegar, poligon CPU), wcześniejsza pełna seria GCC **7/7 PASS**. Dedykowana walidacja GPU przeszła wszystkie kontrole ABI, rezerwacji/zwrotów, deduplikacji, przepełnienia, starej generacji, 8 jednoczesnych detonacji, 128 trafień, opóźnienia 3 klatek, fokusu, skoków na lądowaniu/zboczu/piasku i wygaśnięcia bufora.

Pełna trasa I06 i kontrola na opublikowanym I05 przeszły przy 30/60/144 FPS. Próbki wyniku były identyczne; największa odległość stóp w próbkach co 60 ticków względem I05 wyniosła odpowiednio 0,407 / 0,135 / 0,717 komórki. To próbki trasy, nie dowód identyczności całego stanu materiałów.

### Zachowana nieudana próba i korekta migracji

Pierwszą serię Windows przerwano po wykryciu niezaliczonej trasy w `gpu.mission` i `gpu.pbf_restrict_mission`: wynik misji 3/3 i ukończenie były poprawne, ale piasek nie został uwolniony. Nowy kod przyjmował 12/180 ticków zamiast efektywnych 13/181 starego odejmowania float. Przywrócono dotychczasowe tempo bez zmiany progów, skryptu trasy, solvera ani ustawień domyślnych. Kontrola I05, kontrola zgodności I06 oraz nowa pełna regresja są oddzielnymi próbami. Pierwsza seria i jej SHA256 pozostają w `i06/initial-regression/`.

### Pełny kontrakt CPU i koszt GPU

GTX 1650, NVIDIA 610.47, kontekst GL 4.3. Trzy przebiegi: misja 3096 cząstek, 120 ticków rozgrzewki + 240 próbek. `glFinish()` przed krokiem jest wyłącznie synchronizacją diagnostyczną, poza pomiarem CPU. Sam mierzony krok obejmuje konsumpcję poprzedniego pakietu zdarzeń, reguły, upload komend, zlecanie solvera i staging. Logika CPU obejmuje przygotowanie komend oraz konsumpcję zdarzeń; jest częścią pełnego czasu, nie składnikiem do dodawania. To pomiar kroku, nie całej klatki ani latencji okna.

| Próba | CPU mediana [ms] | CPU p95 | CPU p99 | Logika CPU p95 | GPU mediana | GPU p95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.58315 | 1.36800 | 3.01990 | 0.00070 | 2.98143 | 4.06086 |
| 2 | 0.56350 | 1.26380 | 2.41380 | 0.00070 | 2.97614 | 3.86832 |
| 3 | 0.57775 | 1.07250 | 2.33410 | 0.00070 | 2.94188 | 3.89779 |

Pierwsza seria nie spełniła celu p95 CPU <0,10 ms. Po pełnej regresji wykonano dodatkowo kontrolę **direct → staged → direct** na tym samym EXE, po trzy próby w każdym przypadku, z tą samą misją i metodą.

| Seria | Mediany CPU [ms] | Zakres p95 CPU [ms] |
| --- | ---: | ---: |
| direct-before | 0.02350–0.02640 | 0.02940–0.03440 |
| staged | 0.02710–0.03730 | 0.04300–0.05000 |
| direct-after | 0.02045–0.02785 | 0.03020–0.04170 |

**Końcowe serie spełniają p95 CPU <0,10 ms**, ale rozbieżność względem pierwszych trzech prób pozostaje otwarta. Nie przypisujemy tej zmiany transportowi, cache ani warunkom systemowym. Staged nie wykazał przewagi wymagającej zmiany ustawień; pozostaje domyślny direct. Odbiór stabilności wydajności oraz cel GPU I04.5 <1,5 ms pozostają otwarte. Nie zastępujemy wcześniejszych pomiarów nowszymi. [Pełne dane porównania transportów](../measurements/i06/transport-contract-summary.json).

Nowy stały koszt pamięci: 295424 B staging zdarzeń oraz 9504 B rozszerzenia bufora misji (transport 16 B, komendy 256 B, wynik 9232 B), plus dziennik 8192 B i metadane CPU. Miejsce przechowywania fizycznej alokacji wybiera sterownik. Liczba shaderów i przebiegów solvera pozostaje taka jak w I05.

[Pełna regresja](../measurements/i06/ctest.log), [końcowe 4/4](../measurements/i06/final-ctest.log), [zakres poprawki scenariusza](../measurements/i06/final-diagnostic-change.json), [GCC](../measurements/i06/gcc-compatibility-tests.log), [kontrole GPU](../measurements/i06/gameplay-final/gameplay-tests.txt), [720 próbek pełnej misji](../measurements/i06/gameplay-final/full-mission-contract.csv), [podsumowanie czasu](../measurements/i06/contract-summary.json), [kontrola zgodności z I05](../measurements/i06/i05-route-comparison.json), [identyfikacja buildu](../measurements/i06/final-validation.json).
