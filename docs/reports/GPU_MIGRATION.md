Dokument historyczny wydania 0.5. Aktualne ciecze GPU: [GPU_LIQUIDS.md](GPU_LIQUIDS.md).

# Strefa Zero 0.5 — pierwszy działający etap GPU

Wydanie dodaje osobne laboratorium ziaren na GPU. Nie przenosi jeszcze solvera ciśnienia, termiki ani poligonu. Obecny CPU pozostaje modelem odniesienia dla wielomateriałowej symulacji, a nowy backend pozwala sprawdzić bardziej naturalny ruch drobnic i koszty architektury z danymi pozostającymi w pamięci karty.

## Skąd brały się pasy

`sim/motion.cpp` skanuje wiersze od dołu, aktualizuje świat w miejscu i zmienia komórkę po przekroczeniu jednostkowego offsetu. Ziarna o podobnych początkowych offsetach przekraczają granice prawie jednocześnie. W laboratorium CPU dochodzi do tego niskie tempo publikowania stanów, gdy ciecz lub gaz zajmują wiele milisekund. Sam port tych reguł na GPU zachowałby wygląd automatu komórkowego.

Nowy model przechowuje `x,y,vx,vy` każdego ziarna. Siatka 320 × 192 służy do przeszkód i wyszukiwania sąsiadów; nie kwantuje pozycji cząstek. Początkowe, niewielkie różnice położeń rozbijają idealną kratę. Przyspieszenie grawitacyjne jest wspólne i nie jest losowane dla ukrywania artefaktów. Obraz jest interpolowany pomiędzy pełnymi krokami i rasteryzowany w rozdzielczości 960 × 576 z pikselowymi kształtami.

## Własność danych i architektura

- `gpu/GranularWorld` jest właścicielem zasobów GL przez RAII. Jego publiczny nagłówek nie ujawnia typów OpenGL. Obiekt tworzy się i niszczy na wątku z właściwym kontekstem. CPU `sim::World` nie zależy od GPU ani SDL.
- Dwa bufory SSBO przechowują stan ziaren: 48 bajtów na ziarno, maksymalnie 32768. Każdy przebieg czyta spójny stan wejściowy i zapisuje własny indeks w drugim buforze. Indeksy pozostają tożsamością ziaren podczas ruchu.
- Sąsiedzi są znajdowani przez siatkę jednokomórkowych list: `head` i `next`. `atomicExchange` tworzy listy, a osobny dispatch po barierze odczytuje je podczas kontaktów. Nie ma założenia, że `barrier()` synchronizuje różne grupy shaderów. Przestrzenny podział opisuje [NVIDIA GPU Gems, broad phase](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-32-broad-phase-collision-detection-cuda); nasz wariant jest dwuwymiarowy i używa list, nie sortowania radix.
- Pędzel, usuwanie, lista wolnych miejsc i tworzenie nowych ziaren działają na GPU. CPU wysyła krótki opis odcinka pędzla. Licznik ziaren wraca asynchronicznie przez 12-bajtowy bufor i fence. Kilka edycji przed zakończeniem odczytu powoduje późniejszy odczyt najnowszego licznika.
- Renderowanie instancjami czyta te same bufory. Nie kopiuje pozycji do RAM. Timery `GL_TIME_ELAPSED` są zbierane tylko po `GL_QUERY_RESULT_AVAILABLE`; zwykła pętla nie używa `glFinish` ani synchronicznego odczytu całej symulacji.
- Jawne odczyty stanu i synchronizacja istnieją w narzędziu walidacji oraz przy eksporcie obrazu. Te operacje są poza czasem obliczeń fizyki raportowanym przez GPU.
- Stan, bufory pomocnicze i tekstura wyniku zajmują około 5,83 MiB, poza pamięcią sterownika, GUI i zasobami wcześniejszych trybów.

OpenGL 4.3 daje compute shaders i SSBO, więc nie wymaga przebudowy SDL/ImGui pod nowy interfejs graficzny. Reguły widoczności zapisów pochodzą ze [specyfikacji GLSL 4.30](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.30.pdf). Aplikacja najpierw próbuje GL 4.3, potem GL 3.3 dla CPU. Shadery są w `assets/shaders` i dołączane do paczki; błędy kompilacji mają jawne komunikaty.

