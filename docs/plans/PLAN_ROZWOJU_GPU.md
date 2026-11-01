# Plan rozwoju: wspólny świat GPU, wydajność i efekty

Data utworzenia: 2026-10-28. Aktualizacja: **2026-10-31**, po funkcjonalnym I10 i rozpisaniu jego domknięcia; uwzględnia review `v0.7.0-i04` (`6f318ea`). Punkt wyjścia: źródła Strefa Zero 0.7.0 w `outputs/strefa-zero`.
Status: **2026-10-31 — decyzją użytkownika I04.5 odłożone z niespełnionym celem 1,5 ms i otwartą przyczyną zmienności. G3-C i dalsze mikrooptymalizacje nie są kontynuowane. I05 wdrożone: jeden aktywny świat i 42 programy dla gry/narzędzi, leniwe CPU, cache opt-in, wspólne nagrania. Regresja Windows: 53/54, timeout okna misji wystąpił także na poprzedniej I04.5; szczegóły w raporcie I05; I06 wdrożone funkcjonalnie: sesja CPU i niezawodne komendy/zdarzenia; Windows 55/56 w pełnej serii + 4/4 po poprawce scenariusza timerów; p95 CPU 0,029–0,042 ms w końcowych direct, wcześniej 1,07–1,37 ms; stabilność wydajności otwarta. I07 wdrożone opcjonalnie: jakość 12/12, Windows 58/58, pełny balanced +0,237–0,304 ms/tick; legacy domyślne, budżet +0,25 ms nieodebrany. I08 wdrożone: A/B/C, default basic po 3 blokach ABBA, Windows 59/59 i GCC 8/8; szczegóły i ograniczenia w raporcie I08. I09 wdrożone funkcjonalnie: wspólny RenderDevice/VfxPool, 16k, Windows 61/61 + 10/10, GCC 9/9. Limit +0,25 ms nieodebrany (końcowe low do +0,678 ms); pomiary okna bez fokusu są pomocnicze. I10 wdrożone funkcjonalnie w zakresie A–D: Windows 64/64 + 10/10 + kontrole końcowe, GCC 10/10. Budżety 0,70/0,05 ms nieodebrane; termika domyślnie off. I11 wdrożone funkcjonalnie: Windows 67/68, GCC 11/11; low do +1.028 ms przy limicie 0,35 ms, odbiór aktywnego okna otwarty; domyślnie off.**

Historyczna baza przed I05: **52/52 w zmienionym protokole Q**; G1 miało 44/44, pierwotne Q 38/38. Stałe najniższe progi stosuje się do wspólnych checkpointów i skumulowanego defektu. To nie zaliczenie dawnej reguły niezależnych trajektorii: nadal jest raportowana i dostępna jawnie. Historyczne niepowodzenia pozostają w raportach.

To główny plan dalszych prac. Dokumenty `GPU_MIGRATION.md`, `GPU_LIQUIDS.md` i `GPU_MISSION_070.md` opisują etapy istniejącego wydania; ten dokument określa kierunek następnych zmian. Przy każdej iteracji aktualizujemy checklistę, tabelę wyników i dziennik decyzji poniżej. Nie zmieniamy historycznych raportów, żeby dopasować je do nowych założeń.

Uwagi z dostarczonego review zostały przypisane do etapów poniżej. [Rejestr rozstrzygnięć review](REVIEW_I04_ROZSTRZYGNIECIA.md) dokumentuje pokrycie, hipotezy i korekty propozycji; [oryginał review](reviews/review_strefa_zero_v0_7_0_i04_roadmap.md) pozostaje materiałem źródłowym. Polecenia zawarte w review są propozycjami do oceny, nie samodzielną zgodą na wykonanie zmian. Wyniki I00–I04 poniżej są historyczne. Wdrożenie A/F opisuje [raport I04.5-A/F](../reports/I04_5_GPU_STABILIZATION.md), warianty C opisuje [raport C](../reports/I04_5C_GPU_WORK.md), transport — [raport B](../reports/I04_5B_GPU_TRANSPORT.md), odczyty sąsiadów — [raport D](../reports/I04_5D_GPU_NEIGHBOR_READS.md), koszty przebiegów i Dmax — [raport E0](../reports/I04_5E0_GPU_COSTS.md), a zmianę metody odbioru sprzężenia i aktualne QA — [raport Q](../reports/I04_5Q_GPU_COUPLING_QA.md).

## 1. Historyczna ocena review bazowej wersji 0.7.0

Ta sekcja zachowuje audyt wykonany przed I00–I04. Tabela, liczby dispatchy, układ danych i numery linii opisują ówczesną wersję 0.7.0, nie bieżące I04. Wdrożone zmiany i aktualne ograniczenia są w raportach I00–I04 oraz rejestrze postępu; rozstrzygnięcia nowego review I04 są w osobnym rejestrze wskazanym na początku dokumentu.

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

Poniżej **istniejące** wyniki wydania 0.7.0 z [`gpu070/build/gpu-mission-benchmark.csv`](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu070/build/gpu-mission-benchmark.csv). Nie wykonywano nowego benchmarku przy pisaniu planu.

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

Po I04 punkt odniesienia stanowi [raport I04](../reports/I04_GPU_COUPLING.md): kontrolowana mediana `mission` około 2,97–3,09 ms/tick, CPU submit około 0,011 ms, 2 budowy indeksu/tick. Nie łączymy tych wyników z wolniejszymi seriami w jedną średnią. I04.5 musi odtworzyć bazę na bieżącym sprzęcie i skorelować czas z telemetrią tego samego przebiegu.

Robocze budżety rozszerzeń z nowego review służą do planowania, nie są zmierzonym zyskiem ani zgodą na przekroczenie budżetu klatki:

| Etap | Hipoteza budżetu na GTX 1650 | Sposób odbioru |
| --- | --- | --- |
| I04.5 | Mediana fizyki `mission` <1,50 ms/tick; p95 <2 ms | Pomiar przed/po oraz 6/6 prób okna bez utraty czasu; 60 FPS jako cel podstawowy, 120 FPS osobno jako cel rozszerzony |
| I05 | Mniej duplikacji zasobów i kosztu startu | Osobno pierwsza kompilacja, trafienie cache binarnego i zmiana widoku istniejącego świata; bez obietnicy zimnego startu <40 ms |
| I06 | Zaostrzony cel etapu: p95 CPU submit <0,10 ms/tick; logika CPU raportowana osobno | Koszt pełnego kontraktu komend/zdarzeń oraz kompletność wyników przy opóźnieniach |
| I07 | Przyrost około +0,15–0,25 ms/tick | Pełny koszt nowych przebiegów i kontrola jakości względem zamrożonej I06; I04.5 pozostaje odłożone |
| I08 | Woda około 0,20–0,30 ms/klatkę przy 960 × 576 | Osobno całkowity koszt wody i zmiana kosztu renderu terenu |
| I09 | VFX około 0,15–0,25 ms/klatkę | Aktualizacja + emisja + rysowanie, także przy pełnej puli |
| I10 | Około +0,45–0,70 ms/tick z aktywnymi gazami/ciepłem; cel <0,05 ms/tick przy braku pracy | Osobno podetapy A–D, sceny zimne i gorące oraz poprawność wykrywania aktywności |
| I11 | Światło + HDR + bloom około 0,25–0,35 ms/klatkę | Osobno przebudowa pola cieni po destrukcji; cała klatka p95 ≤16,67 ms, mediana <6,5 ms jako cel rozszerzony |

Nie sumujemy bezpośrednio wartości „na tick” i „na klatkę”. Przy 60 FPS i fizyce 120 Hz zwykle przypadają dwa ticki na klatkę; rzeczywisty rozkład i narzut prezentacji mierzymy w oknie. Po I04.5, I07 i I10 aktualizujemy budżety na podstawie pomiarów.

## 4. Kolejne iteracje

Każda iteracja kończy się działającą, porównywalną wersją, krótkim raportem i decyzją o następnej. Jeśli zakres jest za duży na jeden przegląd zmian, dzielimy go według poniższych podpunktów; nie łączymy optymalizacji modelu numerycznego z wymianą renderera w jednym pomiarze A/B.

| ID | Zakres | Zależności | Status |
| --- | --- | --- | --- |
| I00 | Pomiar przebiegów i diagnostyka | — | Instrumentacja [x]; baza okna otwarta |
| I01 | Transport wyników bez oczekiwania | I00 | [x]; ograniczenia FPS w raporcie |
| I02 | Wyspecjalizowane przebiegi i koszt GL | I00–I01 | [x] |
| I03 | Dane cząstek i kompletne sąsiedztwo | I02 | [x]; wydajność okna otwarta |
| I04 | Lokalne sprzężenie postaci | I03 | [x]; cele wydajności w raporcie |
| I04.5 | Diagnoza okna/pamięci, ograniczenie pustej pracy, kolor gruzu | I00–I04 | Odłożone 2026-10-30 decyzją użytkownika; cel 1,5 ms i przyczyna zmienności nieodebrane |
| I05 | Jeden silnik i odchudzenie Application | I01–I04; przejście mimo nieodebranego I04.5 zatwierdzone przez użytkownika 2026-10-30 | Wdrożone, regresja Windows 53/54; timeout okna misji także na poprzedniej I04.5; [raport](../reports/I05_SHARED_WORLD.md) |
| I06 | Jedna logika rozgrywki na CPU | I05 | Wdrożone funkcjonalnie; Windows 55/56 + końcowe 4/4; stabilność czasu CPU otwarta; [raport](../reports/I06_GAMEPLAY_PROTOCOL.md) |
| I07 | Jakość fizyki cieczy | I03–I06 | [x] opcjonalne modele; budżet nieodebrany |
| I08 | Renderer i wygląd wody | I05–I06; profil I07 | [x]; basic domyślne po pomiarach, [raport](../reports/I08_SCENE_RENDERER.md) |
| I09 | GPU VFX i wtórne cząstki | I08 | [x] funkcjonalne; budżet i odbiór okna otwarte, [raport](../reports/I09_GPU_VFX.md) |
| I10 | Termika, gazy i ogień | I05–I09 | Funkcjonalne A–D; budżet otwarty |
| I11 | Światło, HDR i końcowy styl | I08–I10 | Wdrożone funkcjonalnie; odbiór wydajności otwarty, domyślnie off; [raport](../reports/I11_LIGHTING.md) |

### I00 — odtworzenie pomiarów i wskazanie rzeczywistych kosztów

Cel: mieć wiarygodny punkt porównania i oddzielić koszt dispatchy, sąsiedztwa, kapsuły, odczytu oraz kolejki sterownika.

- [x] Dodać liczniki dispatchy, barier, przebudów siatki, aktualizacji list aktywnych, odczytów, transferowanych bajtów i zapytań GL; nie wykonywać nowych synchronicznych odczytów tylko dla tych liczników.
- [x] Podzielić pomiar CPU na `poll`, aktualizacje wejścia/buforów, zlecenia fizyki i staging. GPU mierzyć grupami przebiegów w trybie profilowania, kontrolując narzut samych zapytań.
- [x] Dodać przełączniki eksperymentalne: HUD readback wyłączony, readback rzadszy, samo sprzężenie kapsuły wyłączone, diagnostyka wyłączona. Warianty służą ustaleniu kosztów; nie są wersją fizyki przeznaczoną do wydania.
- [x] Włączyć kontekst debug, etykiety zasobów i grupy KHR_debug w buildach deweloperskich. Callback nie powinien wykonywać ciężkiej pracy; tryb synchroniczny tylko do diagnozy. Pomiary Release bez niego.
- [x] Uruchomić dotychczasowe testy CPU/GPU i okna; zapisać aktualny punkt odniesienia w nowym katalogu raportów. Kod 77 oznacza pominięcie GPU, nie sukces.

