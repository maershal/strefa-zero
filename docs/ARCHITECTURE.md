# Architektura 0.7.0 / I06

## Jeden aktywny świat i zasoby

`app::SimulationWorld` jest jedynym właścicielem aktywnego `ParticleWorld` w aplikacji. Gra i narzędzia są dwoma widokami tej samej jawnie skonfigurowanej sceny. `DeviceContext` udostępnia API i sprawdza kontekst/wątek, `ShaderLibrary` posiada programy, a `SceneRenderer` posiada cele renderowania. Opcjonalny cache binarny nie zmienia własności świata. `ReadbackRing` pozostaje wspólnym transportem podsumowań. Zmiana widoku nie resetuje sceny ani generacji; reset/zmiana sceny są jawnymi działaniami.

`Application` obsługuje cykl życia i klatkę. Parsowanie uruchomienia, scenariusze diagnostyczne i `ScenarioRunner` są wydzielone. `SessionRecording` zapisuje polecenia rzeczywiście wykonanych kroków; CSV eksportu korzysta z ostatniego asynchronicznego HUD. Modele CPU powstają dopiero przy wejściu do narzędzia referencyjnego. Szczegóły i walidacja: [I05](I05_SHARED_WORLD.md).

## Misja i granica prezentacji

Sesja misji należy do `gpu::ParticleWorld`; `MissionInput` opisuje intencje jednego kroku, `MissionSnapshot` jest niewładczym podsumowaniem HUD. `MissionConfig` ładuje postać i cele do sceny, a `queue_explosion` przyjmuje ograniczoną kolejkę zdarzeń z identyfikatorami. Autorytatywna fizyka postaci, pociski i geometryczne trafienia pozostają na GPU. `game::GameplaySession`, niezależna od CPU/MAC, posiada reguły broni, amunicję, przeładowanie, HP, wynik i ukończenie misji. `WorldCommand` i `WorldEvent` mają ABI 64 B / align 16; niezawodny `EventRing` z 32 slotami pracuje co tick i jest oddzielony od stratnej telemetrii HUD. Rezerwacje komend rozlicza `CommandRing`; [szczegóły I06](I06_GAMEPLAY_PROTOCOL.md). `ui::ArenaView` zastępuje bezpośrednią zależność widoku od właściciela świata CPU. GPU rysuje postać z istniejących atlasów; ekrany CPU i GPU kompilują wspólne równania pozy/IK z `soldier_pose.glsl`. Fizyczna lufa korzysta z aktualnej pozycji GPU, a interpolacja dotyczy renderowania.

Lista aktywnych ID i komendy indirect powstają po zmianie przynależności cząstek. Bufor wolnych miejsc zawiera również listę ID i komendy; bufor liczników rozszerza się o sesję, pociski i granaty. Mieszczymy się w ośmiu wiązaniach compute SSBO. Podsumowanie ma generację świata i numer kroku; odczyt z fence nigdy nie zastępuje aktualnego stanu fizyki. Pełny opis: [misja GPU 0.7](GPU_MISSION_070.md).

## Backend GPU

`gpu::ParticleWorld` (`gpu/world.hpp`) zarządza rezydentnymi buforami cząstek, edycjami, kontaktami PBD, cieczami PBF i teksturą wyniku. Mnożniki/gęstości PBF są w osobnym SSBO. Cząstka ma 64-bajtowe ABI z kompensowaną pozycją. Pole kolizji ziaren używa kubicznie interpolowanej odległości ze znakiem, a ciecz zachowuje dokładne granice AABB. Szczegóły korekty ciśnienia Spiky i parowej kohezji: [przegląd 0.6.1](PHYSICS_REVIEW_061.md). SDL/OpenGL pozostają na wątku okna. CPU przekazuje komendy, a pomiary oraz małe podsumowanie liczników i sesji odbiera asynchronicznie; pełne odczyty stanu należą do walidacji. Oba widoki GPU używają jednego zegara `FixedStepClock<120,4,2147483647>` w `SimulationWorld` oraz interpolacji. W klatce zleca się najwyżej cztery kroki. Po wyczerpaniu kolejki zdarzeń niezlecone kroki i pojedyncze żądanie kroku pozostają oczekujące; przeciążenie nie skraca długu czasu. Pauza i utrata fokusu są jawnym zawieszeniem zegara. CPU zachowuje alias `FixedClock` o dotychczasowych parametrach 60 Hz / 8 kroków.