## Matematyka ruchu

Pełny krok ma 1/120 s, a dwa podkroki kontaktowe po 1/240 s. Prędkość po oporze i grawitacji jest przewidywana półjawnie:

`v* = exp(-drag*dt) v + g dt`, `x* = x + v* dt`.

Promień kontaktowy wynosi 0,46 komórki. Prędkość ma jawny limit 80 komórek/s; jeden podkrok daje przemieszczenie najwyżej 1/3 komórki. Dwa początkowo rozdzielone, zbliżające się ziarna nie mogą przez to zamienić kolejności środków przed wykryciem kontaktu. Przeszkody są dodatkowo sprawdzane po czterech mikroprzesunięciach predykcji. Nie jest to nieograniczony solver CCD dla dowolnych prędkości i nakładających się stanów początkowych.

Dla sąsiadów rozwiązujemy nierówność `C = |xi-xj| - (ri+rj) >= 0`. Korekta jest dzielona zgodnie z odwrotnościami mas. Każdy podkrok wykonuje dwanaście iteracji Jacobiego, z ponownym budowaniem list sąsiadów. Relaksacja i ograniczenie pojedynczej korekty stabilizują gęste kontakty. Dokładnie zbieżne środki dostają przeciwnie skierowane normalne wyznaczone z pary identyfikatorów. Po korekcie pozycji odtwarzana jest prędkość i stosowane przybliżone tarcie kontaktowe.

Jest to model PBD do gry, inspirowany [Müller i in., Position Based Dynamics](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf). Stała liczba iteracji oznacza podatność gęstych usypów i mierzalne nakładanie kontaktów. Tarcie jest przybliżone, nie stanowi pełnego DEM ani gwarancji zachowania energii mechanicznej. Materiały różnią się odwrotną masą, oporem i tarciem; nazwa metal nie oznacza jeszcze sztywnej wielopikselowej bryły.

Porządek budowania list na GPU nie gwarantuje bitowej powtarzalności gęstych kontaktów. Testy sprawdzają geometrię, liczbę ziaren, tożsamość materiałów i tolerancje. Nie deklarujemy bitowej zgodności tego modelu z wcześniejszym automatem CPU.

## Pomiary i weryfikacja

Windows x64, GTX 1650, Release. Końcowy wariant: dwa podkroki po 12 iteracji kontaktów. Poniżej pomiar przenośnej paczki:

| Scena | Ziarna | Mediana GPU | p95 | p99 | maksimum [ms] |
|---|---:|---:|---:|---:|---:|
| Lawina | 9500 | 2.28349 | 3.19898 | 3.26656 | 3.38966 |
| Deszcz ziaren | 4400 | 1.95318 | 2.85091 | 2.94205 | 3.06381 |
| Mieszane materiały | 9500 | 2.31024 | 3.26038 | 3.34413 | 4.21888 |

Drugi przebieg (bezpośrednio z buildu) dał mediany 2,294 / 1,953 / 2,309 ms. Nie zmieniano taktowań ani planu zasilania. Surowe wyniki: [paczka](../measurements/gpu050/runtime/gpu-benchmark.csv), [build](../measurements/gpu050/build/gpu-benchmark.csv).

Test okna paczki: 240 klatek, mediana 16,6683 ms, p95 16,6693 ms, p99 16,7345 ms, maksimum 16,9532 ms. Wykonano 408 kroków; część testu celowo obejmuje pauzę i menu. Pominięty czas: 0 s. Jest to ograniczone do 60 FPS sprawdzenie całego okna, a nie maksymalny osiągalny FPS.

Pełny zestaw przeszedł 11/11 testów. Po końcowym zwiększeniu dokładności kontaktów ponownie przeszły trzy dotknięte testy: fizyka GPU, okno GPU i zgodność GL 3.3. Dwa testy przenośnej paczki zakończyły się kodem 0; wymuszony brak GL 4.3 poprawnie zwrócił 77. Logi: [pełny zestaw](../measurements/legacy/TEST_RESULTS-0.5.0.txt), [końcowe GPU](../measurements/legacy/TEST_GPU_FINAL.txt), [QA](../measurements/legacy/QA-0.5.0.json).

