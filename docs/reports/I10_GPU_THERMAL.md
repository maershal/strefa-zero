# I10 — termika, gazy i reakcje we wspólnym świecie GPU

Data: 2026-10-31. Punkt wyjścia: zachowany roboczy stan I09, łącznie z wcześniejszymi niezacommitowanymi zmianami. Podetapy A–D mają osobne kontrole i pomiary. Domyślne ustawienia gry pozostają `legacy/basic/low`, a nowa termika GPU jest **osobno włączana**. Wyniki i odbiór budżetu znajdują się poniżej; obecność efektów nie oznacza odbioru wydajności.

## A — stan materiału i energia

Źródłem identyfikatorów, gęstości, ciepła właściwego, przewodności, zapłonu, szybkości spalania i pozostałości jest `src/sim/material.hpp`. Tablice GLSL są generowane z tych danych przy kompilacji programu. Nie utrzymujemy drugiej ręcznej tabeli parametrów. Jednostki: znormalizowana masa/objętość, K oraz kJ na jednostkę masy. Przewodnictwo i geometria mają skalę gry.

Dotychczasowy `Grain` i protokół gry zachowują ABI 64 B. Osobny `ThermalParticle` 64 B na stabilne ID zawiera masę, H, pozostałe paliwo, rzeczywisty materiał i dane wymiany. Dwa bufory pozwalają czytać wspólny stan wejściowy i zapisywać wynik bez wyścigów. Masa początkowego nośnika pochodzi z autorytatywnej odwrotnej masy solvera. Przypisanie rodzaju fizycznego nie zastępuje identyfikatora termicznego.

`ThermalCell` ma krok 128 B/std430. Przechowuje materiał nieruchomy, sześć mas gazowych, entalpię, prędkości MAC, ciśnienie projekcji, żądania wymiany i skumulowany bilans źródeł. Pole 320 × 192 jest niezależne od renderera I08, interpolacji i pól jakości cieczy I07. Wszystkie przebiegi mieszczą się w SSBO 0–7; moduł wiąże własne bufory w osobnych przebiegach i stosuje jawne bariery.

Temperatura wynika z H/m. Rodziny lód–woda–para, kamień–lawa i metal–stopiony metal mają plateau temperatury i jawne ciepło utajone. Woda: 273,15 K / 334 oraz 373,15 K / 2256. Lawa: 1400 K / 400. Metal: 1811 K / 270, zgodnie z wybranym modelem metalu w istniejącej tabeli CPU; nie są to uniwersalne parametry dowolnego stopu.

Wymiana cząstka–gaz i cząstka–teren zbiera żądania do właściciela komórki, ogranicza łączny pobór do dostępnej energii i stosuje tę samą dawkę ze znakami przeciwnymi. Wspólny donor zachowuje margines 0,5% przy wyczerpaniu budżetu: pokrywa błąd dwóch dodatnich redukcji do 32768 składników float. Energia pozostaje u donora; margines nie usuwa energii.

Cząstki wymieniają też ciepło przez nakładające się pięciokomórkowe sąsiedztwa kontaktu, z temperaturą ważoną przewodnością. Uczestnicy i normalizacja są identyczni po obu stronach. Współczynnik ogranicza pobór energii i zmianę temperatury; ściana odcina takie sąsiedztwo. Teren–teren, teren–gaz oraz gaz–gaz korzystają z symetrycznych strumieni ze wspólnego stanu wejściowego.

Wybrano zbieranie wkładów zamiast nieograniczonej atomowej sumy `int32` ze skalą 2^10. Dwa ograniczone wkłady 32767 na każdy z 32768 nośników jeszcze mieszczą się w int32; dowolne gorące nośniki już nie. Samo obcięcie lub kwantyzacja wymagałyby dodatkowej polityki poboru i błędu. Aktualna ścieżka nie potrzebuje atomowych operacji float ani int64. Atomowa lista ID zawiera najwyżej 32768 elementów; test skupia całą pulę w jednej komórce.

## B — gaz i topologia