Gotowe, gdy: licznik odtwarza 204/208, raport rozdziela okno i benchmark bez limitu, a warianty A/B wskazują źródła opóźnień zamiast zakładać je z góry.

Raport wdrożenia: [I00_GPU_PROFILING.md](../reports/I00_GPU_PROFILING.md). Liczniki i testy pomiarów przeszły. Test okna wykazał zmienne osiągi także w starej wersji; nie uznajemy bazy FPS za odebraną i nie zmieniamy kryteriów testu, żeby to ukryć.

### I01 — pierścień odczytów i rozdzielenie telemetrii od zdarzeń

Pliki początkowe: `gpu/world.cpp`, `gpu/world.hpp`, `gpu/mission.hpp`.

- [x] Szybkie A/B: zmienić hint wyłącznie bufora staging na `GL_STREAM_READ`; zmierzyć wynik bez zakładania, że hint rozwiąże problem.
- [x] Wydzielić `ReadbackRing`: początkowo 3 sloty, osobny fence i numer generacji/ticka na slot. Nie używać ponownie slotu, którego GPU jeszcze nie zwolniło.
- [x] Preferowana ścieżka: ARB_buffer_storage/GL 4.4, mapowanie READ | PERSISTENT | COHERENT, kopia do wolnego slotu, fence po kopii, odczyt dopiero po gotowości. Zachować BUFFER_UPDATE_BARRIER przed kopiowaniem wyników zapisanych przez shadery. Dla wariantu niekoherentnego wymagana też bariera CLIENT_MAPPED_BUFFER przed fence. Reguły widoczności: [ARB_buffer_storage](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_buffer_storage.txt).
- [x] Zachować GL 4.3: pierścień staging, kopia i fence, odczyt/mapowanie gotowego slotu; bez oczekiwania z dodatnim timeoutem w pętli klatki. Przy pełnym pierścieniu HUD zachowuje ostatni obraz i pomija nową próbkę.
- [x] HUD publikować co 2 ticki; dodać wiek wyniku, zaległość i licznik pominiętych próbek. Oddzielić kontrakt telemetrii od niezawodnych zdarzeń: obecny pierścień obsługuje tylko HUD, implementacja kanału z potwierdzeniami należy do I06.
- [x] Zabezpieczyć reset, przełączenie sceny i zniszczenie świata przy odczycie w locie; stare wyniki nie mogą aktualizować nowego świata.

Gotowe, gdy: test pełnego pierścienia nie blokuje okna ani nie psuje danych, reset odrzuca stare generacje, obie ścieżki możliwości GL przechodzą testy, a porównanie A/B wskazuje faktyczny zysk CPU. Brak osiągnięcia 0,1 ms jest powodem dalszej diagnozy, nie usuwania potrzebnych danych.

Raport wdrożenia: [I01_GPU_READBACK.md](../reports/I01_GPU_READBACK.md). W kontrolowanym A/B p95 CPU ticka około 0,08–0,09 ms; osobno sprawdzono koszt poll i sumę CPU klatki. Regresje 11/11. Okresowe spowolnienie całego GPU nadal występuje we wszystkich backendach i nie jest uznane za naprawione.

### I02 — tańsze zlecanie pracy i wyspecjalizowane programy

- [x] Z jednego źródła/helperów kompilować osobne programy dla poszczególnych przebiegów przez `#define PASS_*` wstawiony po `#version`. Zachować wspólne ABI i logi kompilacji.
- [x] Porównać `local_size_x` 64/128/256 na docelowym GPU, w tym odpowiednio rozmiar redukcji i argumenty indirect. Wybrać konfigurację pomiarem, osobno dla rodzajów pracy.
- [x] Bufory stałe bindować raz dla harmonogramu świata; aktualizować tylko zmieniane wiązania ping-pong. Usunąć nadmiarowe `UseProgram(compute)` przed natychmiastowym wyborem innego programu.
- [x] Opisać zależności odczytu/zapisu przebiegów. Bariery dobierać do konsumenta: SSBO, indirect command, tekstura, kopia lub odczyt CPU. Usunąć tylko bariery bez zależności; `barrier()` nie synchronizuje różnych grup roboczych.
- [x] Dla pracy zależnej od aktywności generować argumenty indirect na GPU. Zero grup ogranicza pracę urządzenia, lecz pozostawia koszt wywołania API. Nie sterować wyłączaniem solvera ze spóźnionego licznika CPU.
- [x] Timery zbierać FIFO: tylko najstarszy oczekujący wynik w każdej kolejce; zatrzymać zbieranie na pierwszym niegotowym. Nie skanować obu tablic po 32 wpisy przy każdym `poll()`.
- [x] Ograniczyć przygotowanie aktywnych ID i terenu do rzeczywistych zmian, zachowując zgodność obu kopii ping-pong po edycji i brak odradzania usuniętych ID.

Gotowe, gdy: te same testy fizyki przechodzą, raport podaje spadek liczby wywołań i kosztów CPU/GPU oraz nie ma nowych błędów widoczności pamięci. Samo zwiększenie liczby programów nie jest uznawane za przyspieszenie.

Raport wdrożenia: [I02_GPU_EXECUTION.md](../reports/I02_GPU_EXECUTION.md). 27 programów, tracker wiązań, warunkowe komendy i FIFO. API w przykładowym ticku 2662 → 740; GPU misji około 3,66 → 3,40 ms w kontrolowanym A/B. Regresje 12/12 i okna 6/6. Pozostaje 204/208 dispatchy i 42/43 budowy siatki na tick; następny zakres to I03. Pędzel nadal używa pełnej ścieżki edycji, a gating przygotowania danych dotyczy ticka misji.

### I03 — uporządkowane dane i bezpieczne używanie sąsiedztwa ponownie

Etap A: counting sort / prefix scan / scatter aktywnych cząstek według komórek; zakresy `cell_start/count` zamiast list `head/link`. Etap B: rozdzielenie często używanych pozycji, prędkości i właściwości od historii renderowania/kompensacji; wybrać SoA lub AoSoA przez pomiar. Zachować stabilne ID, mapowanie ID → indeks i rezerwację wolnych miejsc.

- [x] Przy siatce jednokomórkowej i promieniu 2 zapytanie 5 × 5 można scalić w 5 zakresów wierszy tylko dla odpowiednio posortowanego układu i z przycięciem brzegów. Margines ruchu może zwiększyć okno; zakresy nadal wymagają filtrowania dystansu i widoczności.
- [x] Kompletne zakresy kandydatów ze sprawdzanym na GPU przemieszczeniem od anchorów. Zamiast niezmiennej listy par `R + skin` zapytanie wokół bieżącego końca ma promień `R + Dmax` drugiego końca. Obejmuje pełne korekty, kompensację, teren i kapsułę. Dla `2*Dmax > skin` bezpieczny fallback rozszerza zakresy zamiast używać nieważnej listy; pełny kontrakt i dowód w raporcie I03.
- [x] Indeks powstaje po predykcji raz na podkrok. Przed każdym użyciem kandydaci obejmują bieżące przemieszczenie; nie ma odczytu starych komórek według niechronionych nowych pozycji. Spawn, destrukcja i zmiana aktywnych ID unieważniają indeks przed użyciem. Liczniki z CPU oraz nagłówek GPU potwierdzają rzeczywiste budowy.
- [x] Nie ograniczać list stałym limitem bez obsługi przepełnienia. Dodać licznik, rozszerzenie/przebudowę albo bezpieczny fallback. Budżet pamięci raportować dla 3k/16k/32k i gęstych kontaktów.
- [x] Zachowano cache otoczenia terenu i DDA dla bieżących końców. Nie wprowadzono nowego cache widoczności par, który wymagałby osobnego dowodu ważności po ruchu i edycji ścian.
- [x] Porównać kandydatów z niezależnym wyszukiwaniem wszystkich par na małych scenach, również po dużych korektach, przy narożnikach, jednopikselowych ścianach, usuwaniu/spawnie i zbieżnych pozycjach.

Gotowe, gdy: nie brakuje żadnej pary w zasięgu, nie giną ID, a obniżenie kosztu jest widoczne również po doliczeniu sortowania, list i kontroli marginesu. Mierzyć liczbę przebudów zamiast wpisywać stałe „1” jako warunek za wszelką cenę.

Raport wdrożenia: [I03_GPU_NEIGHBORS.md](../reports/I03_GPU_NEIGHBORS.md). Domyślnie sortowany widok stabilnych ID, cztery strumienie SoA i dwa indeksy na tick. Misja około 15% szybciej w kontrolowanym A/B; CTest 20/20, okna 4/6. Dwie ostatnie próby obu ścieżek utraciły czas przy spowolnieniu całego GPU; odbiór FPS pozostaje otwarty. Pełny raport opisuje koszty, fallback oraz ograniczenia. Kolejny zakres: I04.

### I04 — lokalne sprzężenie kapsuły i odzyskanie budżetu fizyki

- [x] Po analizie kodu wybrano kompakcję rzeczywistych reakcji w istniejących producentach zamiast osobnej listy komórek wokół kapsuły. Konserwatywny bieżący test AABB obejmuje jądro cieczy, a każdy nowy kontakt wpisuje własny kanał i ID. Nie ma opóźnionej listy geometrycznej ani utraty ważności po ruchu/korekcie. Dowód granicy 2N i kompletności opisuje raport I04.
- [x] Redukować reakcje tylko z tej listy; dla większej liczby kandydatów użyć hierarchicznej redukcji grup. Zachować przeciwną reakcję na materiał, podparcie, wyporność i opór.
- [x] Przetestować jedną redukcję na iterację po zebraniu reakcji PBF i kontaktów. Wymaga to osobnych/akumulowanych składników: obecne `terms[N+id]` jest nadpisywane przez kolejne przebiegi, więc samo skasowanie pierwszego `mission_dispatch(3)` utraci reakcję cieczy.
- [x] Porównać kolejność rozwiązywania i błąd sprzężenia przy mniejszej liczbie redukcji. Zachować wariant referencyjny wyłącznie do A/B aż przejdą testy pływania, chodzenia po piasku, podparcia i odrzutu.
- [x] Powtórzyć mission/16k/32k i trasę 30/60/144 FPS. Zamknąć raport celu <1,5 ms GPU i <0,1 ms CPU albo zapisać brakujący koszt oraz konkretny następny eksperyment.

Gotowe, gdy: koszt kapsuły skaluje się z otoczeniem, a testy nie wykazują utraty reakcji, przenikania, tonięcia/pływania bez związku z masą ani rozjeżdżania się sterowania.

Raport wdrożenia: [I04_GPU_COUPLING.md](../reports/I04_GPU_COUPLING.md). Domyślny `combined` stosuje jedną redukcję po PBF i kontakcie, z niezależnym kanałem PBF i hierarchią dla dużych partii. `local` zachowuje poprzednią kolejność do porównania. Wyniki benchmarku, pełne regresje i ograniczenia FPS zapisano w raporcie.

### I04.5 — zamknięcie otwartych pomiarów i bezpieczne ograniczenie pracy

**Odłożone 2026-10-30 na wyraźną prośbę użytkownika.** Poniższe kryteria i eksperymenty zachowano historycznie. Brak odbioru wydajności nie blokuje już I05. Nie zmieniono tolerancji testów, ustawień solvera ani statusu osiągnięcia celu.

