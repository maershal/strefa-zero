# Plan rozwoju: wspólny świat GPU, wydajność i efekty

Data: 2026-10-28. Punkt wyjścia: źródła Strefa Zero 0.7.0 w `outputs/strefa-zero`.
Status: **I04 wdrożone: 21/21 regresji, okna combined 3/3. P95 CPU submit <0,1 ms w benchmarku; cel 1,5 ms GPU pozostaje otwarty. Kolejny zakres: I05. Nie uznajemy historycznej niestabilności GPU za naprawioną**.

To główny plan dalszych prac. Dokumenty `GPU_MIGRATION.md`, `GPU_LIQUIDS.md` i `GPU_MISSION_070.md` opisują etapy istniejącego wydania; ten dokument określa kierunek następnych zmian. Przy każdej iteracji aktualizujemy checklistę, tabelę wyników i dziennik decyzji poniżej. Nie zmieniamy historycznych raportów, żeby dopasować je do nowych założeń.

## 1. Ocena review na podstawie kodu

Kierunek review jest trafny: najpierw ograniczyć narzut przebiegów i komunikacji, następnie połączyć ścieżki aplikacji, dopiero potem wydawać odzyskany budżet na wodę, ogień i oświetlenie. Kilka diagnoz wymaga doprecyzowania przed wdrożeniem.

| Obserwacja | Weryfikacja i konsekwencja dla planu |
| --- | --- |
| Około 204 dispatchy na krok misji | Potwierdzone statycznie dla mieszanki ziaren i cieczy: 10 przebiegów przygotowania + 2 × 97 na podkroki. Co 30 kroków dochodzą 4 przebiegi diagnostyki, razem 208. To liczba zleceń także wtedy, gdy część pracy kończy się wcześnie. Źródło: `src/gpu/world.cpp:307–343`. |
| Siatka budowana 21 razy na podkrok | Potwierdzone: 12 razy na początku iteracji, 8 po korekcie cieczy i raz przed odtworzeniem prędkości. To 42 wywołania `bins()` i 84 dispatchy na krok. Każdy `bins()` czyści 320 × 192 nagłówki, a potem wstawia aktywne ID. |
| Redukcja postaci 42 razy na krok | Potwierdzone dla mieszanej misji: 8 + 12 + 1 na podkrok. `mission.comp:131–139` przegląda wszystkie aktywne cząstki w jednej grupie. Zbierane są korekty pozycji i kontakt podparcia, nie wyłącznie siły. |
| Odczyt GPU → CPU co tick blokuje okno | Misja co krok wywołuje `stage_count()`, ale jedna oczekująca kopia może scalać następne żądania. `poll()` wykonuje `glGetBufferSubData` dopiero po `glClientWaitSync(..., 0, 0)` zwracającym gotowość. To nie jest bezwarunkowe oczekiwanie na cały GPU. Nadal trzeba zbadać koszt alokacji staging, mapowania/odczytu, aktualizacji buforów, zapytań oraz presji kolejki sterownika. Źródło: `world.cpp:152–159, 346–369`. |
| Fizyka płaci za przebiegi, a nie cząstki | Wyniki sugerują duży koszt stały przy małych scenach, lecz liczba sąsiadów i cząstek nadal ma znaczenie, zwłaszcza przy 32k. Sceny różnią się układem i kontaktami; sam wykres czasu względem liczby cząstek nie rozdziela tych przyczyn. |
| Dwa kroki zajmują około 55% klatki | Z mediany 3,73203 ms wynika 7,46406 ms na dwa kroki, czyli około **44,8%** klatki 16,67 ms. Koszt w oknie może być inny. Percentyle całej klatki mierzymy bezpośrednio; nie sumujemy percentyli niezależnych pomiarów. |
| Woda jest nieprzezroczysta, a piasek rysowany nad nią | Potwierdzone. `liquid_surface.frag` odrzuca pole poniżej 0,35 i zwraca alfa = 1. `render()` rysuje wodę przed terenem i ziarnami. |
| Teren to 61 440 quadów, kosztowne odpytywanie stanu GL | Potwierdzone: `DrawArraysInstanced(..., width*height)` oraz 14 wywołań `glGetIntegerv` i 4 `glIsEnabled` w `render()`. Puste komórki są odrzucane w shaderze wierzchołków. |
| Listy wiązane, AoS 64 B i przełącznik `operation` | Potwierdzone w `particle_common.glsl`, `granular.comp`, `liquids.comp`, `mission.comp`. Shader odwołuje się do 64-bajtowej struktury; rzeczywistą liczbę odczytanych bajtów określa też kompilator i cache, więc nie utożsamiamy rozmiaru struktury z każdym transferem pamięci. |
| Powielone światy i logika gry | Istnieją dwa różne modele numeryczne: CPU/MAC i GPU/PBD/PBF, oraz kilka instancji stanu: poligon CPU, laboratorium CPU, laboratorium GPU i misja GPU. `Application` tworzy dwa `ParticleWorld`, łączy orkiestrację, testy i logikę ekranów. |
| Efekty korzystają z ImGui | Potwierdzone dla poligonu CPU w `ui/arena.cpp`. Misja GPU część obrazu generuje już własnymi shaderami `mission.vert/.frag`; migracja VFX musi objąć obie dotychczasowe ścieżki, bez tworzenia trzeciej. |
| Brakuje diagnostyki GL | W `platform/window.cpp` brak żądania kontekstu debug i instalacji callbacku KHR_debug. Dodajemy diagnostykę w buildach deweloperskich; ostrzeżenia sterownika są wskazówkami, nie dowodem przyczyny opóźnień. |

### Ważne korekty propozycji technicznych