Bazą jest MAC 320 × 192 z prawą i dolną prędkością każdej komórki. Zamknięte granice świata mają zerowy strumień. Krawędź wewnętrzna jest zamknięta, gdy **którakolwiek strona** jest ścianą. Projektowanie prędkości używa 32 iteracji Jacobiego, z osobnym polem ciśnienia i pomiarem dywergencji przed/po przepływie. Jest to tłumiony przepływ napędzany wypornością; nie jest solverem akustycznym ani pełnym modelem gazu ściśliwego.

Adwekcja jest zachowawcza i używa wspólnego strumienia krawędzi dla obu komórek. Limit wypływu wynosi 45% zawartości komórki na tick. Osobne strumienie dyfuzji przenoszą masę każdego składnika wraz z jego entalpią. Nie normalizujemy po kroku całkowitej masy ani nie kasujemy śladowych gazów przez licznik czasu. Ciśnienie w narzędziach jest ciśnieniem projekcji w jednostkach modelu, nie pomiarem Pa.

Składniki: gaz obojętny, tlen, para, dym, paliwo gazowe i zbiorcze produkty. Początkowe powietrze ma 0,015 jednostki nośnika i 0,005 tlenu na otwartą komórkę, jak referencja CPU. Gaz zamknięty nową ścianą edytora jest zachowany w węźle bez przepływu; nie znika z bilansu. Otwarcie komórki ponownie udostępnia jej zawartość.

Redukcji do 160 × 96 nie włączono. Niezależna próba grafu pokazuje, że OR zamyka jednopikselowe drzwi, a AND usuwa jednopikselową ścianę. Bez bogatszej reprezentacji topologii sam jeden bit na grubszą komórkę nie spełnia kontraktu. [Dowód i dane](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/topology-160.json). Upakowany piasek i ciecz nie uszczelniają automatycznie krawędzi MAC; pełne sprzężenie gaz–ciecz i porowatość wymagają osobnego modelu.

## C — reakcje, fazy i bilans

W tej wersji aktywne reakcje obejmują **olej, drewno i paliwo gazowe**. Paliwo potrzebuje temperatury zapłonu oraz dostępnego tlenu. Dla jednostki masy przechodzącej do gazu reakcja pobiera dwie jednostki tlenu i tworzy trzy jednostki produktów: 10% masy paliwa trafia do dymu, pozostała część wraz z pobranym tlenem do produktów zbiorczych. Jest to jawny model masowy gry, nie szczegółowa kinetyka molekularna.

Ubytek pierwotnego paliwa, pozostająca masa popiołu, zabrany tlen i przeniesione H są rozliczane razem. Uwolnione ciepło zmniejsza pozostałą energię chemiczną o dokładnie tę samą wartość. Nie powstaje nowe paliwo z normalizacji pól. Gaśnięcie wynika z ochłodzenia lub ograniczenia dostępu do tlenu, a nie ze stanu animacji.

Kontakt ciekłej wody z paliwem zmniejsza dostęp tlenu na powierzchni. Pokrycie jest obliczane z bieżącej masy nośników, udziału fazy ciekłej i odległości. Nie jest trwałym znacznikiem „mokry” ani maską blokującą przepływ gazu. Lód i para nie są automatycznie liczone jako taka sama ciekła osłona. Jednocześnie wymiana ciepła rzeczywiście ochładza paliwo, a parowanie pochłania ciepło utajone.

Wrzenie przenosi odpowiednią masę i H nośnika do pary. Skraplanie oddaje ciepło utajone do gazu i tworzy wodę w komórce. Po zgromadzeniu co najmniej 0,001 jednostki wody i uzyskaniu wolnego ID kondensat wraca do fizycznej puli. Do tego czasu pozostaje w bilansie komórki; brak miejsca nie usuwa masy. Częściowe zamarzanie/topnienie zachowuje H i masę. Mechaniczna faza nośnika jest przybliżona przez istniejącą klasę ziaren albo cieczy, z niezmienną masą; nie wprowadzamy nowego solvera wielofazowego.