Ziarna i ciecze GPU mają wspólny stan; pełny model termochemiczny CPU zachowuje odrębny świat. CPU/MAC i GPU/PBF są różnymi solverami. Pełne tablice cząstek pozostają na GPU; między CPU i GPU przechodzą małe pakiety komend i zdarzeń. Kontrakty, modele, wyniki i plan kolejnych etapów: [GPU_LIQUIDS.md](GPU_LIQUIDS.md); historia etapu 0.5: [GPU_MIGRATION.md](GPU_MIGRATION.md).

Podkrok GPU: predykcja → odbudowa list sąsiadów → gęstość/λ → korekta PBF → kontakty ziaren → odtworzenie prędkości → lepkość. Listy sąsiadów są odbudowywane po zmianach pozycji. Przebiegi położenia używają ping-pong, a bariery SSBO oddzielają producenta i konsumenta. Osiem iteracji cieczy i dwanaście kontaktów ma osobne budżety. Okresowa redukcja maksymalnego ściśnięcia jest wykonywana na końcowym stanie. Renderowanie powierzchni używa RG16F i instancjonowania; nie uruchamia reakcji ani nie poprawia pozycji.

## Model CPU

`World` posiada stałą pulę z `executor.hpp`: domyślnie do dwóch wątków fizyki, z opcją 1–4. Podział dotyczy rozłącznych wierszy i kanałów, a redukcje mają stały porządek. Zespół synchronizuje się pomiędzy etapami; `World::step` pozostaje synchroniczny dla wywołującego. Laboratorium nadal uruchamia ten krok poza wątkiem SDL. Reakcje i ruch proszków nie są uruchamiane równolegle z cieczami, ponieważ zapisują wspólny stan. Szczegóły testów i kosztów: [audyt 0.4.2](PERFORMANCE_AUDIT.md).

`strefa_sim` jest niezależnym od SDL modelem świata. `World` udostępnia edycję, próbkę pól, dostarczenie energii, kolejkę eksplozji z identyfikatorami, raport i renderowanie do tablicy RGBA. `physics.hpp` zawiera wewnętrzny stan SoA i deklaracje systemów; `thermal.cpp`, `liquids.cpp`, `air.cpp`, `reactions.cpp` i `motion.cpp` realizują osobne odpowiedzialności. `scenes.cpp` opisuje sceny, `recording.cpp` eksportuje wyniki. Nie ma odwołań do UI ani GPU w kroku fizyki.

`strefa_game` łączy świat z graczem, pociskami, granatami i tarczami. `animation.hpp` wyznacza czystą funkcją pozę i analityczne dwuczłonowe IK. `strefa_ui` odczytuje stałe widoki modelu i emituje intencje. `app/application.cpp` tworzy referencje CPU leniwie, a wspólny świat GPU posiada poprzez `SimulationWorld`. Laboratorium CPU i poligon CPU mają odrębny stan; gra i narzędzia GPU współdzielą aktywną scenę.

## Kolejność kroku

1. Odświeżenie zmienionych przez autora pól i reguł aktywności.
2. Reakcje z zapotrzebowaniem na energię, paliwo i utleniacz; ograniczona kolejka eksplozji.
3. Akustyka, zachowawczy transport/dyfuzja gazów i ujścia.
4. Ruch proszków i fragmentów z kolizją na całej drodze.
5. Przewidywanie prędkości cieczy, PCG, projekcja, transport udziałów i entalpii.
6. Przewodzenie, przemiany fazowe, odświeżenie temperatur i bilansów.

Zewnętrzny krok 60 Hz jest niezależny od FPS. Podkroki płynów i fali wynikają z CFL. Bufory PCG, prędkości i skalarów są wielokrotnie używane. Iteracyjny solver ma limit, residuum i jawny status zbieżności. Zmieniony materiał budzi otoczenie chunków 16 × 16; przewodzenie, reakcje i powietrze mają niezależną aktywność. Jednowątkowa wersja i stały porządek zapewniają powtarzalność na tym samym kompilatorze/architekturze. Nie deklarujemy zgodności bitowej między różnymi platformami.

Raport masy i entalpii powstaje podczas odświeżania pól, bez dodatkowych skanów całego świata. Temperatury przechowywane są jako cache wynikający z energii, a nie niezależnie modyfikowane źródło prawdy. Edycje oznaczają cache jako brudny. `Cell` jest lekkim widokiem, nie formatem zapisu całego stanu.