Błąd toru swobodnego spadania względem niezależnego obliczenia: 0,000327 komórki. Maksymalne nakładanie promieni w końcowym stanie usypów: 0,165 / 0,058 / 0,352 komórki (średnica ziarna 0,92). To lokalny błąd skończonej liczby iteracji, największy w gęstej mieszance mas; model nie gwarantuje idealnie sztywnych kontaktów. Oba przebiegi zachowały wszystkie ziarna i ich materiały. Test pełnej pojemności 32768 ziaren również przeszedł. Szczegóły: [gpu-tests.txt](../measurements/gpu050/runtime/gpu-tests.txt).

Pomiary `gpu-benchmark.csv` obejmują wszystkie 480 kroków każdej sceny, także początkowy spadek i zderzenia. Każdy pomiar GPU zawiera oba podkroki i wszystkie iteracje kontaktów. Renderowanie, eksport BMP, odczyt walidacyjny i czas kolejkowania CPU są poza tym licznikiem. Test okna osobno mierzy klatki całej aplikacji. Zestaw nie jest porównaniem identycznego modelu CPU/GPU i nie wolno z niego wyciągać mnożnika przyspieszenia dla wody lub wszystkich materiałów.

Walidacja obejmuje swobodny spadek wobec niezależnego obliczenia trajektorii, cienką ścianę przy dużej prędkości, zderzenie pary i jej środek masy, szybkie ziarna lecące ku sobie, 30/60/144 FPS, tworzenie/usuwanie i ponowne użycie slotów, aktualność asynchronicznego licznika, liczbę i typy ziaren w trzech scenach oraz obciążenie pełnych 32768 miejsc. Test okna sprawdza pauzę i powrót przez menu. Osobny test utrzymuje działanie starej aplikacji na GL 3.3.

Testy GPU wymagają sprzętu z GL 4.3. Kod wyjścia 77 oznacza pominięcie z powodu braku tej funkcji; nie jest wynikiem pozytywnym pomiaru. Reprodukcja: `StrefaZero.exe --gpu-test --profile gpu-results` i `StrefaZero.exe --gpu-smoke-test --profile gpu-ui-results`.

## Następne etapy pełnej migracji

1. **Wspólne pola na GPU:** materiał terenu, masa/udziały, entalpia, paliwo i tlen. Osobno stan trwały, bufory pomocnicze, komendy edycji i małe wyniki diagnostyczne. Każda przemiana ma źródło, ujście i jawny produkt. CPU pozostaje walidatorem małych scen.
2. **Ciepło, gaz i reakcje:** sąsiedzkie przebiegi na dwóch buforach, zachowawcze strumienie na ścianach, redukcje bilansów na karcie. Nie przenosić jednego przebiegu, kopiując całe pola do CPU przed kolejnym etapem.
3. **Ciecze i ciśnienie:** prędkości MAC, zachowawczy transport i wielosiatkowy preconditioner dostosowany do GPU. Sprawdzać prawdziwe residuum, zgodność ciśnień w zamkniętych składowych i cienkie ściany. Kilkadziesiąt iteracji Jacobiego bez pomiaru zbieżności nie zastępuje obecnego PCG. [GPU Gems, rozdział o płynach](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-30-real-time-simulation-and-rendering-3d-fluids) omawia kompromis solverów; potrzebna jest adaptacja do swobodnej powierzchni i nieciągłych gęstości.
4. **Sprzężenie ziaren i pól:** wyporność, opór, wymiana pędu i entalpii, zamiana ziarno–ciecz–gaz z zachowaniem bilansu. Dopiero po tych testach połączyć laboratoria i przenieść poligon.
5. **Gra i diagnostyka:** kolizje aktora przez małe lokalne zapytania/asynchroniczne wyniki, a nie odczyt całego świata. Dla renderowania zawsze używać zasobów GPU. Osobno raportować czas GPU, CPU, renderowania, synchronizacji i utracony czas.

Kryterium kolejnego wydania: ta sama scena i parametry fizyczne po obu stronach, bilanse i solver w określonej tolerancji, p95 pełnej fizyki w budżecie klatki. Obecne kilka milisekund dla ziaren pokazuje zapas tego etapu; nie dowodzi jeszcze, że kompletna wielomateriałowa symulacja zmieści się w nim również.