Przy rozbiciu terenu nowy fizyczny gruz przejmuje masę, H i paliwo z pierwotnej komórki. Punkt początkowy jest zachowany w dotychczasowym polu poprzedniej pozycji nośnika; nie zakładamy, że po pierwszym kroku ruchu nadal leży on w tej samej komórce. Rezerwacja ma jednego zwycięzcę. Ta konwersja nie tworzy zewnętrznego źródła masy.

Popiół po cząstkach pozostaje w ich ID. Popiół po nieruchomym drewnie jest zachowany w komórce; jego osobny ruch ziarnisty nie jest deklarowany. Wypalone drewno usuwa rzeczywistą przeszkodę świata, a pole terenu jest przebudowywane przed dalszym użyciem. Zmiana maski nie opiera się na samym obrazie płomienia.

Bilans ma postać:

`Δmasa = masa źródeł − masa usunięta`

`Δ(H + energia chemiczna) = zewnętrzne H + zewnętrzna energia chemiczna`

Osobno raportujemy uwolnione ciepło reakcji. To zamiana energii chemicznej, więc nie dodajemy go drugi raz do prawej strony. Edycje, narodziny/usunięcie materiału przez dotychczasowy interfejs świata, grzanie, chłodzenie i wstrzyknięcia gazu są jawnymi źródłami/ujściami. Energia kinetyczna solvera PBD/PBF nie jest częścią tego bilansu termicznego.

Opcjonalne promieniowanie do otoczenia jest wyłączone w zamkniętych testach bez ujść. Używa grywalnego współczynnika 10^-11 dla różnicy T^4 względem otoczenia 293,15 K, z limitem dostępnej energii. Wartość `radiated` jest raportowana osobno, a ten sam ubytek H trafia do jawnego bilansu zewnętrznego. Nie deklarujemy laboratoryjnego modelu wymiany radiacyjnej ani oświetlenia I11.

Potwierdzone eksplozje i trafienia gry mogą dostarczać ciepło. Osobny, tylko odczytujący konsument sprawdza generację/tick/ID, odrzuca duplikaty i nie potwierdza zdarzeń w imieniu `GameplaySession`. Osłona odcina depozycję po całym odcinku; ogrzewa się sama powierzchnia osłony. Dawka cieplna jest parametrem modelu gry, a rzeczywiście dodana suma trafia do bilansu. Reakcje chemiczne prochu, termitu i nitrogliceryny nie zostały przez to przeniesione.

## D — narzędzia, obraz i zegar

Misja i laboratorium korzystają z tego samego `ParticleWorld` i stanu termiki. Cząstki, przeszkody i gaz są aktualizowane wyłącznie po wykonanym ticku 120 Hz. Render niczego nie dopala ani nie transportuje. Pauza, utrata fokusu i reset korzystają ze wspólnego schedulera. Krótkie polecenie grzania jest zachowywane do najbliższego wykonanego ticka; backpressure nie zużywa go przed wykonaniem kroku.

Narzędzia: grzanie/chłodzenie w kJ/(s·komórkę), dodawanie gazu w jednostkach masy/(s·komórkę), drewno, lód i lawa. Dawka mnożona jest przez dt ticka, a generacja, tick i ID źródła są numerowane. Powtórzony lub stary tick nie ponawia dawki. Wspólny wybór obrazu obejmuje temperaturę, ciśnienie projekcji, prędkość gazu, tlen i paliwo. Inspektor pobiera asynchronicznie 128 B wskazanej komórki i pokazuje współrzędne oraz tick próbki.

Statystyki to asynchroniczna redukcja mas, H, energii chemicznej, źródeł, tlenu, paliwa, pary, dymu i dywergencji. Staging jest odczytywany po gotowości fence, bez oczekiwania w pętli gry. Zmiana generacji unieważnia stare wyniki. Zwykła telemetria nie zakłada nowych zapytań czasu GPU. Timery A/B/C są uruchamiane w jawnym profilu; całkowity koszt mierzy zewnętrzna para timestampów obejmująca również przebudowy fizycznych list i powrót kondensatu.