1. **Trwałe mapowanie wymaga sprawdzenia możliwości.** Projekt wymaga obecnie GL 4.3, a `glBufferStorage` jest w rdzeniu GL 4.4 lub rozszerzeniu ARB_buffer_storage. Zachować ścieżkę GL 4.3. `GL_STREAM_READ` jest wskazówką dla sterownika, nie gwarancją miejsca alokacji ani braku oczekiwania. Podstawa: [ARB_buffer_storage](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_buffer_storage.txt), [karta API GL 4.4](https://www.khronos.org/files/opengl44-quick-reference-card.pdf).
2. **Jedna siatka na podkrok wymaga kompletnego zbioru kandydatów.** Nie wolno pozostawić starych `head/link` i pytać ich według nowych pozycji. Lista z marginesem musi obejmować względny ruch obu cząstek, korekty PBF/PBD, kolizje terenu i kapsuły. Potrzebna kontrola ważności oraz przebudowa przed wykorzystaniem niekompletnej listy.
3. **Współczynniki wody nie są gotowymi wartościami do skopiowania.** Kod ma już `h=2`, 8 iteracji cieczy na podkrok, `s_corr` z `k=0,0001`, XSPH i kohezję tej samej fazy z `k=60`. Zakres 0,01–0,1 oznacza wzrost antyklastrowania o 100–1000 razy. Testować po normalizacji skali; nie wprowadzać jako bezpiecznego ustawienia domyślnego. Źródło: `liquids.comp:4, 72–75, 87–105`.
4. **Small steps to eksperyment jakości i kosztu.** Więcej podkroków oznacza też więcej predykcji, wyszukiwania sąsiadów i sprzężenia postaci. Wynik dla XPBD nie gwarantuje przyspieszenia obecnego PBF. Podstawa: [Small Steps in Physics Simulation](https://mmacklin.com/smallsteps.pdf).
5. **Wirowość może wymagać dodatkowego przebiegu.** Najpierw trzeba policzyć lokalną wirowość, potem jej zmiany w sąsiedztwie. Samo dopisanie obliczeń do XSPH nie zapewnia poprawnej globalnej synchronizacji. Podstawa: [Position Based Fluids](https://mmacklin.com/pbf_sig_preprint.pdf).
6. **Cache widoczności par nie jest bezwarunkowo trwały.** Promień między przesuwającymi się końcami może przeciąć ścianę nawet bez edycji terenu. Używać bezpiecznego obszaru ważności albo ponawiać DDA przy przeszkodach. Zmiana rewizji terenu zawsze unieważnia odpowiedni cache.
7. **JFA nie daje automatycznie głębokości optycznej.** Najkrótsza odległość od powierzchni, pionowa głębokość i grubość na drodze promienia to różne wielkości. Dla 2D wybieramy jawnie stylizowaną grubość; przy sumach w kolumnach rozdzielamy odłączone zbiorniki i przeszkody.
8. **Obecne SDF jest polem lokalnym.** `granular.comp:21–29` bada otoczenie ±3 i ogranicza odległość do 3,5 komórki. Nadaje się do pobliskich kolizji, ale nie jest gotowym polem odległości dla dalekich cieni. Oświetlenie wymaga pola o odpowiednim zasięgu albo osobnego śledzenia zasłon.
9. **Płynny wygląd dymu nie zastępuje bilansu spalania.** Stable fluids może być bazą przepływu, ale zwykła adwekcja półlagranżowska nie gwarantuje zachowania masy i entalpii. Reakcje i wymianę ciepła projektujemy osobno, z pomiarami błędów. Podstawa: [Real-Time Fluid Dynamics for Games](https://www.dgp.toronto.edu/people/stam/reality/Research/pdf/GDC03.pdf).

## 2. Architektura docelowa i granica CPU/GPU

**Jeden aktywny świat fizyczny GPU, jedna implementacja reguł rozgrywki i wspólne narzędzia do tego świata.** Widok misji i laboratorium mają być sposobami pracy z tym samym silnikiem. CPU i GPU zachowują różne odpowiedzialności.

| Właściciel | Odpowiedzialność |
| --- | --- |
| `GameplaySession` na CPU | Intencje sterowania, reguły broni i przeładowania, zasoby gracza, cele misji, wynik, później AI. Tworzy numerowane polecenia i konsumuje zdarzenia fizyki. |
| `SimulationWorld` na GPU | Autorytatywne położenia i prędkości postaci, pocisków, granatów i materiałów; kontakty, trafienia geometryczne, destrukcja, ciecze, gazy i termika. Fizyczne reakcje wymagające sprzężenia w tym samym podkroku zostają tutaj. |
| `GpuDevice` / współdzielone zasoby | Programy shaderów, możliwości GL, bufory, timery, transport poleceń i wyników. Jeden właściciel kontekstu GL na wątku okna; nie przenosimy automatycznie wywołań GL na wątki robocze. |
| `SceneRenderer` | Jawna lista przebiegów i ich zasobów, kompozycja świata, cieczy, VFX, świateł i postprocessu. Odczytuje bieżący stan GPU. |
| `Application` / UI | Cykl życia, wybór sceny, wejście, zegar i prezentacja. ImGui odpowiada za HUD oraz narzędzia. Testy mają osobne scenariusze sterujące produkcyjnym interfejsem. |
| CPU poza grą | Małe obliczenia referencyjne, import/eksport, stare sceny badawcze i testy. CPU/MAC pozostaje narzędziem odniesienia, bez równoległego rozwoju drugiej pełnej gry. |

### Kontrakt asynchroniczny

- Polecenia zawierają `world_generation`, `command_id` i docelowy tick. Zdarzenia dodają tick wykonania i stabilne ID obiektów. Reset unieważnia stare wyniki.
- Polecenie strzału jest autoryzowane na CPU, a GPU wyznacza początek pocisku z aktualnej, skorygowanej pozycji lufy. CPU nie odtwarza kolizji ze spóźnionego obrazu świata.
- Kontakty kapsuły z cieczą i ziarnami są rozwiązywane na GPU w tym samym podkroku. CPU odbiera potwierdzone zdarzenia trafień i zmian stanu; wynik nalicza raz dla każdego ID zdarzenia.
- HUD może być próbkowany co 2–4 ticki, czyli co około 16,7–33,3 ms przy 120 Hz. Kanał zdarzeń rozgrywki obsługujemy co tick; nie dziedziczy wolniejszego rytmu HUD.
- Kolejki mają jawną pojemność, liczniki opóźnienia i zasady przeciążenia. Nie nadpisujemy niewykonanych strzałów, destrukcji ani zdarzeń wyniku. Wizualne emisje można ograniczać; stan fizyczny i zdarzenia krytyczne muszą zostać zachowane lub żądanie musi być jawnie odrzucone przed jego zatwierdzeniem.
- CPU nie czeka w pętli okna na pojedynczy tick GPU. Ustalamy dopuszczalny wiek zdarzeń i maksymalną liczbę ticków w kolejce; mierzymy opóźnienie sterowania. Przekroczenie limitu ogranicza dalsze zlecanie pracy, nie powoduje ukrytego gubienia czasu ani wyników.
- Po przeniesieniu reguły do CPU usuwamy jej runtime'owy odpowiednik GLSL. Zachowanie dotychczasowej misji służy do regresji, nie jest drugą aktywną implementacją.

## 3. Pomiar początkowy i budżety

Poniżej **istniejące** wyniki wydania 0.7.0 z `docs/gpu070/build/gpu-mission-benchmark.csv`. Nie wykonywano nowego benchmarku przy pisaniu planu.

| Scena | Cząstki na początku pomiaru | Mediana GPU/krok | p95 GPU/krok | Mediana CPU submit |
| --- | ---: | ---: | ---: | ---: |
| painted, bez misji | 6064 | 1,14266 ms | 1,86368 ms | 0,0192 ms |
| mission | 3096 | 3,73203 ms | 5,11046 ms | 3,9785 ms |
| stress_16k | 16000 | 4,01555 ms | 5,47389 ms | 4,0909 ms |
| stress_32k | 32768 | 8,06912 ms | 9,96966 ms | 7,4499 ms |

Z `mission-frames.csv`: mediana próbki CPU submit w oknie 1,3253 ms, maksimum 11,404 ms. Wiersz zapisuje ostatni koszt kroku, a nie sumę wszystkich kroków w klatce. `ticks()` w benchmarku zleca pracę bez limitu FPS i wykonuje `glFinish()` co 16 kroków; to inny sposób obciążania kolejki niż rzeczywiste okno. Koszt CPU submit obejmuje obecnie `poll()` i może obejmować oczekiwanie sterownika. Z tych danych nie można przypisać całego opóźnienia jednemu `glGetBufferSubData`.

Cele po odchudzeniu rdzenia, dla tej samej misji i sprzętu referencyjnego GTX 1650:

- GPU physics: mediana **<1,5 ms/krok 1/120 s**; roboczy cel p95 <2 ms. To cele optymalizacji, nie obietnica dla każdej sceny 32k.
- CPU submit: mediana **<0,1 ms/krok**; roboczy cel p95 <0,25 ms. Osobno raportować `poll`, przygotowanie poleceń i całą logikę CPU.
- Render wody: sprawdzić docelowy koszt **około 0,2–0,3 ms/klatkę przy 960 × 576**. Rozdzielić całkowity czas renderu, koszt samej wody i przyrost względem starego wariantu; liczby z review są szacunkiem.
- Cała klatka przy celu 60 FPS: p95 ≤16,67 ms w teście bez ograniczenia VSync, bez utraty czasu w uzgodnionej trasie. Osobno test okna z VSync i opóźnienie wejścia. Sceny przeciążeniowe raportujemy także wtedy, gdy przekraczają budżet.
- 16k/32k: zachować raport skalowania, p99, maksimum, liczbę kontaktów i rozkład liczby sąsiadów. Nie poprawiać wyniku przez utratę materiału, pominięcie kontaktów lub ukryte zmniejszenie dokładności.

Protokół pomiaru: Release, zapis GPU/sterownika/rozdzielczości/ustawień, stałe sceny i seed, rozgrzewka, co najmniej 3 porównywalne przebiegi. Osobno test przepustowości bez ograniczenia i normalna pętla okna. Timery GPU nieblokujące; eksporty i synchroniczne walidacje poza pomiarem. Koszt liczników mierzymy również z diagnostyką wyłączoną. Nie dodajemy czasu CPU i GPU jako czasu klatki, bo ich praca może się nakładać.

## 4. Kolejne iteracje

Każda iteracja kończy się działającą, porównywalną wersją, krótkim raportem i decyzją o następnej. Jeśli zakres jest za duży na jeden przegląd zmian, dzielimy go według poniższych podpunktów; nie łączymy optymalizacji modelu numerycznego z wymianą renderera w jednym pomiarze A/B.

| ID | Zakres | Zależności | Status |
| --- | --- | --- | --- |
| I00 | Pomiar przebiegów i diagnostyka | — | Instrumentacja [x]; baza okna otwarta |
| I01 | Transport wyników bez oczekiwania | I00 | [x]; ograniczenia FPS w raporcie |
| I02 | Wyspecjalizowane przebiegi i koszt GL | I00–I01 | [x] |
| I03 | Dane cząstek i kompletne sąsiedztwo | I02 | [x]; wydajność okna otwarta |
| I04 | Lokalne sprzężenie postaci | I03 | [x]; cele wydajności w raporcie |
| I05 | Jeden silnik i odchudzenie Application | I01–I04 | [ ] |
| I06 | Jedna logika rozgrywki na CPU | I05 | [ ] |
| I07 | Jakość fizyki cieczy | I03–I06 | [ ] |
| I08 | Renderer i wygląd wody | I05–I06; profil I07 | [ ] |
| I09 | GPU VFX i wtórne cząstki | I08 | [ ] |
| I10 | Termika, gazy i ogień | I05–I09 | [ ] |
| I11 | Światło, HDR i końcowy styl | I08–I10 | [ ] |

### I00 — odtworzenie pomiarów i wskazanie rzeczywistych kosztów

Cel: mieć wiarygodny punkt porównania i oddzielić koszt dispatchy, sąsiedztwa, kapsuły, odczytu oraz kolejki sterownika.

- [x] Dodać liczniki dispatchy, barier, przebudów siatki, aktualizacji list aktywnych, odczytów, transferowanych bajtów i zapytań GL; nie wykonywać nowych synchronicznych odczytów tylko dla tych liczników.
- [x] Podzielić pomiar CPU na `poll`, aktualizacje wejścia/buforów, zlecenia fizyki i staging. GPU mierzyć grupami przebiegów w trybie profilowania, kontrolując narzut samych zapytań.
- [x] Dodać przełączniki eksperymentalne: HUD readback wyłączony, readback rzadszy, samo sprzężenie kapsuły wyłączone, diagnostyka wyłączona. Warianty służą ustaleniu kosztów; nie są wersją fizyki przeznaczoną do wydania.
- [x] Włączyć kontekst debug, etykiety zasobów i grupy KHR_debug w buildach deweloperskich. Callback nie powinien wykonywać ciężkiej pracy; tryb synchroniczny tylko do diagnozy. Pomiary Release bez niego.
- [x] Uruchomić dotychczasowe testy CPU/GPU i okna; zapisać aktualny punkt odniesienia w nowym katalogu raportów. Kod 77 oznacza pominięcie GPU, nie sukces.

Gotowe, gdy: licznik odtwarza 204/208, raport rozdziela okno i benchmark bez limitu, a warianty A/B wskazują źródła opóźnień zamiast zakładać je z góry.

Raport wdrożenia: [I00_GPU_PROFILING.md](I00_GPU_PROFILING.md). Liczniki i testy pomiarów przeszły. Test okna wykazał zmienne osiągi także w starej wersji; nie uznajemy bazy FPS za odebraną i nie zmieniamy kryteriów testu, żeby to ukryć.

### I01 — pierścień odczytów i rozdzielenie telemetrii od zdarzeń

Pliki początkowe: `gpu/world.cpp`, `gpu/world.hpp`, `gpu/mission.hpp`.

- [x] Szybkie A/B: zmienić hint wyłącznie bufora staging na `GL_STREAM_READ`; zmierzyć wynik bez zakładania, że hint rozwiąże problem.
- [x] Wydzielić `ReadbackRing`: początkowo 3 sloty, osobny fence i numer generacji/ticka na slot. Nie używać ponownie slotu, którego GPU jeszcze nie zwolniło.
- [x] Preferowana ścieżka: ARB_buffer_storage/GL 4.4, mapowanie READ | PERSISTENT | COHERENT, kopia do wolnego slotu, fence po kopii, odczyt dopiero po gotowości. Zachować BUFFER_UPDATE_BARRIER przed kopiowaniem wyników zapisanych przez shadery. Dla wariantu niekoherentnego wymagana też bariera CLIENT_MAPPED_BUFFER przed fence. Reguły widoczności: [ARB_buffer_storage](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_buffer_storage.txt).
- [x] Zachować GL 4.3: pierścień staging, kopia i fence, odczyt/mapowanie gotowego slotu; bez oczekiwania z dodatnim timeoutem w pętli klatki. Przy pełnym pierścieniu HUD zachowuje ostatni obraz i pomija nową próbkę.
- [x] HUD publikować co 2 ticki; dodać wiek wyniku, zaległość i licznik pominiętych próbek. Oddzielić kontrakt telemetrii od niezawodnych zdarzeń: obecny pierścień obsługuje tylko HUD, implementacja kanału z potwierdzeniami należy do I06.
- [x] Zabezpieczyć reset, przełączenie sceny i zniszczenie świata przy odczycie w locie; stare wyniki nie mogą aktualizować nowego świata.

Gotowe, gdy: test pełnego pierścienia nie blokuje okna ani nie psuje danych, reset odrzuca stare generacje, obie ścieżki możliwości GL przechodzą testy, a porównanie A/B wskazuje faktyczny zysk CPU. Brak osiągnięcia 0,1 ms jest powodem dalszej diagnozy, nie usuwania potrzebnych danych.

Raport wdrożenia: [I01_GPU_READBACK.md](I01_GPU_READBACK.md). W kontrolowanym A/B p95 CPU ticka około 0,08–0,09 ms; osobno sprawdzono koszt poll i sumę CPU klatki. Regresje 11/11. Okresowe spowolnienie całego GPU nadal występuje we wszystkich backendach i nie jest uznane za naprawione.

### I02 — tańsze zlecanie pracy i wyspecjalizowane programy

- [x] Z jednego źródła/helperów kompilować osobne programy dla poszczególnych przebiegów przez `#define PASS_*` wstawiony po `#version`. Zachować wspólne ABI i logi kompilacji.
- [x] Porównać `local_size_x` 64/128/256 na docelowym GPU, w tym odpowiednio rozmiar redukcji i argumenty indirect. Wybrać konfigurację pomiarem, osobno dla rodzajów pracy.
- [x] Bufory stałe bindować raz dla harmonogramu świata; aktualizować tylko zmieniane wiązania ping-pong. Usunąć nadmiarowe `UseProgram(compute)` przed natychmiastowym wyborem innego programu.
- [x] Opisać zależności odczytu/zapisu przebiegów. Bariery dobierać do konsumenta: SSBO, indirect command, tekstura, kopia lub odczyt CPU. Usunąć tylko bariery bez zależności; `barrier()` nie synchronizuje różnych grup roboczych.
- [x] Dla pracy zależnej od aktywności generować argumenty indirect na GPU. Zero grup ogranicza pracę urządzenia, lecz pozostawia koszt wywołania API. Nie sterować wyłączaniem solvera ze spóźnionego licznika CPU.
- [x] Timery zbierać FIFO: tylko najstarszy oczekujący wynik w każdej kolejce; zatrzymać zbieranie na pierwszym niegotowym. Nie skanować obu tablic po 32 wpisy przy każdym `poll()`.
- [x] Ograniczyć przygotowanie aktywnych ID i terenu do rzeczywistych zmian, zachowując zgodność obu kopii ping-pong po edycji i brak odradzania usuniętych ID.

Gotowe, gdy: te same testy fizyki przechodzą, raport podaje spadek liczby wywołań i kosztów CPU/GPU oraz nie ma nowych błędów widoczności pamięci. Samo zwiększenie liczby programów nie jest uznawane za przyspieszenie.

Raport wdrożenia: [I02_GPU_EXECUTION.md](I02_GPU_EXECUTION.md). 27 programów, tracker wiązań, warunkowe komendy i FIFO. API w przykładowym ticku 2662 → 740; GPU misji około 3,66 → 3,40 ms w kontrolowanym A/B. Regresje 12/12 i okna 6/6. Pozostaje 204/208 dispatchy i 42/43 budowy siatki na tick; następny zakres to I03. Pędzel nadal używa pełnej ścieżki edycji, a gating przygotowania danych dotyczy ticka misji.

### I03 — uporządkowane dane i bezpieczne używanie sąsiedztwa ponownie

Etap A: counting sort / prefix scan / scatter aktywnych cząstek według komórek; zakresy `cell_start/count` zamiast list `head/link`. Etap B: rozdzielenie często używanych pozycji, prędkości i właściwości od historii renderowania/kompensacji; wybrać SoA lub AoSoA przez pomiar. Zachować stabilne ID, mapowanie ID → indeks i rezerwację wolnych miejsc.

- [x] Przy siatce jednokomórkowej i promieniu 2 zapytanie 5 × 5 można scalić w 5 zakresów wierszy tylko dla odpowiednio posortowanego układu i z przycięciem brzegów. Margines ruchu może zwiększyć okno; zakresy nadal wymagają filtrowania dystansu i widoczności.
- [x] Kompletne zakresy kandydatów ze sprawdzanym na GPU przemieszczeniem od anchorów. Zamiast niezmiennej listy par `R + skin` zapytanie wokół bieżącego końca ma promień `R + Dmax` drugiego końca. Obejmuje pełne korekty, kompensację, teren i kapsułę. Dla `2*Dmax > skin` bezpieczny fallback rozszerza zakresy zamiast używać nieważnej listy; pełny kontrakt i dowód w raporcie I03.
- [x] Indeks powstaje po predykcji raz na podkrok. Przed każdym użyciem kandydaci obejmują bieżące przemieszczenie; nie ma odczytu starych komórek według niechronionych nowych pozycji. Spawn, destrukcja i zmiana aktywnych ID unieważniają indeks przed użyciem. Liczniki z CPU oraz nagłówek GPU potwierdzają rzeczywiste budowy.
- [x] Nie ograniczać list stałym limitem bez obsługi przepełnienia. Dodać licznik, rozszerzenie/przebudowę albo bezpieczny fallback. Budżet pamięci raportować dla 3k/16k/32k i gęstych kontaktów.
- [x] Zachowano cache otoczenia terenu i DDA dla bieżących końców. Nie wprowadzono nowego cache widoczności par, który wymagałby osobnego dowodu ważności po ruchu i edycji ścian.
- [x] Porównać kandydatów z niezależnym wyszukiwaniem wszystkich par na małych scenach, również po dużych korektach, przy narożnikach, jednopikselowych ścianach, usuwaniu/spawnie i zbieżnych pozycjach.

Gotowe, gdy: nie brakuje żadnej pary w zasięgu, nie giną ID, a obniżenie kosztu jest widoczne również po doliczeniu sortowania, list i kontroli marginesu. Mierzyć liczbę przebudów zamiast wpisywać stałe „1” jako warunek za wszelką cenę.

Raport wdrożenia: [I03_GPU_NEIGHBORS.md](I03_GPU_NEIGHBORS.md). Domyślnie sortowany widok stabilnych ID, cztery strumienie SoA i dwa indeksy na tick. Misja około 15% szybciej w kontrolowanym A/B; CTest 20/20, okna 4/6. Dwie ostatnie próby obu ścieżek utraciły czas przy spowolnieniu całego GPU; odbiór FPS pozostaje otwarty. Pełny raport opisuje koszty, fallback oraz ograniczenia. Kolejny zakres: I04.

### I04 — lokalne sprzężenie kapsuły i odzyskanie budżetu fizyki

- [x] Po analizie kodu wybrano kompakcję rzeczywistych reakcji w istniejących producentach zamiast osobnej listy komórek wokół kapsuły. Konserwatywny bieżący test AABB obejmuje jądro cieczy, a każdy nowy kontakt wpisuje własny kanał i ID. Nie ma opóźnionej listy geometrycznej ani utraty ważności po ruchu/korekcie. Dowód granicy 2N i kompletności opisuje raport I04.
- [x] Redukować reakcje tylko z tej listy; dla większej liczby kandydatów użyć hierarchicznej redukcji grup. Zachować przeciwną reakcję na materiał, podparcie, wyporność i opór.
- [x] Przetestować jedną redukcję na iterację po zebraniu reakcji PBF i kontaktów. Wymaga to osobnych/akumulowanych składników: obecne `terms[N+id]` jest nadpisywane przez kolejne przebiegi, więc samo skasowanie pierwszego `mission_dispatch(3)` utraci reakcję cieczy.
- [x] Porównać kolejność rozwiązywania i błąd sprzężenia przy mniejszej liczbie redukcji. Zachować wariant referencyjny wyłącznie do A/B aż przejdą testy pływania, chodzenia po piasku, podparcia i odrzutu.
- [x] Powtórzyć mission/16k/32k i trasę 30/60/144 FPS. Zamknąć raport celu <1,5 ms GPU i <0,1 ms CPU albo zapisać brakujący koszt oraz konkretny następny eksperyment.

Gotowe, gdy: koszt kapsuły skaluje się z otoczeniem, a testy nie wykazują utraty reakcji, przenikania, tonięcia/pływania bez związku z masą ani rozjeżdżania się sterowania.

Raport wdrożenia: [I04_GPU_COUPLING.md](I04_GPU_COUPLING.md). Domyślny `combined` stosuje jedną redukcję po PBF i kontakcie, z niezależnym kanałem PBF i hierarchią dla dużych partii. `local` zachowuje poprzednią kolejność do porównania. Wyniki benchmarku, pełne regresje i ograniczenia FPS zapisano w raporcie.

### I05 — jeden silnik, jeden zestaw zasobów, mniejsze Application

- [ ] Wydzielić współdzielone programy/zasoby GPU, transport wyników, harmonogram symulacji i renderer. Wprowadzać konkretne klasy według odpowiedzialności, bez rozbudowanego ogólnego frameworka.
- [ ] Zastąpić `gpu_world_` i `mission_world_` jednym właścicielem aktywnego świata oraz jawną konfiguracją sceny. Programy kompilować raz na kontekst/konfigurację.
- [ ] Misja i narzędzia laboratorium korzystają z tej samej aktywnej sceny. Przełączenie widoku lub powrót z menu nie resetuje jej po cichu; reset/zmiana sceny to jawne działanie.
- [ ] Wyprowadzić sekwencje smoke, zbieranie CSV i benchmarki z `Application::run()` do scenariuszy używających tego samego wejścia i kroku co gra.
- [ ] Zachować stary model CPU jako referencję/testy i narzędzie badawcze. Wycofanie starych pozycji menu wykonać po przeniesieniu potrzebnych funkcji; do tego czasu oznaczyć je jako referencyjne.

Gotowe, gdy: jeden komplet programów obsługuje grę i laboratorium, Application organizuje cykl życia i klatkę, a powrót między widokami nie gubi świata ani nie przyjmuje starych wyników.

### I06 — jedna implementacja reguł gry na CPU

- [ ] Utworzyć niezależną od modelu CPU/MAC sesję rozgrywki z kontraktem z sekcji 2. Nie przenosić starego `game::Session` wraz z jego własnym światem fizycznym.
- [ ] Kolejno migrować timery broni, amunicję/reload, cele/wynik i sterowanie. Fizyczne trafienia, ruch kapsuły oraz integracja pocisków/granatów pozostają na GPU. GPU może przechowywać potwierdzony stan prezentacyjny, ale nie drugie reguły naliczania.
- [ ] Zdarzenia trafień/destrukcji łączyć z tickiem, generacją i ID polecenia. Dodać potwierdzanie wykonania, deduplikację i ochronę przed wyczerpaniem kolejki; rezerwacja amunicji musi mieć zdefiniowany wynik przy odrzuceniu polecenia.
- [ ] Zapewnić, że komendy ze zbuforowanych wejść nie giną przy 144 FPS bez kroku fizyki ani nie wykonują się wielokrotnie przy kilku tickach na klatkę.
- [ ] Testować sztucznie opóźnione i porcjowane wyniki, jednoczesne wybuchy, reset w locie, stratę fokusu i przeciążenie. Zdarzenia krytyczne muszą być kompletne; HUD może pokazywać starszą próbkę.

Gotowe, gdy: reguły mają jednego właściciela, trasa misji i wynik są zgodne przy 30/60/144 FPS, CPU nie używa starego snapshotu do autorytatywnych kolizji, a zmiana nie pogarsza odczuwalnej reakcji sterowania.

### I07 — poprawa zachowania wody, bez równoczesnej wymiany solvera

- [ ] Porównać warianty liczby podkroków/iteracji przy stałym ticku 1/120 s, zaczynając od obecnych 2 podkroków × 8 iteracji PBF i 12 kontaktów ziaren. Oceniać błąd gęstości, tłumienie, penetracje, stabilność i całkowity czas, nie samą liczbę iteracji.
- [ ] Strojenie `s_corr`: bazowe 0,0001 oraz stopniowy sweep do większych wartości; oddzielne pomiary dla wody/oleju i skali kernela. Wartości 0,01–0,1 to kandydaci eksperymentalni, nie domyślny zakres produkcyjny.
- [ ] Dodać ograniczone vorticity confinement jako opcję jakości, z poprawnymi etapami obliczeń. Kontrolować wzrost energii, fale i bryzgi; nie maskować nim błędów objętości.
- [ ] Obecne próbki ścian o jednostkowej objętości zastąpić/przetestować ważeniem objętością zgodnym z metodą brzegową. Sprawdzać narożniki, menisk, cienkie kanały i sprzężenie kapsuły. Inspiracja: [Akinci i in. 2012](https://cg.informatik.uni-freiburg.de/publications/2012_SIGGRAPH_rigidFluidCoupling.pdf).
- [ ] Osobno porównać obecną kohezję `k=60` z modelem napięcia, krzywizny i adhezji inspirowanym [Akinci i in. 2013](https://cg.informatik.uni-freiburg.de/publications/2013_SIGGRAPHASIA_surfaceTensionAdhesion.pdf). Zadaptować kernele i jednostki do 2D; nie sumować bez kontroli dwóch modeli napięcia.
- [ ] Rozdzielczość 0,5 komórki badać dopiero po pomiarze zapasu. W 2D daje około 4× więcej nośników przy tym samym obszarze: zmienić masę/objętość nośnika, promienie, gęstość odniesienia i pojemność. Pełne obecne 32k mogłoby wymagać około 128k miejsc.

Gotowe, gdy: wybrany wariant ma lepszą jakość przy udokumentowanym koszcie i przechodzi sceny naczyń połączonych, przerwanej zapory, kropli oleju, warstw woda/olej, piasku w wodzie i postaci. APIC/FLIP pozostaje osobnym eksperymentem dopiero przy wykazanej słabości ulepszonego PBF.

### I08 — jawne przebiegi renderera i czytelna woda

Etap A: wydzielenie renderera i nieprzezroczystej tekstury sceny. Etap B: pole cieczy i kompozycja. Etap C: dodatki wizualne. Każdy etap porównujemy na tym samym zapisie symulacji.

- [ ] Zbudować niewielki graf/listę przebiegów z jawnymi wejściami, wyjściami, formatami i stanem GL. Wspólny tracker z unieważnieniem na granicy ImGui; nie usuwać odtwarzania stanu bez uzgodnionego kontraktu z backendem UI.
- [ ] Zastąpić terenową instancję na każdą komórkę teksturą materiału i jednym przebiegiem pełnoekranowym. Aktualizować z GPU po edycjach/destrukcji; nie kopiować mapy na CPU przy każdej klatce.
- [ ] Przygotować teksturę sceny: tło, teren, ziarna i właściwe warstwy aktorów. Woda kompozytuje się po obiektach zanurzonych; elementy przed wodą mają osobną warstwę/maskę.
- [ ] Splatować pole gęstości faz oraz ważonej prędkości. Zachować rozróżnienie wody i oleju: określić semantykę kanałów RGBA16F lub dodatkowych załączników. Normalizować prędkość przez wagę, nie używać surowej sumy jako prędkości płynu.
- [ ] Porównać niewielkie separowalne wygładzanie z wariantem bilateralnym; chronić cienkie strużki, krople i granicę olej/woda. Dodać miękką maskę brzegu, normalne z gradientu, połysk i rimlight.
- [ ] Wybrać stylizowaną grubość 2D. Dla kolumn obsługiwać osobne segmenty cieczy; dla JFA jawnie traktować wynik jako odległość do brzegu. Absorpcję Beer–Lamberta i alfa dobrać do tej definicji oraz palety.
- [ ] Dodać refrakcję ograniczoną maską cieczy i przeszkód, z bezpiecznym próbkowaniem krawędzi. Zanurzony piasek i aktor otrzymują odpowiednie podbarwienie/absorpcję.
- [ ] Dodać tanią pianę powierzchniową i stylizowane kaustyki maskowane obecnością wody i dna. Sama duża prędkość lub dywergencja nie wystarczy do rozpoznania grzbietów fal.
- [ ] Utrzymać zgodne rozmiary pikseli, kontrolę palety i dithering Bayera. Przy późniejszym HDR końcowa kwantyzacja następuje po tonemappingu.

Gotowe, gdy: zanurzenie jest czytelne, woda i olej nie zlewają się, nie ma refrakcji przez ściany ani znikających cienkich strużek, a raport zawiera koszt każdego etapu przy 960 × 576 oraz większym oknie. Cel 0,2–0,3 ms weryfikujemy, a przy przekroczeniu ustalamy poziomy jakości.

### I09 — wspólny system GPU VFX i cząstki wtórne

- [ ] Wydzielić GPU emit/update/compact/draw z pulą wolnych miejsc i rysowaniem indirect. Zdarzenia VFX wynikają z potwierdzonych zdarzeń świata; emisja nie wymaga odczytu wszystkich materiałów do CPU.
- [ ] Eksplozje: błysk, iskry, wizualne odłamki, krótkie smugi, dystorsja fali i osmalenia. Fizyczny gruz pozostaje w solverze; cząstki dekoracyjne nie tworzą dodatkowej masy świata.
- [ ] Wtórne cząstki wody: bryzgi balistyczne, piana niesiona polem prędkości i wizualne pęcherzyki. Zacząć od 16k, 64k dopuścić po pomiarze. Inspiracja: [Ihmsen i in. 2012 — strona publikacji autora](https://cg.informatik.uni-freiburg.de/people/ihmsen.htm).
- [ ] Źródła emisji oceniać na powierzchni: grzbiety/krzywizna, energia względnego ruchu i wskaźnik uwięzionego powietrza. To model wizualny; nie deklarować rzeczywistej symulacji bąbli bez odpowiedniego stanu fizycznego.
- [ ] Kolizje lokalnych VFX korzystają z pola terenu; kontrolować przeskakiwanie cienkich ścian przy dużej prędkości. Piana nie przepływa wizualnie przez osłony.
- [ ] Przepełnienie puli ogranicza emisję według priorytetu i jest raportowane. Wyłączenie VFX nie zmienia trajektorii, liczby ani masy cząstek fizycznych.

Gotowe, gdy: efekty obu dotychczasowych trybów korzystają z jednego systemu, masa świata nie zależy od jakości VFX, a wielokrotne eksplozje i pełna pula nie blokują gry. Budżet ustalamy z odzyskanego czasu po I04, osobno dla symulacji VFX i rysowania.

### I10 — termika i gazy na GPU, ogień połączony ze światem

Etap A: materiały i ciepło. Etap B: przepływ gazów. Etap C: reakcje i gaszenie. Etap D: render ognia/dymu. Każdy podetap ma osobne bilanse i pomiar.

- [ ] Ujednolicić identyfikatory materiałów, jednostki, masę, pojemność cieplną, entalpię faz, paliwo i temperatury zapłonu. Temperaturę wyprowadzać ze stanu energetycznego. Dawny CPU/MAC dostarcza testów praw fizycznych, nie wymaga identycznych trajektorii innego solvera.
- [ ] Dodać wymianę ciepła między terenem, cząstkami i gazem z parami źródło/ujście. Transport cząstka–siatka–cząstka nie może wielokrotnie dodawać tej samej energii.
- [ ] Zbudować pola prędkości gazu, tlenu, paliwa gazowego, dymu/pary i energii. Porównać przepływ 160 × 96 z 320 × 192; przeszkody o grubości jednej komórki nie mogą zniknąć przy redukcji rozdzielczości. Maski przepływu muszą respektować pełną topologię terenu.
- [ ] Ustalić solver projekcji, warunki brzegowe, adwekcję, dyfuzję i wyporność. Raportować dywergencję, utratę składników i energii; jawnie oddzielić wizualne uproszczenia od zachowawczego transportu wymaganych wielkości.
- [ ] Spalanie zużywa paliwo i tlen, wytwarza zadeklarowane produkty oraz ciepło. Rozróżnić płomień, gorący gaz, dym i żar; wygląd sam nie wywołuje reakcji. Gaśnięcie wynika z chłodzenia/niedoboru substratów.
- [ ] Przenieść potrzebne przemiany fazowe, w tym parowanie/skraplanie i pozostałości spalania. Źródła, ujścia i promieniowanie prowadzić w bilansie. Ich zakres wdrażać stopniowo, bez deklarowania zgodności wszystkich 19 materiałów przed testami.
- [ ] Scena odbiorcza: palący się olej/drewno, dym pod sufitem i przez otwór, ściana oddzielająca paliwo, gaszenie wodą, para i wyczerpanie tlenu w zamknięciu. Sprawdzić różne FPS i edycję terenu w trakcie pożaru.

Gotowe, gdy: ogień reaguje na rzeczywisty stan świata, osłony i wodę, błędy bilansów mają określone tolerancje i nie rosną bez kontroli, a koszt termiki/gazów mieści się w zrewidowanym budżecie. Nie wracamy do pełnego odczytu świata na CPU, by wykonać jeden brakujący etap reakcji.

### I11 — oświetlenie i dopracowanie stylu

- [ ] Dodać światła 2D i emisję materiałów: ogień, lawa, błysk lufy oraz eksplozja. Bufor światła zacząć od 320 × 192; cienie muszą korzystać z pola odległości o odpowiednim zasięgu lub sprawdzanego śledzenia przeszkód.
- [ ] Aktualizację pola cieni wiązać z edycją terenu. Sprawdzić jednopikselowe ściany, wąskie szczeliny i zniszczenie osłony; JFA porównać z dokładną maską, żeby kontrolować przecieki.
- [ ] Wprowadzić liniowy HDR RGBA16F, poprawne przejścia sRGB, kontrolowany bloom, tonemapping i LUT palety tła. ImGui i tekst HUD kompozytować po postprocessie świata.
- [ ] Winieta, wstrząs kamery i aberracja przy wybuchach mają osobne ustawienia siły/wyłączenia; nie mogą pogarszać czytelności celu ani tekstu.
- [ ] Piasek: paleta 3–4 odcieni, AO z gęstości usypu, smugi i pył przy uderzeniu. Ciemniejszy wygląd po kontakcie z wodą powiązać z trwałym parametrem wilgoci, jeśli ma utrzymywać się po wyjściu z wody.
- [ ] Rozdzielić wizualnie mokry piasek od jego mechaniki: kohezja wymaga osobnej zmiany solvera i testów usypywania, wypierania wody oraz bilansów. Nie uznawać zmiany koloru za wdrożenie spójnego mokrego materiału.
- [ ] Zapisać porównawcze obrazy/nagrania dnia, ciemnej sceny, pożaru, wybuchu i wody; osobno profilować światło, bloom i kompozycję.

Gotowe, gdy: styl pasuje do pikselowych teł, efekty nie zasłaniają rozgrywki, a poziomy jakości respektują zmierzony budżet. Radiance Cascades nie jest zależnością ukończenia tego etapu.

## 5. Eksperymenty po wykonaniu podstaw

| Eksperyment | Warunek rozpoczęcia | Kryterium decyzji |
| --- | --- | --- |
| APIC/FLIP dla cieczy, PBD dla ziaren | I07 wykazuje konkretny nierozwiązany problem jakości/skali PBF | Ten sam test wody/oleju, cienkich ścian, materiałów i kapsuły; porównać jakość, całkowity koszt transferów/ciśnienia i złożoność sprzężenia. Bez drugiego silnika gry. |
| Rozdzielczość cieczy 0,5 komórki | Zapewniony budżet po I04/I07 | Czterokrotnie większa liczba cząstek z poprawioną masą i promieniem; policzona pamięć, sąsiedzi i FPS. |
| Gruz w bryłach przez shape matching | Podstawowa destrukcja i VFX stabilne | Zachować masę i ID przy tworzeniu/rozpadzie klastrów; kontakt z kapsułą i cieczą, kolizje cienkich ścian, koszt gęstych stosów. Inspiracja: [Müller i in. 2005](https://matthias-research.github.io/pages/publications/MeshlessDeformations_SIG05.pdf). |
| Radiance Cascades | Światła bezpośrednie I11 mieszczą się w budżecie i potrzeba światła pośredniego jest widoczna | Mierzalna poprawa obrazu przy akceptowalnym koszcie i bez smug/opóźnień po destrukcji. Opcja jakości, nie fundament fizyki. |

## 6. Pokrycie punktów review

| Punkt | Miejsce wdrożenia |
| --- | --- |
| P1 — staging i odczyty | I00 diagnoza, I01 pierścień i możliwości GL |
| P2 — 21 przebudów na podkrok | I03 kompletne sąsiedztwo i kontrola marginesu |
| P3 — linked lists i AoS | I03 counting sort, zakresy, SoA/AoSoA |
| P4 — ubershadery | I02 programy per przebieg i rozmiary grup |
| P5 — kapsuła i seryjna rozgrywka | I04 lokalne reakcje; I06 reguły gry na CPU |
| P6 — bindowanie i pusta praca | I02 tracker wiązań i indirect |
| P7 — teren i odpytywanie stanu GL | I08 tekstura terenu i kontrakt stanu z ImGui |
| P8 — odpytywanie 64 timerów | I02 FIFO |
| P9 — ciężka fizyka CPU w grze | I05/I06 wspólna ścieżka runtime; I10 termika/gazy GPU |
| P10 — dwa ParticleWorld i kompilacje | I05 jeden właściciel i współdzielone zasoby |
| P11 — diagnostyka GL | I00 kontekst debug i KHR_debug |
| Small steps, s_corr, wirowość, adhezja i granice | I07 |
| Nowy pipeline wody | I08 |
| Cząstki wtórne i VFX | I09 |
| Ogień i dym związany z materiałami | I10 |
| Piasek, oświetlenie, HDR, bloom i paleta | I11 |
| APIC/FLIP, bryły gruzu, Radiance Cascades | Eksperymenty warunkowe w sekcji 5 |

## 7. Zasady odbioru i kontynuacji

- Każdy etap: najpierw pomiar przed zmianą, potem jedna grupa zmian, testy dotkniętych kontraktów i porównanie po zmianie. Pełny zestaw integracyjny przed wydaniem.
- Wydajność nie może opierać się na pominięciu materiału, cichym przepełnieniu list, usunięciu reakcji postaci lub opóźnionym snapshotcie używanym jako bieżący stan fizyczny.
- Kontrolować stabilne ID, masy materiałów, aktywne sloty, brak odradzania usuniętych cząstek, szczelność cienkich ścian, skalowanie czasu, utratę fokusu i reset. Termika dodatkowo wymaga bilansu energii i składników.
- Zachować testy tras 30/60/144 FPS. Nowe metody numeryczne oceniać tolerancjami i mierzalnym zachowaniem; brak zgodności bitowej między GPU nie jest sam w sobie błędem.
- Zmieniony etap ma instrukcję odtworzenia, sprzęt, surowe CSV oraz opis ograniczeń. Raportować także nieudane cele i eksperymenty odrzucone na podstawie pomiaru.
- Nie przebudowujemy całej aplikacji w jednym wydaniu. Po I04 ustalamy rzeczywisty odzyskany budżet; po I07 wybieramy parametry wody; po I10 ponownie bilansujemy koszt pełnego świata.

### Rejestr postępu

| Iteracja | Stan | Wynik / raport | Następny krok |
| --- | --- | --- | --- |
| Przygotowanie planu | Zakończone 2026-10-28 | Zweryfikowano kod i istniejące raporty 0.7.0; bez zmian runtime i bez nowych testów GPU | I00 |
| I00 | Instrumentacja ukończona; odbiór wydajności okna otwarty | [Raport I00](I00_GPU_PROFILING.md): liczniki, fazy CPU/GPU, 8 wariantów, 3 powtórzenia; test okna niestabilny również w starej wersji | I01 — transport wyników; osobno kontrola warunków GPU |
| I01 | Wdrożone, z ograniczeniami pomiarów FPS | [Raport I01](I01_GPU_READBACK.md): ring 3 sloty, mapowanie/fallback, generacje, frame-level poll, regresje 11/11, kontrolowane p95 CPU <0,1 ms | I02 — przebiegi i zlecanie pracy |
| I02 | Wdrożone 2026-10-28 | [Raport I02](I02_GPU_EXECUTION.md): 27 programów, tracker, komendy indirect, FIFO, regresje 12/12, okna 6/6; kontrolowane GPU misji około −7% | I03 — dane i kompletne sąsiedztwo |
| I03 | Wdrożone 2026-10-28; odbiór okna otwarty | [Raport I03](I03_GPU_NEIGHBORS.md): counting sort i prefix scan, stabilne ID, SoA, pełne zakresy po ruchu, 42/43 → 2 budowy indeksu; regresje 20/20, okna 4/6 | I04 — lokalne sprzężenie postaci |
| I04 | Wdrożone 2026-10-28; cel GPU otwarty | [Raport I04](I04_GPU_COUPLING.md): kompakcja reakcji, dwa kanały, hierarchia, konserwatywny zasięg próbek, regresje 21/21 i okna combined 3/3; 16k/32k około −29%/−33% GPU | I05 — jeden silnik i współdzielone zasoby |

Po każdej zakończonej iteracji dopisać: zakres i pliki, wynik testów, medianę/p95/p99 GPU i CPU, czas całej klatki, liczbę przebiegów/przebudów, pamięć, opóźnienie odczytów/zdarzeń, decyzje i pozostałe problemy. Zaktualizować status w sekcji 4.

### Dziennik decyzji

- **2026-10-28:** rozwijamy jeden aktywny świat GPU. CPU przejmuje reguły gry przez jawny kontrakt poleceń/zdarzeń; fizyka wymagająca natychmiastowego sprzężenia pozostaje na GPU. Starszy CPU/MAC służy jako referencja.
- **2026-10-28:** zaczynamy od pomiarów i kosztów stałych. Nie przyjmujemy automatycznie, że każdy odczyt opróżnia kolejkę GPU; obecny kod sprawdza fence.
- **2026-10-28:** cel jednej budowy sąsiedztwa na podkrok jest podporządkowany kompletności kandydatów. APIC/FLIP, gęstsza ciecz, bryły gruzu i światło pośrednie są eksperymentami warunkowymi.

- **2026-10-28, I00:** nie zakładamy, że sam okres HUD co 4 ticki usuwa opóźnienia. Eksperyment bez odczytów ograniczył p95 CPU; duży koszt występuje również w sprawdzaniu fence. Baza okna nieodebrana, nie deklarujemy poprawy FPS.

- **2026-10-28, I01:** samo trwałe mapowanie nie usuwa kosztu częstego sprawdzania fence. Produkcyjny HUD zbieramy na poziomie klatki; CPU tick zbiera tylko timery. Odrzucono GetSynciv po pomiarze regresji. Krytyczne zdarzenia z potwierdzeniami pozostają oddzielnym zakresem I06.

- **2026-10-28, I02:** zachowujemy grupy 256 dla cząstek i redukcji; programy skalarne mają jeden wątek. Grupy 64/128 nie wygrały w większych scenach. Zera indirect ograniczają pracę GPU, ale nie usuwają kosztu dispatch API. Bieżące okna przeszły bez utraty czasu; nie uznajemy tego za dowód naprawy historycznych spowolnień.

- **2026-10-28, I03:** ponownie używamy posortowanego indeksu komórek. Dynamiczny promień `R + Dmax` wokół aktualnego końca utrzymuje kompletność także po przekroczeniu stałego skin; koszt dużych rozszerzeń pozostaje jawny. Odrzucono sortowanie przed każdym zapytaniem jako wolniejsze. Wybrano SoA po pomiarze; stan ma stałe adresy ID, a sortowany widok własną mapę odwrotną. Cel 1,5 ms nadal wymaga I04.

- **2026-10-28, I04:** lokalność uzyskujemy przez zbieranie aktualnych reakcji w istniejących przebiegach, bez drugiej listy geometrycznej z marginesem ważności. Wybrano combined po pełnych testach misji i porównaniu kosztu z local. Niezależny kanał PBF zachowuje oba składniki; local pozostaje opcją zachowującą sekwencyjną kolejność. Natywny harness czeka na rzeczywisty proces Windows, a diagnostyka kolejki używa kopii staging poza czasem kroku.

## 8. Materiały pomocnicze

Kod i dane: [architektura 0.7](ARCHITECTURE.md), [misja GPU](GPU_MISSION_070.md), [przegląd fizyki 0.6.1](PHYSICS_REVIEW_061.md), [benchmark misji](gpu070/build/gpu-mission-benchmark.csv), [próbki okna](gpu070/build/mission-frames.csv), [obecne testy](TEST_RESULTS.txt).

Źródła pierwotne metod, do dalszego wdrażania:

- [KHR_debug — specyfikacja](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt).
- [Position Based Fluids — Macklin i Müller](https://mmacklin.com/pbf_sig_preprint.pdf).
- [Small Steps in Physics Simulation — Macklin i in.](https://mmacklin.com/smallsteps.pdf).
- [Versatile Rigid-Fluid Coupling for Incompressible SPH — Akinci i in.](https://cg.informatik.uni-freiburg.de/publications/2012_SIGGRAPH_rigidFluidCoupling.pdf).
- [Versatile Surface Tension and Adhesion for SPH Fluids — Akinci i in.](https://cg.informatik.uni-freiburg.de/publications/2013_SIGGRAPHASIA_surfaceTensionAdhesion.pdf).
- [Unified Spray, Foam and Bubbles — publikacje Ihmsena](https://cg.informatik.uni-freiburg.de/people/ihmsen.htm).
- [Real-Time Fluid Dynamics for Games — Stam](https://www.dgp.toronto.edu/people/stam/reality/Research/pdf/GDC03.pdf).
- [Meshless Deformations Based on Shape Matching — Müller i in.](https://matthias-research.github.io/pages/publications/MeshlessDeformations_SIG05.pdf).