## Własność i błędy

SDL, okno/kontekst, tekstury i sesja ImGui używają RAII. Fabryki i oczekiwane błędy platformy zwracają `std::expected`. Na granicy procesu pozostałe wyjątki otrzymują komunikat lub plik raportu smoke. Renderer nie wykonuje reakcji. Eksport jest lokalny i pozostaje poza pomiarem fizyki. W laboratorium wykonuje go wątek symulacji; obciążenie zapisem może zmniejszyć liczbę kroków modelu, bez blokowania obsługi okna.

Ustawienia nadal korzystają ze schematu typowanego `meta::Field` i stabilnych kluczy zapisu. C++26 reflection jest opcjonalnym eksperymentem, poza wymaganiami gry. Nie dodano kontenera usług, ogólnego event busa ani ECS. Nowe zależności runtime nie są potrzebne.

`FixedClock` ma limit ośmiu kroków nadrabiania. Raportuje pominięty czas, pauza zeruje akumulator, a utrata fokusu czy minimalizacja zatrzymuje poligon i usuwa wejście. Jednorazowe akcje przechodzą przez `InputBuffer`, więc nie giną przy renderowaniu bez kroku fizyki.

## Testy

`core.settings` — parsowanie, walidacja i zapis. `sim.materials` — bilanse, entalpia, topienie/krzepnięcie, hydrostatyka, naczynia, cienkie ściany, mieszaniny, tlen, kwas, fala, osłona, silne źródła i porównanie skanowania proszków. `game.training` — wejście, kolizje, kurs, broń, tarcze, granat, 30/60/144 FPS oraz więzy animacji. `app.opengl_smoke` — sześć ekranów, trzy tła, atlas części/broni, pauza i kroki, strzały i eksplozja na prawdziwym OpenGL. Sceny CLI i natywna kontrola okna uzupełniają testy.

## Wątek laboratorium

`LaboratoryRunner` posiada World wyłącznie na swoim wątku. Do UI trafia `shared_ptr<const LaboratorySnapshot>` z RGBA, statystyką, konfiguracją, maską chunków i odpowiedzią inspektora dla wskazanego punktu. UI nie blokuje się na solverze. Mutex chroni krótką wymianę wskaźnika i kolejkę poleceń, a nie obliczenia fizyki ani renderowanie. Kierunek kontroli: intencja UI → kolejka → World → niezmienny obraz. Spóźniony odczyt inspektora nie jest przypisywany nowej pozycji kursora.

`std::jthread` i stop_token zapewniają wybudzenie, zatrzymanie i dołączenie wątku przed zniszczeniem zasobów. Nagranie jest finalizowane przez właściciela świata. Po wyjściu z laboratorium i minimalizacji obliczenia są zatrzymywane; jeden rozpoczęty krok może jeszcze dobiec końca. Kolejka ma limit 512 poleceń i jawny komunikat przeciążenia. Nie łączymy krzywych pędzla w prosty odcinek, aby nie gubić kształtu rysowania.

Laboratorium wykonuje najwyżej jeden krok w iteracji roboczej, bez kumulowania długu czasu. Poligon nadal używa swojego FixedClock. Synchroniczna ścieżka testu sześciu ekranów jest zachowana; oddzielny test app.laboratory_responsiveness uruchamia produkcyjną ścieżkę z wątkiem i mierzy rzeczywiste odstępy klatek okna.

`pressure.cpp` przechowuje wektory PCG w zwartej numeracji mokrych komórek, a sąsiedztwo operatora jest buforowane przed iteracjami. Poprzedni solver pozostaje do niezależnego porównania. Nowe testy obejmują zgodność obu solverów, niezmienność opublikowanych obrazów, próbkę inspektora, pojedyncze kroki, reset i zamknięcie wątku.

### Fizyczne profile cieczy I07

Niezmienne `ExecutionOptions::fluid` jest częścią konfiguracji świata i nagrań v2. Opcjonalne wagi granic zajmują piątą płaszczyznę FP32 Terrain i tablicę lokalnych próbek kapsuły. Dwa dodatkowe vec4/slot za dotychczasowymi kanałami FluidTerms przechowują świeżą gęstość i pole normalnych/wirowości. Wiązania SSBO pozostają 0–7, ABI Grain 64 B. Przebiegi gęstość → pola → prędkości są rozdzielone barierami, bez odczytu wszystkich cząstek do CPU w pętli gry. Szczegóły i bramki: [I07](I07_FLUID_QUALITY.md).