Płomień wynika z bieżącego uwalniania energii, gorący gaz z temperatury produktów, a dym/para z ich zachowanych mas. Wizualna absorpcja jest stylizowana dla czytelności komórki. Warstwa gazu jest składana po optyce wody, a cele i HUD po niej. Nie dodano HDR, bloom ani świateł I11.

Nagrania v1/v2 nadal wykonują dawny solver bez nowej termiki. Nagrania termiczne v3 zawierają rewizję I10, ustawienia, reset sceny oraz polecenia źródeł i kolejne ticki. Zmiana trybu i promieniowania jest zapisywana. Wspólny replay odtwarza te same dawki energii niezależnie od FPS. Gracz nadal jest niewrażliwy; oddziaływanie cieplne na HP nie jest częścią tej iteracji.

## Matryca funkcji CPU → GPU

| Funkcja | Stan I10 GPU | Zakres / ograniczenie |
| --- | --- | --- |
| Olej, drewno, ogień | Wdrożone | Zapłon, tlen, paliwo, masa produktów, ciepło, gaszenie |
| Gaz palny | Wdrożone | Jawna masa, Cp, tlen i zbiorcze produkty |
| Dym / para | Wdrożone | Zachowawczy transport i obraz; skraplanie/wrzenie |
| Woda / lód | Wdrożone termicznie | Plateau fazy, masa i H; przybliżona mechaniczna klasa nośnika |
| Kamień / lawa | Wdrożone termicznie | Dane i plateau CPU; uproszczona mechanika ciekłej lawy |
| Metal / stopiony metal | Wdrożone termicznie | Wybrana tabela CPU, H i faza; uproszczona klasa cieczy |
| Popiół | Częściowe | Masa zachowana; popiół terenu pozostaje nieruchomy |
| Kwas / korozja | Poza zakresem reakcji | Referencja CPU pozostaje dostępna |
| Węgiel | Poza zakresem reakcji | Nie jest wystawiony jako palny materiał GPU |
| Proch / termit / nitrogliceryna | Poza zakresem chemii I10 | Dotychczasowy gameplay wybuchów pozostaje osobnym interfejsem |
| Fala ciśnienia / akustyka | Poza zakresem gazu I10 | Zachowano dotychczasowe impulsy gry; projekcja MAC nie jest falą akustyczną |
| Gaz → siły na postaci/cieczy | Poza zakresem | Brak pełnego dwukierunkowego sprzężenia mechanicznego |
| Porowatość piasku / grubsza siatka gazu | Niewłączone | Bez domniemanego uszczelniania z gęstości |

## Odtworzenie

```powershell
./StrefaZero.exe --mission --gpu-thermal on
./StrefaZero.exe --gpu-thermal-scene fire
./StrefaZero.exe --gpu-thermal-scene oil
./StrefaZero.exe --gpu-thermal-scene quench
./StrefaZero.exe --gpu-thermal-scene phases
./scripts/profile-thermal.ps1 -Executable ./StrefaZero.exe -Output wyniki-jakosci -Suite quality
./scripts/profile-thermal.ps1 -Executable ./StrefaZero.exe -Output wyniki-pomiarow -Suite benchmark
python scripts/profiling/analyze-thermal.py wyniki-pomiarow --controls
```

W laboratorium włącz „Termika i gazy I10”, wybierz scenę i przycisk „Wczytaj scenę I10”. Promieniowanie jest osobną opcją. CPU i fallback GL 3.3 zachowują dotychczasowy model referencyjny; `--gpu-thermal` dotyczy świata GPU.

## Protokół odbioru

Bramka bilansu: <0,1% błędu względnego masy i H + energii chemicznej przez 600 ticków, z odjęciem jawnych źródeł/ujść. Izolowana para jest też wykonywana przez 2400 ticków. Kontrole obejmują pełne 32768 nośników w jednej komórce, plateau fazowe, brak przepływu przez cienką ścianę, wyczerpanie tlenu, chłodzenie wodą, parę/skropliny, źródła energii, zdarzenia gry, render i nagrania.