Priorytet P0. Najpierw I04.5-A, potem niezależne porównania B/C. Zmiany układu danych z D/E są warunkowe: wdrażamy je dopiero, jeśli profil potwierdzi koszt i prostsze zmiany nie zamkną celu. Nie zmieniamy w tym etapie równań fizyki ani tolerancji odbioru, aby poprawić wynik benchmarku.

- [x] **A — pomiar całej klatki:** dodać czas CPU `present()`, obsługi wejścia, ticków, renderu i pozostałej pracy. Zapisywać fokus/minimalizację, VSync, tryb/odświeżanie monitora, liczbę ticków/klatkę, zaległość zegara i utracony czas. Porównać `GL_TIME_ELAPSED` z parami `GL_TIMESTAMP` oraz wyłączonym profilowaniem; odczytywać wyniki dopiero po gotowości. Żadne wywołanie GL nie ma z góry gwarancji zerowego oczekiwania.
- [x] **A — seria diagnostyczna:** przeplot ABBA dla dwóch wariantów / Latin dla trzech, co najmniej 3 powtórzenia na wariant; benchmark oraz widoczne okno, także kontrola fokusu/VSync. Zestawić czasy z zegarami, mocą, temperaturą i pamięcią GPU. Osobno logować zwykły runtime i odczyty walidacyjne. Ocenić hipotezy limitu prezentacji, presji kolejki i migracji VIDEO → HOST. Wynik 2026-10-29: dawnego reżimu 30 FPS nie odtworzono; przyczyna historyczna niepotwierdzona i pozostaje otwarta, szczegóły w raporcie.
- [x] **B — badanie transportu i podziału pamięci:** odtworzono bazę QA bez zmiany limitów/tolerancji. Dodano trzy sloty UploadRing, ścieżki direct/staged/immutable/auto i immutable storage z flags=0 bez bezpośrednich zapisów CPU. Fallback staged używa GL 4.3. Pełne QA 28/28 oraz 36/36 procesów porównawczych; brak spójnej przewagi uzasadniającej zmianę domyślnego direct. Początkowa polityka odczytów obejmuje cały cykl od create(). Szczegóły i odrzucone hipotezy w raporcie B. Miejsce pamięci nadal wybiera sterownik; id=131186 liczono według faz, bez wyciszania callbacku.
- [x] **C — badanie pracy bez cieczy:** dodano eksperymentalną maskę 240 kafelków 16 × 16 i warunek cieczy przed próbkami ścian. Maska sprawdza pełny konserwatywny zakres z aktualnym przemieszczeniem, bez stałego halo; brak pewności zachowuje pełną ścieżkę. Ping-pong, reakcje i liczniki pozostają zachowane. `phase_gating` pozostaje domyślnie OFF, ponieważ pomiary nie wykazały pewnego przyspieszenia; wyniki QA w raporcie C.
- [x] **C — badanie projekcji terenu:** odrzucono pomijanie na podstawie samego `correction == 0` lub progu `1e-14`. Wariant opt-in używa lokalnego potwierdzenia braku penetracji i bitowej zgodności pozycji oraz kompensacji po kolejnym `move()`. Nie przenosi potwierdzenia między dispatchami ani edycjami terenu. `skip_zero_projection` pozostaje domyślnie OFF; wyniki QA w raporcie C.
- [x] **D — badanie odczytów i `sortedHot`:** porównano full/narrow/sorted w 18 procesach. Cache ma dwa aktualizowane banki pozycji/kompensacji/prędkości/masy/typu, bezpośredni gather według wpisu, mapę odwrotną dla producentów oraz unieważnianie przy edycji ID. Dodatkowe 2 MiB mieszczą się w istniejących 8 SSBO. 35/35 QA, po 88 audytów bitowych w pięciu testach cache; siedem okien bez utraty czasu. Narrow: około −2,2% mediany misji bez podobnego zysku 16k/32k; sorted spowalnia większe sceny. Domyślne full zachowane, szczegóły w raporcie D.
- [x] **E0 — profil kosztów i Dmax w czasie:** 16/16 procesów w porządku Latin i 6/6 kontroli wyłączonej sondy; 384 snapshoty, brak pominiętych geometrycznych par, kontrola outliera 96,2. W misji dominują gęstość/korekta PBF, przy 16k/32k kontakty ziaren. Dmax do 0,125/0,25/0,4375 w nagranych scenach; snapshoty eksplozji do około 50% więcej kandydatów niż świeży indeks. Jawnie zmierzono narzut; diagnostyka pozostaje OFF. Pełne QA 37/38 i znany błąd sprzężenia opisuje raport E0.
- [ ] **E — warunkowy lokalny `Dmax`:** odroczony po E0; profil nie uzasadnia obecnie dodatkowego indeksu lokalnego. Zachować globalny konserwatywny zakres. Wrócić po potwierdzeniu kosztu cięższych outlierów; potrzebny dowód kompletności także dla sąsiada przeniesionego z odległego kafelka, fallback i porównanie all-pairs przy eksplozji, przekroczeniu kafelków, resecie i destrukcji. Stałe halo 3 × 3 nie jest dowodem.
- [x] **F — kolor gruzu:** jawnie przypisano typowi 4 barwę zniszczalnej przegrody. Zachowano porównawcze obrazy ukończonej misji; wspólna ścieżka renderuje gruz ze strzałów i wybuchów, a tworzenie/bilanse gruzu po wybuchu nadal sprawdza `gpu.mission`.
- [x] **Q — wiarygodna weryfikacja sprzężenia:** wykazano odrzucenie identycznych referencji przez starą regułę (1/16). Wprowadzono odtwarzanie wspólnych stanów przed krokiem, stałe najniższe progi i skumulowane budżety, kontrolę rzeczywiście wyłączonego sprzężenia oraz niezależne sprawdzenie zastosowania reakcji do postaci. 12/12 powtórek, 1080 checkpointów, 12/12 odrzuconych usterek, pełne QA 38/38 w nowym protokole. Stare porównanie 90 kroków jest diagnostyką i nadal może nie przejść; solver bez zmian.
- [x] **G1 — niezależne grupy PBF:** porównano 64/128/256 przy ziarnach i redukcjach 256. Dodano oddzielny argument indirect (+12 B), test pustej/niepełnej grupy, nieciągłych ID i gęstości względem CPU. Pełne 44/44 QA, po 3 Q na konfigurację, 16/16 końcowych pomiarów, 8/8 okien. G 256/64 w misji 2,83006/2,82413 ms; różnica około 0,2% nie uzasadnia zmiany domyślnej, 128 wolniej. Sondy OFF. [Raport G1](../reports/I04_5G_GPU_PBF_GROUPS.md).
- [x] **G2 — kwalifikatory rozdzielonych banków PBF:** tylko korekta i XSPH. Kontrola przed GL, 128/128 odrzuconych aliasów/braków wiązań, odczyt rzeczywistych źródeł shaderów i dowód różnic wyłącznie w kwalifikatorach. 52/52 QA, 8/8 Q i usterek (w tym ON 64/128), 6/6 okien. Dwie serie 18/18, bez powtarzalnego przyspieszenia; OFF pozostaje domyślne. Wolniejszy i szybszy przebieg wystąpiły także na tym samym G1. [Raport G2](../reports/I04_5G2_GPU_PBF_RESTRICT.md).
- [x] **G3-A — narzędzia i ograniczona reprodukcja:** hashe zamrożonych EXE, ustalony plan, PID/argumenty/kody, read-only obserwacja okien/fokusu/AC/priorytetu i asynchroniczne liczniki silników GPU. 25/25 procesów + 2/2 kontrole początkowe, 22080 surowych wierszy CPU/liczników łącznie; 13/13 negatywnych kontroli analyzera. Bounds i dwa snapshots: 290976 wpisów Dmax, 384 rekonstrukcje, zero pominiętych par. Skoku do 12 ms nie odtworzono; pełnego dawnego preludium 52 testów nie powtarzano. To wynik ograniczonej próby, nie przypisanie przyczyny. [Raport G3-A](../reports/I04_5G3_GPU_VARIANCE.md).
- [x] **G3-B — kontrolowane odtworzenie wolnego reżimu:** jedno pełne preludium 52/52 QA i sześć dodatkowych Q na zamrożonych binariach. G1 przed 2,751/2,789 ms, cztery OFF po 11,288–11,422 ms; bounds/snapshots także wolne. 193984 wpisy Dmax, 192 rekonstrukcje bez pominiętych par; 16k/32k mają identyczne badane CSV/histogramy jak szybka G1. Po 142,69 s i w nowym supervisorze pierwsza ukryta próba jest już szybka, przed pokazaniem okna. Osobny 3×H/V/V/H i smoke nie przypisuje przyczyny; fokus 0/870. 21 profili/20160 surowych wierszy CPU, 9 negatywnych kontroli, 3 kontrole jednorazowego planu. [Raport G3-B](../reports/I04_5G3B_GPU_REPLAY.md).
- [ ] **G3-C — odłożone; historyczny zakres przypisania warunków wykonania:** rozdzielić przerwę czasową i nowy proces nadrzędny. Od początku zapisywać powody ograniczenia zegarów i dostępny stan power/QoS/priorytetu rodzica/dziecka. Po rzeczywistym spowolnieniu porównać OFF w tym samym supervisorze przed/po ustalonej przerwie, świeży supervisor i powrót do starego; osobny passes/OFF dla kosztów przebiegów. Wcześniej ustalić budżet i kryterium przechwycenia. Nie traktować samych 1395 MHz/20 W jako zdiagnozowanego limitu ani samego show-test-window jako fokusu. Zachować niezmieniony G1 i nie zmieniać ustawień systemu dla wyniku. Przyczyna całego G3 pozostaje otwarta; kolejne mikrooptymalizacje czekają na wiarygodne warunki.
- [ ] **Raport:** zapisać sprzęt/sterownik, dokładne komendy, surowe CSV/logi, liczniki pracy, pamięć, opóźnienia, wyniki wariantów i odrzucone hipotezy. Wykonać pełną regresję przed wydaniem, w tym mission/16k/32k, sprzężenie materiałów, fallback GL 4.3 oraz trasy 30/60/144 FPS.

Wdrożony i zweryfikowany zakres A/F: [raport A/F](../reports/I04_5_GPU_STABILIZATION.md). [Eksperyment C](../reports/I04_5C_GPU_WORK.md) pozostaje OFF. [Badanie B](../reports/I04_5B_GPU_TRANSPORT.md): 28/28 QA, 36/36 pomiarów, direct nadal domyślne. Ten sam build pracował w wolnym i szybkim reżimie w różnych sesjach; przyczyna zmienności nie jest zamknięta. [Badanie D](../reports/I04_5D_GPU_NEIGHBOR_READS.md): 35/35 QA, 18/18 pomiarów i 7 okien; full pozostaje domyślne. [E0](../reports/I04_5E0_GPU_COSTS.md) przypisało koszt i zbadało trajektorie: lokalny E odroczony, PBF wskazane do punktowego G. [Q](../reports/I04_5Q_GPU_COUPLING_QA.md) zmieniło niewiarygodną regułę odbioru: 38/38 w nowym protokole, bez zmiany solvera. [G1](../reports/I04_5G_GPU_PBF_GROUPS.md) zakończyło badanie grup: 44/44 QA i kompletne pomiary, bez powtarzalnego zysku; domyślne 256. [G2](../reports/I04_5G2_GPU_PBF_RESTRICT.md) zakończono: poprawność potwierdzona, wariant OFF domyślny, bez powtarzalnego zysku. [G3-A](../reports/I04_5G3_GPU_VARIANCE.md) dodało obserwację i zakończyło ograniczoną próbę bez odtworzenia 12 ms; [G3-B](../reports/I04_5G3B_GPU_REPLAY.md) odtworzyło wolny reżim po pełnym preludium i zapisało kompletną rekonstrukcję; przyczyna G3 nadal otwarta; proponowane G3-C odłożono decyzją użytkownika. Zmienność długich trajektorii pozostaje jawna. Pełny odbiór całego I04.5 pozostaje otwarty.

