# Architektura 0.7.0

## Misja i granica prezentacji

Sesja misji należy do `gpu::ParticleWorld`; `MissionInput` opisuje intencje jednego kroku, `MissionSnapshot` jest niewładczym podsumowaniem HUD. `MissionConfig` ładuje postać i cele do sceny, a `queue_explosion` przyjmuje ograniczoną kolejkę zdarzeń z identyfikatorami. Autorytatywna fizyka postaci, pociski, trafienia i wynik pozostają na GPU. `ui::ArenaView` zastępuje bezpośrednią zależność widoku od właściciela świata CPU. GPU rysuje postać z istniejących atlasów; ekran CPU nadal korzysta ze swojej funkcji pozy.

Lista aktywnych ID i komendy indirect powstają po zmianie przynależności cząstek. Bufor wolnych miejsc zawiera również listę ID i komendy; bufor liczników rozszerza się o sesję, pociski i granaty. Mieszczymy się w ośmiu wiązaniach compute SSBO. Podsumowanie ma generację świata i numer kroku; odczyt z fence nigdy nie zastępuje aktualnego stanu fizyki. Pełny opis: [misja GPU 0.7](GPU_MISSION_070.md).

## Backend GPU

`gpu::ParticleWorld` (`gpu/world.hpp`) zarządza rezydentnymi buforami cząstek, edycjami, kontaktami PBD, cieczami PBF i teksturą wyniku. Mnożniki/gęstości PBF są w osobnym SSBO. Cząstka ma 64-bajtowe ABI z kompensowaną pozycją. Pole kolizji ziaren używa kubicznie interpolowanej odległości ze znakiem, a ciecz zachowuje dokładne granice AABB. Szczegóły korekty ciśnienia Spiky i parowej kohezji: [przegląd 0.6.1](PHYSICS_REVIEW_061.md). SDL/OpenGL pozostają na wątku okna. CPU przekazuje komendy, a pomiary oraz małe podsumowanie liczników i sesji odbiera asynchronicznie; pełne odczyty stanu należą do walidacji. Oddzielny ekran `gpu_laboratory` używa wspólnego zegara `FixedStepClock<120,4>` oraz interpolacji. CPU zachowuje alias `FixedClock` o dotychczasowych parametrach 60 Hz / 8 kroków.

Ziarna i ciecze GPU mają wspólny stan; pełny model termochemiczny CPU zachowuje odrębny świat. CPU/MAC i GPU/PBF są różnymi solverami. Dane nie przechodzą w każdym kroku między RAM i VRAM. Kontrakty, modele, wyniki i plan kolejnych etapów: [GPU_LIQUIDS.md](GPU_LIQUIDS.md); historia etapu 0.5: [GPU_MIGRATION.md](GPU_MIGRATION.md).

Podkrok GPU: predykcja → odbudowa list sąsiadów → gęstość/λ → korekta PBF → kontakty ziaren → odtworzenie prędkości → lepkość. Listy sąsiadów są odbudowywane po zmianach pozycji. Przebiegi położenia używają ping-pong, a bariery SSBO oddzielają producenta i konsumenta. Osiem iteracji cieczy i dwanaście kontaktów ma osobne budżety. Okresowa redukcja maksymalnego ściśnięcia jest wykonywana na końcowym stanie. Renderowanie powierzchni używa RG16F i instancjonowania; nie uruchamia reakcji ani nie poprawia pozycji.

## Model CPU

`World` posiada stałą pulę z `executor.hpp`: domyślnie do dwóch wątków fizyki, z opcją 1–4. Podział dotyczy rozłącznych wierszy i kanałów, a redukcje mają stały porządek. Zespół synchronizuje się pomiędzy etapami; `World::step` pozostaje synchroniczny dla wywołującego. Laboratorium nadal uruchamia ten krok poza wątkiem SDL. Reakcje i ruch proszków nie są uruchamiane równolegle z cieczami, ponieważ zapisują wspólny stan. Szczegóły testów i kosztów: [audyt 0.4.2](PERFORMANCE_AUDIT.md).

`strefa_sim` jest niezależnym od SDL modelem świata. `World` udostępnia edycję, próbkę pól, dostarczenie energii, kolejkę eksplozji z identyfikatorami, raport i renderowanie do tablicy RGBA. `physics.hpp` zawiera wewnętrzny stan SoA i deklaracje systemów; `thermal.cpp`, `liquids.cpp`, `air.cpp`, `reactions.cpp` i `motion.cpp` realizują osobne odpowiedzialności. `scenes.cpp` opisuje sceny, `recording.cpp` eksportuje wyniki. Nie ma odwołań do UI ani GPU w kroku fizyki.

`strefa_game` łączy świat z graczem, pociskami, granatami i tarczami. `animation.hpp` wyznacza czystą funkcją pozę i analityczne dwuczłonowe IK. `strefa_ui` odczytuje stałe widoki modelu i emituje intencje. `app/application.cpp` posiada światy i zegary, wykonuje komendy, mierzy fizykę i aktualizuje teksturę. Laboratorium i poligon mają odrębny stan, wspólne modele i materiały.

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