Macierz kosztu ma 4 sceny (zimny piasek/woda, drewno, olej, woda gasząca), 4 etapy (`off`, A, A+B, A+B+C), 3 bloki ABBA, 120 ticków rozgrzewki i 240 próbek na segment. Razem 192 segmenty i 46 080 ticków. Render następuje co drugi tick, czyli dwa kroki na klatkę 60 Hz przy obrazie 960 × 576. Każdy segment resetuje tę samą scenę; jest to pomiar rozwijającego się świata, a nie deklaracja identycznej trajektorii podczas włączenia reakcji.

Dodatkowy koszt to mediana segmentu kandydata minus średnia median dwóch referencji off w tym samym bloku. Hipotezy z planu pozostają 0,70 ms/tick dla gorących scen oraz 0,05 ms/tick dla zimnego włączonego świata. Nie kwalifikujemy wyłączonego modułu jako wyniku zimnej włączonej termiki. Osobno raportujemy A/B/C, dodatkowy render D, p95/p99, CPU submit i parę ticków z renderem. Te ostatnie nie zawierają prezentacji okna i nie zastępują pomiaru start–start aktywnego okna.

## Wyniki poprawności

Funkcje A–D są wdrożone w zadeklarowanym wyżej zakresie. **Odbiór wydajności I10 pozostaje otwarty; termika jest domyślnie wyłączona.** Nie zmieniono tolerancji 0,1% ani historycznych wyników I04.5, I07 i I09.

| Seria | Wynik | Zakres |
| --- | --- | --- |
| Pełny CTest Windows | 64/64 PASS, 854,45 s | CPU, świat GPU, VFX, render, sesja, nagrania, UI i fallback 3.3 |
| Końcowa kontrola integracji | 10/10 PASS | Po dodaniu osłony wodnej i przekazania masy terenu do gruzu |
| Po zawężeniu barier | 3/3 PASS | Termika, UI, instrumentacja |
| Po specjalizacji programów | 3/3 PASS | Końcowy EXE i shadery użyte w drugiej macierzy |
| Nowy `gpu.thermal_edges` | 1/1 PASS, 46,20 s | Pełne QA termiki na AoS/sorted, zamknięta ściana → otwarte drzwi, inspektor/reset |
| CPU GCC | 10/10 PASS, 40,06 s | Bez aplikacji; ostrzeżenia traktowane jako błędy |

To osobne serie z zachowanymi hashami, nie deklaracja pojedynczego uruchomienia 65/65. Pełna seria poprzedza ostatnie poprawki termiki; ich zakres ma powtórzone testy powyżej. [JUnit i logi](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/ctest-complete/ctest.xml), [końcowa integracja](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/ctest-final-check/ctest.xml), [specjalizacja](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/ctest-specialized/ctest.xml), [AoS i drzwi](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/ctest-edges/ctest.xml), [GCC](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/gcc-ctest.xml).

W końcowej serii kontrolnej największy względny błąd masy wyniósł 5,164·10^-8, a H + energii chemicznej 1,810·10^-7. Największy błąd bezwzględny H wyniósł 5,337 przy pełnych 32768 gorących nośnikach w jednej komórce; względny błąd pozostaje poniżej bramki. Izolowana para ma błąd H 0,00006104 po 600 i po 2400 tickach, bez przyrostu dryfu między tymi punktami. [Pełny bilans](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/quality/thermal-balances.csv).

Kontrole obejmują rzeczywisty solver w czterech scenach po 600 ticków, dostępną energię wspólnego donora, fazy CPU/GPU, skraplanie i narodziny nośnika, promieniowanie, potwierdzone eksplozje bez podwójnego grzania, brak ciepła z odrzuconego strzału, zachowanie masy podczas rozbicia ściany i niezmienność termiki podczas renderowania. W próbie lokalnego paliwa woda zachowuje całe paliwo i obniża temperaturę poniżej zapłonu; sucha kontrola pozostawia około 0,7449 początkowego paliwa. Scena demonstracyjna `quench` nie oznacza natychmiastowego zgaszenia całej komory: woda może spłynąć z części powierzchni.