Gotowe, gdy: mediana `mission` <1,50 ms/tick na sprzęcie referencyjnym, 6/6 porównywalnych prób okna przechodzi bez utraty czasu, kolor gruzu jest poprawny, a masa, pary sąsiadów i reakcje pozostają kompletne. p95 fizyki <2 ms jest celem roboczym; 60 FPS weryfikujemy czasem całej klatki, 120 FPS raportujemy osobno. Brak migracji w logu nie zastępuje pomiaru stabilności. Cel nie został spełniony; 2026-10-30 użytkownik jawnie zdecydował o odłożeniu I04.5 i przejściu do I05.

**Nie przyjmujemy skrócenia `RetainedSteps` z 24 do 3 jako optymalizacji.** Zmianę polityki zegara można rozważyć osobno dopiero z testem przeciążenia i jawnym raportem utraty czasu. Ustawień sterownika/systemu nie zmieniamy po cichu; warianty środowiska są opisanymi warunkami pomiaru.

### I05 — jeden silnik, jeden zestaw zasobów, mniejsze Application

- [x] Wydzielić współdzielone programy/zasoby GPU, transport wyników, harmonogram symulacji i renderer. Wprowadzać konkretne klasy według odpowiedzialności, bez rozbudowanego ogólnego frameworka.
- [x] Zastąpić `gpu_world_` i `mission_world_` jednym właścicielem aktywnego świata oraz jawną konfiguracją sceny. Programy kompilować raz na kontekst/konfigurację.
- [x] Misja i narzędzia laboratorium korzystają z tej samej aktywnej sceny. Przełączenie widoku lub powrót z menu nie resetuje jej po cichu; reset/zmiana sceny to jawne działanie.
- [x] Wyprowadzić sekwencje smoke, zbieranie CSV i benchmarki z `Application::run()` do scenariuszy używających tego samego wejścia i kroku co gra.
- [x] Zachować stary model CPU jako referencję/testy i narzędzie badawcze. Wycofanie starych pozycji menu wykonać po przeniesieniu potrzebnych funkcji; do tego czasu oznaczyć je jako referencyjne.
- [x] Wydzielić `DeviceContext` (API/możliwości/zasoby), `ShaderLibrary`, `SimulationWorld` i `SceneRenderer`, z jawną własnością i czasem życia kontekstu. Istniejący `ReadbackRing` wykorzystać ponownie. Liczbę klas podporządkować odpowiedzialnościom, nie celowi liczby linii.
- [x] Dodać opcjonalny cache binarny programów: klucz obejmuje GPU/sterownik/wersję GL, rozwinięte źródła z include, definicje i format binarium. Sprawdzać link po wczytaniu, uszkodzony/niezgodny cache odrzucać i kompilować źródła. Raportować trafienia, chybienia i czasy osobno; cache nie przyspieszy pierwszej kompilacji na nowym sterowniku.
- [x] `GameSession`/`LaboratoryRunner` CPU tworzyć dopiero po wejściu do narzędzia referencyjnego. Start `--mission` nie uruchamia nieużywanych światów i pul wątków CPU. Zweryfikować wejście/wyjście oraz zamykanie aplikacji w obu ścieżkach.
- [x] Wspólna nakładka laboratorium: pędzel, pauza, pojedynczy krok i dostępne inspektory działają na żywej misji. Niedostępne pola termiki/gazów są oznaczone do I10; samo przełączenie narzędzi nie resetuje cząstek, celu, wyniku ani kolejki zdarzeń.
- [x] Wspólny zapis scenariusza/sesji, obrazów i CSV korzysta z produkcyjnego kroku oraz asynchronicznych odczytów; eksport nie wchodzi do czasu fizyki. Funkcje nagrywania CPU przenosić według potrzeb na wspólny świat, bez drugiego symulatora.
- [x] Dodać porządkowanie przenośności: zweryfikować i usunąć ostrzeżenia `-Wmisleading-indentation`, sprawdzić listę targetów IPO (w tym `strefa_clock_tests`), wykonać headless z `STREFA_WARNINGS_AS_ERRORS=ON` na dostępnych GCC/Clang/MSVC. Nie wyciszać kategorii ostrzeżeń, aby uzyskać sukces.

[Raport wdrożenia i walidacji](../reports/I05_SHARED_WORLD.md). Weryfikacja Clang 18 z dostępną libstdc++ 13 jest ograniczona brakiem `std::expected`; nie wyciszono ostrzeżeń ani nie dodano obejść standardowej biblioteki.

Gotowe, gdy: jeden komplet programów obsługuje grę i laboratorium, Application organizuje cykl życia i klatkę, a powrót między widokami nie gubi świata ani nie przyjmuje starych wyników.

### I06 — jedna implementacja reguł gry na CPU

- [x] Utworzyć niezależną od modelu CPU/MAC sesję rozgrywki z kontraktem z sekcji 2. Nie przenosić starego `game::Session` wraz z jego własnym światem fizycznym.
- [x] Kolejno migrować timery broni, amunicję/reload, cele/wynik i sterowanie. Fizyczne trafienia, ruch kapsuły oraz integracja pocisków/granatów pozostają na GPU. GPU może przechowywać potwierdzony stan prezentacyjny, ale nie drugie reguły naliczania.
- [x] Zdarzenia trafień/destrukcji łączyć z tickiem, generacją i ID polecenia. Dodać potwierdzanie wykonania, deduplikację i ochronę przed wyczerpaniem kolejki; rezerwacja amunicji musi mieć zdefiniowany wynik przy odrzuceniu polecenia.
- [x] Zapewnić, że komendy ze zbuforowanych wejść nie giną przy 144 FPS bez kroku fizyki ani nie wykonują się wielokrotnie przy kilku tickach na klatkę.
- [x] Testować sztucznie opóźnione i porcjowane wyniki, jednoczesne wybuchy, reset w locie, stratę fokusu i przeciążenie. Zdarzenia krytyczne muszą być kompletne; HUD może pokazywać starszą próbkę.
- [x] Zdefiniować `WorldCommand`/`WorldEvent` oraz ABI C++/GLSL: generacja, docelowy/wykonany tick, ID komendy/zdarzenia, rodzaj, podmiot i wymagany payload. 32 B i align 16 z review to punkt startowy; zweryfikować offsety/refleksję, komplet danych trafienia, zawijanie liczników i kolejność publikacji zapisów.
- [x] Rozpisać `CommandRing`/`EventRing`: kursory zapisu/ack, rezerwacja miejsca, bariery/fence, deduplikacja oraz ograniczenie zlecania przy przeciążeniu. Początkowe 1024 zdarzenia wymiarować według maksymalnej liczby zdarzeń na tick i opóźnienia, nie liczby ticków. Reset unieważnia komendy i rozlicza rezerwacje amunicji.
- [x] Dodać wspólne cooldowny karabinu/granatu oraz rezerwację amunicji ze zwrotem przy odrzuceniu. Reguły nie zależą od częstotliwości wywołania UI; wykonanie i aktualny początek pocisku pozostają na GPU.
- [x] Dodać strojoną tolerancję skoku: coyote time 80–90 ms i jump buffer 100 ms jako wartości początkowe. Okna wejścia są numerowane tickiem i zużywane jednokrotnie; aktualne podparcie i wykonanie skoku sprawdza GPU. Opóźniony HUD nie autoryzuje skoku. Przetestować lądowanie, zbocza, ruchomy piasek, utratę fokusu i 3 klatki opóźnienia.
- [x] Usunąć odcięcie plecaka/granatu po wyjściu kursora poza arenę przy aktywnym oknie; zdefiniować clamp/przeliczenie celu i respektować przechwycenie wejścia przez UI. Utrata fokusu nadal bezpiecznie zeruje sterowanie.
- [x] Ujednolicić `SoldierPose`/IK i warstwę prezentacji postaci, bez przenoszenia autorytatywnej kolizji na interpolowaną pozycję CPU. HP celów i potwierdzone trafienia udostępnić rendererowi I08/VFX I09.

Gotowe, gdy: reguły mają jednego właściciela, trasa misji i wynik są zgodne przy 30/60/144 FPS, CPU nie używa starego snapshotu do autorytatywnych kolizji, a zmiana nie pogarsza odczuwalnej reakcji sterowania.

### I07 — poprawa zachowania wody, bez równoczesnej wymiany solvera

Stan 2026-10-30: modele opcjonalne wdrożone i sprawdzone; legacy pozostaje domyślne, budżet +0,25 ms/tick nieodebrany. [Wyniki I07](../reports/I07_FLUID_QUALITY.md).