## Renderer I08

`SceneRenderer` posiada stałe bufory obrazu 960 × 576, mapę terenu R16UI oraz listę przebiegów sceny, splatów, filtracji, grubości, kompozycji i pierwszego planu. `DeviceContext::state` jest wspólnym trackerem wiązań compute/graphics. Regularny render ustawia pełny kontrakt stanu i kończy na framebufferze 0; granice ImGui i jawnych eksportów unieważniają tracker.

`RenderOptions` i potwierdzona prezentacja tarcz są oddzielone od `ExecutionOptions`, ABI komend/zdarzeń oraz nagrań fizyki. Szczegóły i formaty: [I08_SCENE_RENDERER.md](I08_SCENE_RENDERER.md).

## I09: wspólna prezentacja VFX

`RenderDevice` posiada pożyczony kontekst urządzenia, bibliotekę programów i jedną pulę VFX. `ParticleWorld` przyjmuje współdzielone urządzenie; stare wywołania create zachowują zgodność. Adapter poligonu CPU używa tego samego urządzenia bez tworzenia świata GPU. VFX mają własne bufory, kursor zdarzeń, zegar ticków i pole 320 × 192. Nie potwierdzają EventRing ani nie zapisują do świata. `World::sample_fluid` jest lekkim, tylko odczytowym widokiem danych CPU dla prezentacji. GPU wykorzystuje punktowy wariant kernela splatu I08; pierwotny renderer wody zachowuje swój przebieg. Rozmiar WorldEvent i kontrakt nagrań pozostają bez zmian. [Kontrakt i odbiór I09](I09_GPU_VFX.md).

## I10: autorytatywna termika i gazy

`ParticleWorld` tworzy `ThermalSystem` leniwie po włączeniu. To stan fizyczny świata, aktualizowany po wykonanym ticku; `RenderDevice` pozostaje właścicielem prezentacji. Osobne bufory podwójne zawierają H/masę/materiał nośników i pola MAC 320 × 192. Parametry GLSL powstają z tabeli materiałów CPU. Wymiany mają wspólnego donora i przeciwne źródło/ujście, a reakcje rozliczają paliwo, tlen, produkty i energię chemiczną. Kondensat wraca do istniejącej fizycznej puli. Przekazanie gruzu po destrukcji dziedziczy masę i H pierwotnej komórki.

Wszystkie przebiegi mieszczą się w SSBO 0–7. Termika ma własny kursor zdarzeń bez konsumpcji ack, asynchroniczną telemetrię i inspektor. Scheduler zapisuje źródła w jednostkach na sekundę; nowy format nagrań v3 zachowuje ustawienia i dawki, starsze nagrania wyłączają moduł. Warstwa ognia/gazu jest rysowana po wodzie i przed celami/HUD. [Zakres, bilanse i nieodebrany budżet I10](I10_GPU_THERMAL.md).

## I11 — stan prezentacji i HDR

`SceneRenderer` posiada `LightingSystem`: wilgoć R32F po stabilnym ID, pole materiałów, ograniczoną listę świateł i pole osłon. `ParticleWorld` aktualizuje ten stan raz po wykonanym ticku; osobny przebieg przed narodzinami zeruje historię nieaktywnych ID przez image binding 7, bez rozszerzania fizycznego ABI i SSBO 0–7. Termika udostępnia tylko odczytywany stan i eksport temperatury/emisji. Oświetlenie ma własną konsumpcję potwierdzonych zdarzeń i nie steruje krytycznymi ack.

Włączone I11 utrzymuje liniowy RGBA16F przez ciecz, gazy i VFX, a następnie wykonuje bloom, tonemapping, LUT, jedno kodowanie sRGB i kwantyzację. Oznaczenia i ImGui są składane później. DDA maski terenu rozstrzyga widoczność; JFA+1 w high jest pomocniczym przybliżeniem miękkich cieni. Aktualizacja maski i pola nie wymaga odczytu świata na CPU. CLI i ustawienia obrazu są niezależne od nagrań fizyki. Szczegóły i odbiór: [I11_LIGHTING.md](I11_LIGHTING.md).