Przy 30/60/144 FPS uzyskano tę samą dawkę 600. Pauza i utrata fokusu dodają zero; pojedynczy krok wykonuje jeden tick. Krótkie kliknięcie przetrwało klatkę bez ticka, a replay v3 odtworzył dawki. [Kontrole schedulera i nagrań](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/quality/thermal-session-tests.txt). Inspektor zwraca żądaną komórkę, a reset usuwa oczekujące próbki poprzedniej generacji. [Próba drzwi](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/edges/thermal-door.csv).

## Wyniki kosztu i decyzja

Sprzęt: GTX 1650, OpenGL 4.3 / sterownik NVIDIA 610.47, i5-9300HF, Windows 11. Build MSVC 19.51.36256.0 Release/LTO, C++23, `/W4 /WX`. Fizyczny solver `legacy`, renderer `basic`, VFX wyłączone w macierzy, 960 × 576. Każda z dwóch macierzy ma 46 080 próbek / 192 segmenty; wszystkie końcowe stany są skończone, a osiem negatywnych kontroli kompletności analizy przechodzi.

| Scena | Dodatkowe A, zakres median | Dodatkowe A+B | Dodatkowe A+B+C | Limit pełnego wariantu |
| --- | ---: | ---: | ---: | ---: |
| Zimny piasek/woda | 1,353–1,428 ms | 2,628–2,694 ms | 3,337–3,382 ms | 0,05 ms — FAIL |
| Drewno/ogień | 1,367–1,395 ms | 2,615–2,638 ms | 3,246–3,270 ms | 0,70 ms — FAIL |
| Olej | 1,728–1,754 ms | 2,966–2,995 ms | 0,183–3,662 ms | 0,70 ms — FAIL |
| Woda gasząca | 5,267–5,339 ms | 8,159–8,324 ms | 9,653–9,717 ms | 0,70 ms — FAIL |

Wartości to przyrost całego ticka względem referencji ABBA, łącznie z integracją fizycznej puli. Spalanie zmienia liczbę nośników, więc przyrost w scenie oleju obejmuje też zmianę przebiegu świata; nie jest kosztem samego kernela na identycznym stanie. Zmienność tej sceny pozostaje widoczna w surowych danych.

| Scena pełna | Cały tick med./p95/p99 | Dwa ticki + render GPU med./p95/p99 | CPU submit med./p95/p99 na tick |
| --- | --- | --- | --- |
| Zimna | 6,864 / 7,180 / 8,281 ms | 14,049 / 15,356 / 15,640 ms | 0,839 / 1,879 / 72,304 ms |
| Ogień | 3,428 / 4,109 / 4,981 ms | 7,103 / 8,407 / 8,663 ms | 0,849 / 1,674 / 35,181 ms |
| Olej | 5,696 / 5,865 / 7,019 ms | 11,668 / 12,888 / 13,104 ms | 0,830 / 1,389 / 61,353 ms |
| Gaszenie | 18,989 / 19,556 / 24,036 ms | 39,227 / 43,747 / 44,495 ms | 2,204 / 4,632 / 212,165 ms |

Każda kolumna pokazuje maksimum danej statystyki spośród sześciu segmentów kandydata. CPU submit obejmuje wywołania GL i oczekiwanie przy backpressure; nie jest czystym kosztem obliczeń CPU. Harness opróżnia kolejkę co 16 ticków, poza zakresem pojedynczej próbki CPU. Timestamp pary GPU obejmuje również ewentualne przerwy między zleceniami, ale nie prezentację okna. Rozkłady submitu całej pary są w JSON. Ta próba nie kwalifikuje aktywnego okna ani celu p95 ≤16,67 ms.

Osobny profil A/B/C z pierwszych 120 ticków (bez rozgrzewki macierzy) daje mediany odpowiednio: zimna 1,231/1,252/0,555 ms; ogień 1,196/1,231/0,530 ms; olej 3,392/2,957/1,161 ms; gaszenie 3,416/2,952/1,170 ms. Nie dodajemy tych median do wyników innego przedziału czasu. CSV zapisuje osobno submit i asynchroniczny wynik GPU; analizator łączy rekordy po scenie i ticku, zamiast traktować pola zerowe jako pomiar GPU. P95/p99 i wszystkie surowe próbki są zachowane.