- [x] Porównać warianty liczby podkroków/iteracji przy stałym ticku 1/120 s, zaczynając od obecnych 2 podkroków × 8 iteracji PBF i 12 kontaktów ziaren. Oceniać błąd gęstości, tłumienie, penetracje, stabilność i całkowity czas, nie samą liczbę iteracji.
- [x] Strojenie `s_corr`: bazowe 0,0001 oraz stopniowy sweep do większych wartości; oddzielne pomiary dla wody/oleju i skali kernela. Wartości 0,01–0,1 to kandydaci eksperymentalni, nie domyślny zakres produkcyjny.
- [x] Dodać ograniczone vorticity confinement jako opcję jakości, z poprawnymi etapami obliczeń. Kontrolować wzrost energii, fale i bryzgi; nie maskować nim błędów objętości.
- [x] Obecne próbki ścian o jednostkowej objętości zastąpić/przetestować ważeniem objętością zgodnym z metodą brzegową. Sprawdzać narożniki, menisk, cienkie kanały i sprzężenie kapsuły. Inspiracja: [Akinci i in. 2012](https://cg.informatik.uni-freiburg.de/publications/2012_SIGGRAPH_rigidFluidCoupling.pdf).
- [x] Osobno porównać obecną kohezję `k=60` z modelem napięcia, krzywizny i adhezji inspirowanym [Akinci i in. 2013](https://cg.informatik.uni-freiburg.de/publications/2013_SIGGRAPHASIA_surfaceTensionAdhesion.pdf). Zadaptować kernele i jednostki do 2D; nie sumować bez kontroli dwóch modeli napięcia.
- **Odłożone poza I07:** rozdzielczość 0,5 komórki pozostawić jako eksperyment po odbiorze I08–I11 i pomiarze zapasu. W 2D daje około 4× więcej nośników przy tym samym obszarze: zmienić masę/objętość nośnika, promienie, gęstość odniesienia i pojemność. Pełne obecne 32k mogłoby wymagać około 128k miejsc.
- [x] Objętości próbek brzegu prekomputować po zmianie terenu; osobno skalibrować próbki kapsuły. Pakowanie fp16/flag uzgodnić z wszystkimi konsumentami terenu. Zweryfikować jednostki i znormalizowane kernele 2D niezależnym obliczeniem referencyjnym przed wdrożeniem wzorów z review.
- [x] Zacząć sweep `s_corr` od {0,0001; 0,0005; 0,002; 0,008} przy jawnym `h`, `delta_q` i wykładniku. Raportować amplitudę powierzchni po 600 tickach, błąd gęstości, pęd, energię i koszt. Próg oscylacji 0,05% komórki z review jest propozycją do kalibracji, nie wynikiem istniejącego testu.
- [x] Wirość wdrożyć jako policzenie pola omega, barierę i ograniczony impuls z poprawnie wyprowadzonym iloczynem wektorowym 2D. Sprawdzić zerowe wzbudzenie spoczynku i kierunek wiru; odrzucić błędną macierz podaną w review. Każde ulepszenie granic/napięcia/wirości ma osobne A/B i możliwość wyłączenia do diagnozy.

Gotowe, gdy: wybrany wariant ma lepszą jakość przy udokumentowanym koszcie i przechodzi sceny naczyń połączonych, przerwanej zapory, kropli oleju, warstw woda/olej, piasku w wodzie i postaci. APIC/FLIP pozostaje osobnym eksperymentem dopiero przy wykazanej słabości ulepszonego PBF.

### I08 — jawne przebiegi renderera i czytelna woda

Etap A: wydzielenie renderera i nieprzezroczystej tekstury sceny. Etap B: pole cieczy i kompozycja. Etap C: dodatki wizualne. Każdy etap porównujemy na tym samym zapisie symulacji.

- [x] Zbudować niewielki graf/listę przebiegów z jawnymi wejściami, wyjściami, formatami i stanem GL. Wspólny tracker z unieważnieniem na granicy ImGui; nie usuwać odtwarzania stanu bez uzgodnionego kontraktu z backendem UI.
- [x] Zastąpić terenową instancję na każdą komórkę teksturą materiału i jednym przebiegiem pełnoekranowym. Aktualizować z GPU po edycjach/destrukcji; nie kopiować mapy na CPU przy każdej klatce.
- [x] Przygotować teksturę sceny: tło, teren, ziarna i właściwe warstwy aktorów. Woda kompozytuje się po obiektach zanurzonych; elementy przed wodą mają osobną warstwę/maskę.
- [x] Splatować pole gęstości faz oraz ważonej prędkości. Zachować rozróżnienie wody i oleju: określić semantykę kanałów RGBA16F lub dodatkowych załączników. Normalizować prędkość przez wagę, nie używać surowej sumy jako prędkości płynu.
- [x] Porównać niewielkie separowalne wygładzanie z wariantem bilateralnym; chronić cienkie strużki, krople i granicę olej/woda. Dodać miękką maskę brzegu, normalne z gradientu, połysk i rimlight.
- [x] Wybrać stylizowaną grubość 2D. Dla kolumn obsługiwać osobne segmenty cieczy; dla JFA jawnie traktować wynik jako odległość do brzegu. Absorpcję Beer–Lamberta i alfa dobrać do tej definicji oraz palety.
- [x] Dodać refrakcję ograniczoną maską cieczy i przeszkód, z bezpiecznym próbkowaniem krawędzi. Zanurzony piasek i aktor otrzymują odpowiednie podbarwienie/absorpcję.
- [x] Dodać tanią pianę powierzchniową i stylizowane kaustyki maskowane obecnością wody i dna. Sama duża prędkość lub dywergencja nie wystarczy do rozpoznania grzbietów fal.
- [x] Utrzymać zgodne rozmiary pikseli, kontrolę palety i dithering Bayera. Przy późniejszym HDR końcowa kwantyzacja następuje po tonemappingu.
- [x] Dla tekstury terenu zacząć od R16UI 320 × 192; jawnie rozdzielić materiał, HP/pęknięcia i resztę stanu. Format i pakowanie zweryfikować po I10, zamiast nazywać teksturę „VAO”.
- [x] Refrakcja sprawdza cały odcinek do przesuniętej próbki względem maski terenu, nie tylko jego koniec. Kandydat ma pozostawać w odpowiednim obszarze cieczy i nie przechodzić przez ścianę; w razie niepewności użyć próbki bez przesunięcia. Sprawdzić ściany 1px, narożniki i oddzielone zbiorniki.
- [x] Fresnel, rimlight i stylizowane kaustyki dodawać po czytelnej bazowej absorpcji. Tarcze otrzymują czytelny znak, słupek/obrys i pozostałe HP; potwierdzone trafienie daje krótką informację wizualną, rozwijaną wspólnie z I09.

Gotowe, gdy: zanurzenie jest czytelne, woda i olej nie zlewają się, nie ma refrakcji przez ściany ani znikających cienkich strużek, a raport zawiera koszt każdego etapu przy 960 × 576 oraz większym oknie. Cel 0,2–0,3 ms weryfikujemy, a przy przekroczeniu ustalamy poziomy jakości.

### I09 — wspólny system GPU VFX i cząstki wtórne

- [x] Wydzielić GPU emit/update/compact/draw z pulą wolnych miejsc i rysowaniem indirect. Zdarzenia VFX wynikają z potwierdzonych zdarzeń świata; emisja nie wymaga odczytu wszystkich materiałów do CPU.
- [x] Eksplozje: błysk, iskry, wizualne odłamki, krótkie smugi, dystorsja fali i osmalenia. Fizyczny gruz pozostaje w solverze; cząstki dekoracyjne nie tworzą dodatkowej masy świata.
- [x] Wtórne cząstki wody: bryzgi balistyczne, piana niesiona polem prędkości i wizualne pęcherzyki. Zacząć od 16k, 64k dopuścić po pomiarze. Inspiracja: [Ihmsen i in. 2012 — strona publikacji autora](https://cg.informatik.uni-freiburg.de/people/ihmsen.htm).
- [x] Źródła emisji oceniać na powierzchni: grzbiety/krzywizna, energia względnego ruchu i wskaźnik uwięzionego powietrza. To model wizualny; nie deklarować rzeczywistej symulacji bąbli bez odpowiedniego stanu fizycznego.
- [x] Kolizje lokalnych VFX korzystają z pola terenu; kontrolować przeskakiwanie cienkich ścian przy dużej prędkości. Piana nie przepływa wizualnie przez osłony.
- [x] Przepełnienie puli ogranicza emisję według priorytetu i jest raportowane. Wyłączenie VFX nie zmienia trajektorii, liczby ani masy cząstek fizycznych.
- [x] `VfxPool` zaczyna od 16 384 slotów, wariant 65 536 wymaga pomiaru pamięci i kosztu. Rozpisać priorytety emisji i przejścia spray/foam/bubbles według gęstości pola oraz jego znormalizowanej prędkości; progi 0,15/0,75 i histereza 0,05 dotyczą znormalizowanego pokrycia. Przyjęto 48 B/slot, z poprzednią pozycją do interpolacji i pełnym czasem życia/kolorem/typem; same cząstki zajmują 768 KiB.
- [x] Emiter GPU ma własny kursor konsumpcji zdarzeń lub osobną kolejkę emisji; odczyt VFX nie potwierdza zdarzeń w imieniu `GameplaySession`. Brak zasobów dekoracyjnych nigdy nie blokuje krytycznych ack/trafień.
- [x] Bramka A/B: VFX wyłączone/niskie/wysokie przy tych samych komendach mają tę samą masę, trajektorie fizyczne i wynik misji. To korekta odwróconej bramki w tabeli review.

Funkcjonalnie wdrożone w misji GPU, laboratorium i poligonie CPU GL 4.3; fallback GL 3.3 zachowany. Szczegóły i dowody: [raport I09](../reports/I09_GPU_VFX.md). Low/high używają 16k; low przy dużej liczbie wtórnych cząstek wody rysuje stabilną próbkę około 8192 markerów, aktualizując wszystkie sloty.

- [ ] Odbiór wydajności: każdy segment low przy 960 × 576, dwie aktualizacje GPU/klatkę, dodatkowy koszt ≤0,25 ms. Końcowa macierz ma najgorszy przyrost +0,678344 ms; limit pozostaje niespełniony.
- [ ] Odbiór aktywnego okna: próby kończą się bez utraty czasu, lecz środowisko raportuje brak fokusu; ich p95 nie kwalifikuje aktywnego okna.


### I10 — termika i gazy na GPU, ogień połączony ze światem

Funkcjonalne A–D, 2026-10-31. [Raport, zakres materiałów i dane](../reports/I10_GPU_THERMAL.md). Etap A: materiały i ciepło. Etap B: przepływ gazów. Etap C: reakcje i gaszenie. Etap D: render ognia/dymu. Każdy podetap ma osobne bilanse i pomiar. Domyślnie off z powodu nieodebranego budżetu.

**Plan domknięcia:** [I10-E0–E4 — koszt integracji, aktywne obszary i odbiór](PLAN_I10_DOMKNIECIE.md). Kolejność: utrwalenie bazy i rozbicie kosztu → warunkowe przebudowy puli → aktywność ciepła/reakcji → transport i projekcja gazu → pełne QA, ABBA i aktywne okno. Etapy są zaplanowane; nie zmieniają wyników A–D ani statusu nieodebranych budżetów.

- [x] Ujednolicić identyfikatory materiałów, jednostki, masę, pojemność cieplną, entalpię faz, paliwo i temperatury zapłonu. Temperaturę wyprowadzać ze stanu energetycznego. Dawny CPU/MAC dostarcza testów praw fizycznych, nie wymaga identycznych trajektorii innego solvera.
- [x] Dodać wymianę ciepła między terenem, cząstkami i gazem z parami źródło/ujście. Transport cząstka–siatka–cząstka nie może wielokrotnie dodawać tej samej energii.
- [x] Zbudować pola prędkości gazu, tlenu, paliwa gazowego, dymu/pary i energii. Porównać przepływ 160 × 96 z 320 × 192; przeszkody o grubości jednej komórki nie mogą zniknąć przy redukcji rozdzielczości. Maski przepływu muszą respektować pełną topologię terenu.
- [x] Ustalić solver projekcji, warunki brzegowe, adwekcję, dyfuzję i wyporność. Raportować dywergencję, utratę składników i energii; jawnie oddzielić wizualne uproszczenia od zachowawczego transportu wymaganych wielkości.
- [x] Spalanie zużywa paliwo i tlen, wytwarza zadeklarowane produkty oraz ciepło. Rozróżnić płomień, gorący gaz, dym i żar; wygląd sam nie wywołuje reakcji. Gaśnięcie wynika z chłodzenia/niedoboru substratów.
- [x] Przenieść potrzebne przemiany fazowe, w tym parowanie/skraplanie i pozostałości spalania. Źródła, ujścia i promieniowanie prowadzić w bilansie. Ich zakres wdrażać stopniowo, bez deklarowania zgodności wszystkich 19 materiałów przed testami.
- [x] Scena odbiorcza: palący się olej/drewno, dym pod sufitem i przez otwór, ściana oddzielająca paliwo, gaszenie wodą, para i wyczerpanie tlenu w zamknięciu. Sprawdzić różne FPS i edycję terenu w trakcie pożaru.
- [x] **A — entalpia:** jawne H cząstek/terenu i funkcja T(H, materiał), z masą i ciepłem utajonym. Zachować spójną konwencję jednostek istniejącego `material.hpp`; nie kopiować temperatur i stałych dla ogólnego „metalu” bez decyzji materiałowej. Sprawdzić lód↔woda↔para oraz lawa→kamień.
- [x] **A — wymiana ciepła:** odczyty ze wspólnego stanu wejściowego, symetryczne źródło/ujście i osobny zapis. Dla cząstka↔gaz porównać akumulację stałoprzecinkową; skala 2^10 z review wymaga analizy zakresu, overflow, zaokrągleń i łącznego poboru z komórki przez wiele cząstek. Testować także teren↔gaz i nieruchomą izolowaną parę.
- [x] **B — gazy:** wariant bazowy MAC 320 × 192; 160 × 96 dopuszczać tylko po dowodzie zachowania pełnej topologii przeszkód. Zamknięta krawędź ma zerowy przepływ, jeśli którakolwiek strona jest ścianą. Uszczelnienie przez upakowany piasek wymaga osobnego modelu porowatości i testu, nie samego wskaźnika gęstości.
- [x] **C — reakcje:** poprawnie odjąć spalone paliwo, zużyć tlen, dodać zadeklarowane produkty i ciepło; szybkości mają spójne jednostki i zależność od dt. Ustalić obsługę nośnika/inertnych produktów, aby bilans obejmował całą masę gazu. Zaadaptować model ze źródeł/testów CPU zamiast uszkodzonego równania spalania z review.
- [x] **D — odbiór i narzędzia:** mapy temperatury/ciśnienia/prędkości/tlenu/paliwa i inspektor na wspólnym świecie. Prowadzić matrycę funkcji/materialów CPU→GPU z osobnym statusem: olej/drewno/ogień, dym/para, lód/lawa/metal, kwas/korozja, proch/termit/nitrogliceryna i detonacje. Każda deklarowana funkcja ma scenę i bilans; I10 nie oznacza automatycznego przeniesienia wszystkich 19 materiałów.
- [x] Narzędzia grzania/chłodzenia przekazują numerowane polecenia energii o jednostkach i dawce zależnych od ticka, nie FPS UI. Uwzględnić je jako jawne źródła/ujścia w bilansie. Matryca funkcji obejmuje też falę ciśnienia/akustykę: przenieść z testem albo jawnie pozostawić poza zakresem.

Zachowana twarda tolerancja odbioru I10: względny błąd całkowitej masy i entalpii <0,1% w zamkniętej komorze przez 600 ticków, bez zewnętrznych źródeł/ujść; dla spalania bilans zawiera energię chemiczną. Raportować również błąd bezwzględny, dłuższy przebieg i tempo dryfu. Zmiana tolerancji wymaga jawnej decyzji z uzasadnieniem, nie dopasowania po wyniku.

- [ ] Budżet ≤0,70 ms/tick w scenach gorących i <0,05 ms/tick przy braku aktywnej pracy: końcowo do +9,716552 / +3,382032 ms. Bez zmiany limitów; zimny włączony moduł nadal wykonuje pełną siatkę.
- [ ] Wykrywanie aktywnych obszarów i pomijanie pracy zimnego świata z testem zachowania bilansów; realizacja według [planu E0–E4](PLAN_I10_DOMKNIECIE.md), z osobnym kontraktem projekcji ciśnienia i spójności banków.
- [ ] Odbiór całej klatki aktywnego okna; timestampy bez prezentacji nie kwalifikują p95 okna.

Matryca materiałów jest częściowa zgodnie z zakresem etapu: reakcje oleju/drewna/gazu, fazy wody/lodu/lawy/metalu; kwas, węgiel, termit/proch/nitrogliceryna i akustyka pozostają poza zakresem. 160 × 96 odrzucono testem topologii; nie deklarujemy pełnego gaz–ciecz ani porowatości.

Pełny odbiór, gdy: ogień reaguje na rzeczywisty stan świata, osłony i wodę, błędy bilansów mają określone tolerancje i nie rosną bez kontroli, a koszt termiki/gazów mieści się w zrewidowanym budżecie. Nie wracamy do pełnego odczytu świata na CPU, by wykonać jeden brakujący etap reakcji.

### I11 — oświetlenie i dopracowanie stylu

- [x] Dodać światła 2D i emisję materiałów: ogień, lawa, błysk lufy oraz eksplozja. Bufor światła zacząć od 320 × 192; cienie muszą korzystać z pola odległości o odpowiednim zasięgu lub sprawdzanego śledzenia przeszkód.
- [x] Aktualizację pola cieni wiązać z edycją terenu. Sprawdzić jednopikselowe ściany, wąskie szczeliny i zniszczenie osłony; JFA porównać z dokładną maską, żeby kontrolować przecieki.
- [x] Wprowadzić liniowy HDR RGBA16F, poprawne przejścia sRGB, kontrolowany bloom, tonemapping i LUT palety tła. ImGui i tekst HUD kompozytować po postprocessie świata.
- [x] Winieta, wstrząs kamery i aberracja przy wybuchach mają osobne ustawienia siły/wyłączenia; nie mogą pogarszać czytelności celu ani tekstu.
- [x] Piasek: paleta 3–4 odcieni, AO z gęstości usypu, smugi i pył przy uderzeniu. Ciemniejszy wygląd po kontakcie z wodą powiązać z trwałym parametrem wilgoci, jeśli ma utrzymywać się po wyjściu z wody.
- [x] Rozdzielić wizualnie mokry piasek od jego mechaniki: kohezja wymaga osobnej zmiany solvera i testów usypywania, wypierania wody oraz bilansów. Nie uznawać zmiany koloru za wdrożenie spójnego mokrego materiału.
- [x] Zapisać porównawcze obrazy/nagrania dnia, ciemnej sceny, pożaru, wybuchu i wody; osobno profilować światło, bloom i kompozycję.
- [x] Porównać JFA+1 z polem referencyjnym; nasiona zapisują dwie współrzędne w RG16UI albo pojedynczy liniowy ID w R16UI. Nie traktować przybliżenia JFA jako dokładnego SDF. Przebudowa po rewizji terenu ma osobny pomiar i budżet piku destrukcji.
- [x] Widoczność rozstrzyga dokładny supercover DDA, kończący się przy źródle; JFA+1 w high kształtuje miękki brzeg. Wyczerpanie limitu oznacza zasłonięcie. Testy: osłona 1px, skos/narożnik, ściana za bliską lampą, lampa przy ścianie i zmiana terenu w tej samej klatce.
- [x] Jawny potok barw: tekstury koloru sRGB → liniowy RGBA16F → światła → bloom (kandydat Dual Kawase) → tonemapping ACES/AgX i LUT → konwersja/kwantyzacja/dithering Bayera 4 × 4 → UI. Zagwarantować pojedynczą konwersję sRGB; tekst i celownik pozostają ostre po efektach kamery.
- [x] Odcienie piasku stabilizować przez ID; wilgoć przechowywać jako jawny parametr z wysychaniem zależnym od warunków. Pakowanie w `motion.w` wymaga zmiany ABI i wszystkich testów identyfikacji materiału, nie może po cichu zmienić typu. Kohezja mokrego piasku zostaje osobnym eksperymentem fizycznym.

Gotowe, gdy: styl pasuje do pikselowych teł, efekty nie zasłaniają rozgrywki, a poziomy jakości respektują zmierzony budżet. Radiance Cascades nie jest zależnością ukończenia tego etapu.

Wdrożenie i protokół: [I11_LIGHTING.md](../reports/I11_LIGHTING.md). Wilgoć jest osobnym R32F zamiast pakowania do ABI; termika jest opcjonalnym źródłem temperatury i emisji. Końcowy koszt low do +1.028 ms przy limicie 0,35 ms; odbiór aktywnego okna: otwarty. Oświetlenie domyślnie off.

## 5. Eksperymenty po wykonaniu podstaw

| Eksperyment | Warunek rozpoczęcia | Kryterium decyzji |
| --- | --- | --- |
| APIC/FLIP dla cieczy, PBD dla ziaren | Po odbiorze I08–I11; I07 wykazuje konkretny nierozwiązany problem jakości/skali PBF | Ten sam test wody/oleju, cienkich ścian, materiałów i kapsuły; porównać jakość, całkowity koszt transferów/ciśnienia i złożoność sprzężenia. Bez drugiego silnika gry. |
| Rozdzielczość cieczy 0,5 komórki | Po odbiorze I08–I11 i potwierdzeniu budżetu | Czterokrotnie większa liczba cząstek z poprawioną masą i promieniem; policzona pamięć, sąsiedzi i FPS. |
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
| Review v0.7.0-i04: pamięć, okno, maski faz, sortedHot, lokalny Dmax, gruz | I04.5; hipotezy i ograniczenia w rejestrze rozstrzygnięć |
| Cache shaderów, leniwe CPU, ScenarioRunner, GCC/IPO | I05 |
| ABI, ack/odrzucenie, skok, sterowanie poza areną, cooldown, IK | I06 |
| HP tarcz i informacja trafienia; narzędzia/pola na misji | I05/I06/I08/I09/I10 według właściciela stanu |
| Szczegółowe wzory i deklaracje QA w dostarczonym review | Materiał źródłowy, korekty w REVIEW_I04_ROZSTRZYGNIECIA.md; nie są odbiorem implementacji |

## 7. Zasady odbioru i kontynuacji

- Każdy etap: najpierw pomiar przed zmianą, potem jedna grupa zmian, testy dotkniętych kontraktów i porównanie po zmianie. Pełny zestaw integracyjny przed wydaniem.
- Wydajność nie może opierać się na pominięciu materiału, cichym przepełnieniu list, usunięciu reakcji postaci lub opóźnionym snapshotcie używanym jako bieżący stan fizyczny.
- Kontrolować stabilne ID, masy materiałów, aktywne sloty, brak odradzania usuniętych cząstek, szczelność cienkich ścian, skalowanie czasu, utratę fokusu i reset. Termika dodatkowo wymaga bilansu energii i składników.
- Zachować testy tras 30/60/144 FPS. Nowe metody numeryczne oceniać tolerancjami i mierzalnym zachowaniem; brak zgodności bitowej między GPU nie jest sam w sobie błędem.
- Zmieniony etap ma instrukcję odtworzenia, sprzęt, surowe CSV oraz opis ograniczeń. Raportować także nieudane cele i eksperymenty odrzucone na podstawie pomiaru.
- Nie przebudowujemy całej aplikacji w jednym wydaniu. Po I04.5 ustalamy rzeczywisty odzyskany budżet; po I07 wybieramy parametry wody; po I10 ponownie bilansujemy koszt pełnego świata.

### Rejestr postępu

| Iteracja | Stan | Wynik / raport | Następny krok |
| --- | --- | --- | --- |
| Przygotowanie planu | Zakończone 2026-10-28 | Zweryfikowano kod i istniejące raporty 0.7.0; bez zmian runtime i bez nowych testów GPU | I00 |
| I00 | Instrumentacja ukończona; odbiór wydajności okna otwarty | [Raport I00](../reports/I00_GPU_PROFILING.md): liczniki, fazy CPU/GPU, 8 wariantów, 3 powtórzenia; test okna niestabilny również w starej wersji | I01 — transport wyników; osobno kontrola warunków GPU |
| I01 | Wdrożone, z ograniczeniami pomiarów FPS | [Raport I01](../reports/I01_GPU_READBACK.md): ring 3 sloty, mapowanie/fallback, generacje, frame-level poll, regresje 11/11, kontrolowane p95 CPU <0,1 ms | I02 — przebiegi i zlecanie pracy |
| I02 | Wdrożone 2026-10-28 | [Raport I02](../reports/I02_GPU_EXECUTION.md): 27 programów, tracker, komendy indirect, FIFO, regresje 12/12, okna 6/6; kontrolowane GPU misji około −7% | I03 — dane i kompletne sąsiedztwo |
| I03 | Wdrożone 2026-10-28; odbiór okna otwarty | [Raport I03](../reports/I03_GPU_NEIGHBORS.md): counting sort i prefix scan, stabilne ID, SoA, pełne zakresy po ruchu, 42/43 → 2 budowy indeksu; regresje 20/20, okna 4/6 | I04 — lokalne sprzężenie postaci |
| I04 | Wdrożone 2026-10-28; cel GPU otwarty | [Raport I04](../reports/I04_GPU_COUPLING.md): kompakcja reakcji, dwa kanały, hierarchia, konserwatywny zasięg próbek, regresje 21/21 i okna combined 3/3; 16k/32k około −29%/−33% GPU | I04.5 — uzupełniająca diagnoza i optymalizacja |
| Integracja review I04 | Dokumentacja zakończona 2026-10-29 | Przypisano uwagi do I04.5–I11, dodano kryteria i rejestr korekt; bez zmian runtime i bez nowych wyników testów | I04.5-A — odtworzenie bazy i pomiar present |
| I04.5-A/F | Wdrożone 2026-10-29; cały etap otwarty | [Raport I04.5](../reports/I04_5_GPU_STABILIZATION.md): pełna klatka/present, tryby timerów, debug phases, gruz; 21/21 + końcowe 4/4, 18/18 prób, mediana GPU misji 2,79–3,09 ms | I04.5-C, potem ponowna ocena B/D/E i odbiór całego I04.5 |
| I04.5-C | Eksperyment opt-in zrealizowany 2026-10-29; obie opcje domyślnie OFF | [Raport C](../reports/I04_5C_GPU_WORK.md): 16/16 pomiarów przepustowości, brak potwierdzonego przyspieszenia; poprawiono przekazanie początkowej polityki odczytów. Nowe C 5/5 PASS; pełny CTest 22/26 (trzy timeouty UI i błąd tolerancji lokalnego sprzężenia). Wyniki QA w raporcie C | I04.5-B — najpierw odtworzenie i wyjaśnienie bazy QA, potem transport i inicjalizacja; D/E warunkowe |
| I04.5-B | Badanie i implementacja opt-in zakończone 2026-10-30; direct domyślne | [Raport B](../reports/I04_5B_GPU_TRANSPORT.md): UploadRing, immutable flags=0, fallback GL 4.3, 28/28 QA, 36/36 procesów; brak spójnej przewagi i zmienność tego samego EXE między sesjami | Przypisanie kosztu i decyzja D/E; cel 1,5 ms oraz stabilność warunków nadal otwarte |
| I04.5-D | Badanie opt-in zakończone 2026-10-30; full domyślne | [Raport D](../reports/I04_5D_GPU_NEIGHBOR_READS.md): wąskie odczyty i aktualizowany cache w 8 SSBO, 35/35 QA, 18/18 procesów i 7 okien bez utraty czasu. Brak podstaw do zmiany domyślnej ścieżki; cache +2 MiB spowalnia 16k/32k | Profil kosztów przebiegów i chwilowego Dmax przed warunkowym E; cel 1,5 ms nadal otwarty |
| I04.5-E0 | Diagnostyka zakończona 2026-10-30; pełne QA otwarte | [Raport E0](../reports/I04_5E0_GPU_COSTS.md): 16/16 pomiarów, 6/6 kontroli, 384 snapshoty; sondy OFF. Dominujące PBF/ziarna, Dmax do 0,4375 po eksplozjach. Pełne QA 37/38; powtórka E0 PASS, to samo sprzężenie FAIL na D | Najpierw wyjaśnienie bazy QA sprzężenia, potem punktowy PBF G. Lokalny E warunkowy; cel 1,5 ms otwarty |
| I04.5-Q | Zakończone 2026-10-30; metoda QA zmieniona jawnie | [Raport Q](../reports/I04_5Q_GPU_COUPLING_QA.md): identyczna referencja odrzucona 1/16 przez starą regułę, 12/12 nowych powtórek, 1080 checkpointów, wszystkie wyłączenia sprzężenia wykryte; 38/38 Q. Solver bez zmian, stary warunek nadal diagnostyczny | Punktowe PBF G, z Q dla każdej konfiguracji. Cel 1,5 ms i przyczyny zmienności długich trajektorii otwarte |
| I04.5-G1 | Badanie zakończone 2026-10-30; PBF 256 pozostaje domyślne | [Raport G1](../reports/I04_5G_GPU_PBF_GROUPS.md): niezależny PBF i oddzielny argument GPU, 44/44 QA, 9/9 Q i wykrytych usterek, 16/16 końcowych pomiarów, 8/8 okien z fokusem bez utraty czasu. G 256/64 misja 2,83006/2,82413 ms; brak powtarzalnego przyspieszenia. Zachowano odrzucone/przygotowawcze próby skryptu; końcowe 15360 surowych kroków CPU/liczników | G2 — kontrakt rozdzielonych banków PBF i osobne A/B braku aliasowania; cel 1,5 ms nadal otwarty |
| I04.5-G2 | Zakończone 2026-10-30; wariant kwalifikatorów OFF domyślnie | [Raport G2](../reports/I04_5G2_GPU_PBF_RESTRICT.md): 52/52 QA, 128/128 błędnych wiązań odrzuconych przed GL, źródła GL różnią się wyłącznie kwalifikatorami; 8/8 Q i usterek, 6/6 okien. Dwie serie po 18/18; OFF/ON w szybszej 2,81512/2,81293 ms. Na tym samym G1 odtworzono około 2,8→12→2,8 ms bez zmiany EXE; przyczyna otwarta | G3 — przypisać zmienność bazy przed dalszym PBF i oceną odbioru I04.5 |
| I04.5-G3-A | Ograniczona próba zakończona 2026-10-30; całe G3 otwarte | [Raport G3-A](../reports/I04_5G3_GPU_VARIANCE.md): runtime bez zmian; 25/25 + 2/2 kontrole, 13/13 negatywnych kontroli, 384 rekonstrukcje bez pominiętych par; ukryta misja 2,785–2,898 ms, nie odtworzono 12 ms. Narzut sond i obserwatora raportowany osobno | G3-B — pełne historyczne preludium QA oraz przechwycenie wolnego reżimu przed dalszym PBF |
| I04.5-G3-B | Odtworzenie zakończone 2026-10-30; przyczyna całego G3 otwarta | [Raport G3-B](../reports/I04_5G3B_GPU_REPLAY.md): 52/52 QA, 6/6 dodatkowych Q, G1 2,77→11,34→około 2,8 ms, runtime bez zmian. 192 rekonstrukcje bez pominiętych par; histogramy 16k/32k identyczne z szybkimi. Powrót nastąpił przed pokazaniem okna, po 142,69 s i zmianie supervisora; nie przypisano przyczyny | Historyczna propozycja G3-C odłożona; użytkownik zlecił I05 |

| I05 | Wdrożone 2026-10-30; wydajność okna nieodebrana | [Raport I05](../reports/I05_SHARED_WORLD.md): jeden świat, 42 programy, wspólna scena i scheduler, leniwe CPU, cache opt-in, nagrania/CSV/obraz. Nowe testy sesji i cache 2/2 PASS; GCC 6/6, format nagrań 12/12 odrzuceń; pełny Windows 53/54 (timeout misji także na poprzedniej I04.5); cold/warm 42 kompilacje → 42 trafienia | I06; ograniczenie wydajności okna pozostaje jawne |

| I07 | Funkcjonalne 2026-10-30; nowy profil opcjonalny | [Raport I07](../reports/I07_FLUID_QUALITY.md): granice, napięcie/adhezja 2D, ograniczona wirowość, nagrania v2; jakość 12/12, Windows 58/58, GCC 8/8; ABBA +0,237–0,304 ms/tick | Legacy domyślne; budżet +0,25 ms nieodebrany; profil kosztu gotowy dla I08 |

| I09 | Funkcjonalne 2026-10-31; budżet i odbiór okna otwarte | [Raport I09](../reports/I09_GPU_VFX.md): 16k/48 B, wspólne urządzenie GPU/CPU, niekonsumujący ack odbiór zdarzeń, wtórna woda, DDA, warstwy; Windows 61/61 + 10/10, GCC 9/9; 69 120 próbek ABBA + 23 040 referencji I08, low do +0,678 ms | I10 pozostaje osobnym etapem; nie deklarować odbioru budżetu I09 ani okna bez fokusu |
| I10 | Funkcjonalne A–D 2026-10-31; domyślnie off, budżet otwarty | [Raport I10](../reports/I10_GPU_THERMAL.md): zachowawcze H/masa, MAC 320×192, tlen/paliwo/produkty, fazy, źródła tickowe i v3 replay; Windows 64/64 + 10/10 + 3/3 + 3/3 + 1/1, GCC 10/10; dwie pełne macierze po 46 080 próbek | Aktywne obszary i integracja fizycznej puli wymagają osobnego projektu; do +9,717 ms gorące / +3,382 ms zimne, bez odbioru budżetu ani aktywnego okna |
| I10-E0–E4 | Zaplanowane 2026-10-31; prace niewykonane | [Plan domknięcia I10](PLAN_I10_DOMKNIECIE.md): pomiar kosztów, warunkowa integracja puli, aktywność A/C i transport B, QA i odbiór okna; bez nowych wyników ani zmiany limitów | Zacząć od E0 i zachować roboczą bazę I10; termika pozostaje domyślnie off |

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

- **2026-10-29, review I04:** dodajemy I04.5 przed I05. Diagnozy DWM/VRAM i obietnice przyspieszenia są hipotezami do porównania z bazą, a nie potwierdzonym usunięciem problemu. Nie skracamy zaległości zegara w celu ukrycia kosztu fizyki; zachowujemy fallback GL 4.3 i kompletność sąsiedztwa.
- **2026-10-29, review I04:** I05 obejmuje wspólny świat/zasoby i narzędzia, I06 reguły CPU/komendy/zdarzenia. Nowe wzory wymagają niezależnej walidacji; sprzeczności review rozstrzyga osobny rejestr. APIC/FLIP, zagęszczenie 0,5 i Radiance Cascades czekają na odbiór I08–I11; nie zakładamy, że PBF na pewno usunie wszystkie artefakty.

- **2026-10-29, I04.5-A/F:** trzy tryby timerów nie zamknęły celu 1,5 ms; pozostawiamy domyślne elapsed. Seria 18/18 zakończona poprawnie, ale fokus był zmienny, a p95 klatki nadal bywa ponad 16,67 ms. W kontroli debug id=131186 odebrano przy walidacji, nie w runtime; to nie dowód przyczyny dawnych spowolnień. Kolejne prace C dotyczą pustych przebiegów i terenu, bez zmiany równań solvera.

- **2026-10-29, I04.5-C:** maska faz i lokalne potwierdzenie projekcji pozostają opcjami eksperymentalnymi, domyślnie OFF; 16/16 pomiarów przepustowości nie wykazało pewnego przyspieszenia. Politykę odczytów przekazujemy przed inicjalizacją świata, także w starszych helperach, aby próba bez odczytów nie zaczynała się od ukrytych kopii. Następne B bada transport i inicjalizację osobno; nie ogłaszamy zamknięcia celu 1,5 ms ani przyczyny historycznych spowolnień. D/E nadal wymagają wskazania kosztu pomiarem.

- **2026-10-29, QA po C:** nowe testy C zakończyły się 5/5 PASS, pełny CTest wynikiem 22/26. Otwarte pozostają trzy timeouty UI i błąd tolerancji lokalnego sprzężenia. Limity oraz tolerancje nie zostały zmienione. B rozpoczynamy od odtworzenia i wyjaśnienia tej bazy QA; dalsze optymalizacje transportu wymagają osobnej oceny.

- **2026-10-30, I04.5-B:** pozostawiamy direct; staged/immutable/auto są eksperymentalne. Pełne QA 28/28 i 36/36 procesów bez błędu. Niewielka przewaga mediany w jednym warunku nie utrzymuje się we wszystkich scenach; nowe ścieżki kosztują dodatkową pamięć. Duża zmiana wydajności między sesjami wystąpiła na tym samym EXE, więc nie przypisujemy jej uploadowi ani nie deklarujemy naprawy warunków systemowych. D/E wymagają najpierw przypisania kosztu; I05 nie rozpoczyna się automatycznie.

- **2026-10-30, I04.5-D:** zachowujemy full/direct oraz obie opcje C OFF. 18/18 procesów potwierdziło niewielką przewagę narrow w misji, bez podobnej korzyści 16k/32k; sorted ma dodatkowy koszt zapisów i 2 MiB. Pełne QA 35/35, siedem prób okna bez utraty czasu. Mały końcowy Dmax nie wyklucza chwilowych outlierów; lokalny E czeka na profil w czasie i dowód kompletności. Cel 1,5 ms oraz historyczna przyczyna zmienności nadal otwarte.

- **2026-10-30, E0:** odraczamy lokalny Dmax po profilu trajektorii i kosztów. Wąskie gardła: PBF w misji i kontakt ziaren w 16k/32k. Sonda passes ma znaczny koszt CPU, bounds/snapshots zwiększają GPU; wszystkie pozostają OFF. Pełny CTest 37/38 ujawnił znaną zmienność sprzężenia, odtworzoną również na D. Zachowujemy nieudaną próbę, tolerancje i limity. Najpierw baza QA, potem eksperyment rozmiaru grupy wyłącznie PBF; I05 nie jest rozpoczęte.

- **2026-10-30, Q:** zmieniamy metodę akceptacji sprzężenia po wykazaniu odrzucenia identycznych referencji 1/16. Nie podnosimy wartości progów: najniższe dawne minima ograniczają krok i skumulowany defekt na wspólnych stanach. Prawdziwe wyłączenie sprzężenia jest wykrywane. 38/38 dotyczy nowego protokołu; nie oznacza zaliczenia starego ilorazu ani deterministyczności solvera. Następne PBF G; I04.5 pozostaje otwarte.

- **2026-10-30, G1:** nie zmieniamy domyślnego 256 po minimalnym wyniku 64 i braku spójnego zysku w większych scenach. Pełny Q 44/44; stare kryterium odrzuciło 3/9 długich tras diagnostycznych, bez zmiany progów. Przygotowanie katalogów przez harness uzupełnia brakujące surowe CSV również dla starej Q; wstępne próby zachowano i wykluczono z końcowej serii. Kolejne wskazówki braku aliasowania wymagają dowodu rozdzielenia banków; I05 nie otrzymuje automatycznego odbioru.

- **2026-10-30, G2:** kwalifikatory tylko dla dwóch dowiedzionych banków Jacobi pozostają opt-in. 52/52 i silne kontrole wiązań potwierdzają poprawność, a nie przyspieszenie. Ta sama baza G1 miała około 2,8 ms, potem około 12 ms i wróciła do około 2,8 ms; obie pełne serie zachowano oddzielnie. Następne G3 przypisuje zmienność pracy/warunków; nie deklarujemy naprawy ani automatycznego rozpoczęcia I05.

- **2026-10-30, G3-A:** kończymy ograniczony protokół z wynikiem negatywnym odtworzenia, bez nowego EXE i bez przypisania przyczyny. CPU submit wzrósł także przy szybkim GPU, więc sam wysoki CPU nie rozpoznaje reżimu 12 ms. Ślady Dmax/kandydatów są kompletne w osobnych próbach sondowanych, lecz nie obejmują historycznego wolnego reżimu ani pełnych surowych stanów. Następne G3-B ma odtworzyć pełne dawne preludium i skorelować rzeczywiste spowolnienie z pracą i zlecaniem; I04.5 nadal otwarte.

- **2026-10-30, G3-B:** odtworzono spowolnienie po pełnym preludium przy niezmienionym EXE i poprawnym świeżym QA. W wolnym reżimie nie znaleziono wzrostu Dmax/kandydatów odpowiadającego skokowi czasu; 16k/32k mają identyczne badane histogramy. CPU bywa zarówno niski, jak i wysoki przy wolnym GPU. Powrót szybkiej pierwszej ukrytej próby po przerwie i nowym supervisorze nie pozwala przypisać zmiany widoczności. G3-C ma rozdzielić te czynniki i zebrać dokładniejszy stan wykonania; nie zmieniamy ustawień domyślnych ani statusu odbioru I04.5.

## 8. Materiały pomocnicze

Kod i dane: [architektura 0.7](../ARCHITECTURE.md), [misja GPU](../reports/GPU_MISSION_070.md), [przegląd fizyki 0.6.1](../reports/PHYSICS_REVIEW_061.md), [benchmark misji](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu070/build/gpu-mission-benchmark.csv), [próbki okna](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu070/build/mission-frames.csv), [obecne testy](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/legacy/TEST_RESULTS.txt).

Review z 2026-10-29: [oryginał dostarczony przez użytkownika](reviews/review_strefa_zero_v0_7_0_i04_roadmap.md) oraz [rejestr przyjęcia i korekt](REVIEW_I04_ROZSTRZYGNIECIA.md). Te materiały nie zmieniają historycznych raportów ani statusu wydania I04.

Źródła pierwotne metod, do dalszego wdrażania:

- [KHR_debug — specyfikacja](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_debug.txt).
- [Position Based Fluids — Macklin i Müller](https://mmacklin.com/pbf_sig_preprint.pdf).
- [Small Steps in Physics Simulation — Macklin i in.](https://mmacklin.com/smallsteps.pdf).
- [Versatile Rigid-Fluid Coupling for Incompressible SPH — Akinci i in.](https://cg.informatik.uni-freiburg.de/publications/2012_SIGGRAPH_rigidFluidCoupling.pdf).
- [Versatile Surface Tension and Adhesion for SPH Fluids — Akinci i in.](https://cg.informatik.uni-freiburg.de/publications/2013_SIGGRAPHASIA_surfaceTensionAdhesion.pdf).
- [Unified Spray, Foam and Bubbles — publikacje Ihmsena](https://cg.informatik.uni-freiburg.de/people/ihmsen.htm).
- [Real-Time Fluid Dynamics for Games — Stam](https://www.dgp.toronto.edu/people/stam/reality/Research/pdf/GDC03.pdf).
- [Meshless Deformations Based on Shape Matching — Müller i in.](https://matthias-research.github.io/pages/publications/MeshlessDeformations_SIG05.pdf).

- **2026-10-30, przejście do I05:** użytkownik zlecił przerwanie I04.5 i dalszą pracę nad I05. Odkładamy G3-C, lokalny E i cel 1,5 ms bez oznaczania ich jako zaliczone. I05 używa dotychczasowych ustawień: 256, restrict OFF, full/direct, C i sondy OFF. Architektura i koszt startu są oceniane oddzielnie od nierozwiązanej zmienności GPU.

- **2026-10-30, odbiór I05:** jeden świat i 42 programy, wspólne narzędzia/nagranie oraz leniwe CPU. Windows 53/54; timeout 60 s misji odtworzono zarówno na wcześniejszej I04.5-G2, jak i I05. Końcowe sprawdzenia wspólnej sesji, laboratorium i cache zachowano w raporcie. Nie zaliczamy wydajności okna ani celu I04.5 przez sam refaktor.

- **2026-10-30, I06:** wspólna sesja reguł CPU, ABI 64 B, CommandRing/EventRing, ack/zwroty/deduplikacja i backpressure, coyote/jump buffer, wejście poza areną oraz wspólne IK. Windows 55/56 + końcowe 4/4 po poprawce wyłącznie scenariusza timerów; p95 CPU <0,10 ms osiągnięte w końcowych direct (0,029–0,042 ms), ale wcześniejsze 1,07–1,37 ms zachowano i stabilność pozostaje otwarta. Zachowano 13/181 ticków broni po wykryciu i skorygowaniu regresji migracji; pierwotna nieudana seria pozostaje w raporcie. I07 nie rozpoczęte.

- **2026-10-30, I07:** wdrożono nowe modele jako opcje, zachowując legacy. Nie podnosimy s_corr ani liczby podkroków automatycznie. Balanced przechodzi poprawność i poprawia część miar, lecz dwa z trzech bloków przekraczają +0,25 ms/tick. Odbiór wydajności pozostaje otwarty; nie zmieniamy historii I04.5/I06. Szczegóły, pełne wyniki i ograniczenia: [raport I07](../reports/I07_FLUID_QUALITY.md).

- **2026-10-31, I08:** wdrożono jawne przebiegi, wspólny tracker stanu, teren R16UI, liniową scenę, pole faz/prędkości, absorpcję i bezpieczną refrakcję, opcjonalny bilateral/Fresnel/pianę/kaustyki oraz HP i potwierdzony błysk tarcz. Windows 59/59, GCC 8/8; 12 zamrożonych checkpointów i po 512 prób terenu/maski cieczy. Końcowa macierz 51 840 próbek kwalifikuje basic do budżetu wody; cały render droższy od referencji. Cele całej klatki oraz historyczne ograniczenia są jawnie rozdzielone w raporcie I08.

- **2026-10-31, I09:** wspólny `RenderDevice` i `VfxPool` dla misji, laboratorium i adaptera CPU; pula 16 384 × 48 B, jakość niezależna od wody, domyślnie low. Osobny kursor generacja/tick/ID nie konsumuje ack. Pole 320×192 jest autorytatywne dla prezentacji, z progami 0,15/0,75 i histerezą 0,05; fizyka bez sprzężenia zwrotnego. Low ogranicza ocenę źródeł i rysowanie wtórnej wody przy pełnej puli; high/low zachowują żywe sloty. Pełna regresja Windows 61/61, końcowe 10/10, GCC 9/9; kompletna macierz nie spełnia +0,25 ms (do +0,678344 ms). Przekroczenie i brak fokusu okien są jawne. 64k, I04.5, termika, gazy i HDR nie są włączane tym etapem.

- **2026-10-31, I10:** uruchomiono opcjonalną termikę i gazy w autorytatywnym świecie GPU, bez zmian starych shaderów solvera legacy. Parametry pochodzą z material.hpp; zakres reakcji i ograniczenia faz są jawne w raporcie. Zamknięte bilanse zachowują tolerancję 0,1%, także przy pełnych 32768 nośnikach, gaszeniu i destrukcji. Źródła są tickowe, a nagrania v3 zgodne ze schedulerem; v1/v2 zachowują starą ścieżkę. 160×96 odrzucono z powodu cienkich ścian/drzwi. Specjalizacja programów i zawężenie barier nie wystarczyły do limitów 0,70/0,05 ms; nie podnosimy limitów ani nie włączamy modułu domyślnie. Nie ma jeszcze aktywnych obszarów zimnego świata, pełnego sprzężenia gaz–ciecz/porowatości ani chemii wszystkich materiałów. Historyczne problemy I04.5/I07/I09 i odbiór aktywnego okna pozostają otwarte; I11 nie rozpoczęto.

- **2026-10-31, plan domknięcia I10:** zapisano [E0–E4](PLAN_I10_DOMKNIECIE.md) na podstawie istniejącej implementacji A–D i przekroczonych budżetów. Najpierw rozdzielenie kosztu oraz integracja puli, następnie zachowawcza aktywność i transport gazu. Projekcja ciśnienia wymaga osobnego dowodu zasięgu; samo halo kafli nie wystarcza. To aktualizacja planu, bez zmian runtime i nowych pomiarów. I04.5 pozostaje odłożone.

- **2026-10-31, I11:** I11 wdrożone funkcjonalnie: Windows 67/68, GCC 11/11; low do +1.028 ms przy limicie 0,35 ms, odbiór aktywnego okna otwarty; domyślnie off. Osobne R32F wilgoci, dokładne DDA i pomocnicze JFA+1 high, liniowy HDR i pojedyncze sRGB. Nie zmieniono numerycznych shaderów I10 ani 64-bajtowego ABI. Pełne pomiary, nieudane próby i testy paczki zachowano w raporcie I11.