Pierwsza macierz miała maksymalny przyrost 10,090 ms dla gorącej i 3,443 ms dla zimnej sceny. Następnie zawężono bariery do wymaganych typów, dodano strażnika powtórzonych ticków i skompilowano osobne specjalizacje operacji ze wspólnego kodu GLSL. Druga macierz ma odpowiednio 9,717 i 3,382 ms. To nie wystarcza do odbioru. Nie obniżono liczby iteracji projekcji ani tolerancji bilansu, nie zmieniono solvera legacy.

Włączona zimna termika nadal wykonuje przebiegi całej siatki; **wykrywanie aktywności i pomijanie zimnych obszarów pozostaje niewdrożone**. Dalsza redukcja kosztu wymaga projektu aktywnych obszarów i integracji kondensacji/list fizycznych, z zachowaniem tych samych bilansów. I04.5 pozostaje odłożone. Nie podwyższamy budżetu do uzyskanego wyniku.

Pamięć modułu: 21 067 808 B po inicjalizacji, do 21 313 568 B z ośmioma slotami telemetrii; trzy próbki inspektora dodają najwyżej 384 B. To własne bufory, bez wewnętrznej pamięci programów sterownika. Przy domyślnym `off` moduł nie jest tworzony. Po wcześniejszym włączeniu `off` zatrzymuje aktualizację i obraz, zachowując stan do ponownego włączenia/resetu.

Surowe wyniki: [macierz 1](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/benchmark-1/thermal-samples.csv), [macierz 2](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/benchmark-2/thermal-samples.csv), [analiza 2](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/benchmark-2/thermal-summary.json), [profile A/B/C](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/benchmark-2/thermal-pass-samples.csv), [kontrole analizatora](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/benchmark-2/analysis-controls.json). Metadane rozróżniają hashe EXE obu serii i końcowe hashe shaderów.

## Obrazy i wydanie

Porównania po tej samej liczbie ticków, układ `off / A / A+B / A+B+C`: [zimna scena](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/benchmark-2/cold-comparison.png), [ogień](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/benchmark-2/fire-comparison.png), [olej](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/benchmark-2/oil-comparison.png), [gaszenie](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/benchmark-2/quench-comparison.png). Obraz jest dowodem kompozycji; bilanse i przepływ oceniają osobne kontrole numeryczne.

Paczka `StrefaZero-Windows-x64-I10` zawiera EXE, SDL, shadery, licencje, ten raport, CSV, obrazy i skrypty. `BUILD_INFO.json`, manifest źródeł i `SHA256SUMS.txt` identyfikują artefakt. ZIP ma osobną sumę SHA256. Wyniki sprawdzenia faktycznie skopiowanej paczki znajdują się w [`i10/package-*`](https://github.com/maershal/strefa-zero-measurements/tree/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10).

Końcowy katalog wydania przeszedł `--gpu-thermal-check --gl-debug`, poligon CPU GL 4.3 i fallback GL 3.3: każdy proces zakończył się kodem 0 bez timeoutu. Hash głównego EXE odpowiada drugiej macierzy. Sprawdzone logi KHR_debug z QA, próby AoS/drzwi i trzech prób paczki mają zero komunikatów error/undefined behavior; ostrzeżenia sterownika o migracji buforów podczas jawnych odczytów diagnostycznych pozostają w logach. [Kontrola logów](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/khr-debug-summary.json), [metadane buildów i prób](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/build-provenance.json).

Audyt względem zachowanej roboczej bazy I09 potwierdza identyczne bajty wszystkich 42 dotychczasowych shaderów. Dodano sześć plików termiki; zmiany integracyjne C++ i nagrań są jawne w [różnicy źródeł](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/source-delta-i09.json) i [patchu I10](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/i10/I10-source.patch). Nie cofano istniejących niezacommitowanych zmian.
