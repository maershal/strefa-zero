# I05 — wspólny świat gry i narzędzi

Data: 2026-10-30. Wersja źródeł: 0.7.0, wydanie I05.

Użytkownik zlecił odłożenie I04.5 i przejście do I05. Cel 1,5 ms/tick oraz przyczyna historycznych skoków GPU pozostają nierozwiązane. G3-C i dalsze strojenie nie są kontynuowane. Nie zmieniono równań fizyki, tolerancji QA ani domyślnych ustawień: grupy 256, restrict OFF, full/direct, eksperymenty C i sondy OFF.

## Zmiana zachowania

Gra i narzędzia GPU pokazują jedną aktywną scenę. Przy pierwszym wejściu „Gra” tworzy misję, a „Narzędzia” klepsydrę. Kolejne wejścia zachowują tę scenę, także po powrocie z menu. Scenę zmienia dopiero wybór sceny/reset w narzędziach albo „Nowa misja / Od nowa” w grze.

Przycisk „Narzędzia świata” otwiera edycję bieżącej misji. Pędzel, pauza, pojedynczy krok, liczniki, HP tarcz i stan postaci dotyczą tego samego świata. „Widok gry” wraca bez ponownej kompilacji i bez utraty terenu, cząstek, celu, wyniku czy zdarzeń. Pauza jest wspólna; przycisk „Wznów” jest również w grze. W narzędziach nie są wysyłane przytrzymane polecenia broni z widoku gry. Utrata fokusu czyści wejście i wstrzymuje symulację.

Modele CPU pozostały referencją. `GameSession` powstaje dopiero po wejściu do poligonu CPU; `LaboratoryRunner` i jego wątek dopiero po wejściu do laboratorium CPU. Start `--mission` nie tworzy żadnego z nich. Menu nazywa obie pozycje referencyjnymi. Termika, gazy i ogień wspólnego świata pozostają w I10.

## Własność i kod

| Element | Odpowiedzialność |
| --- | --- |
| `gpu::detail::DeviceContext` | API, możliwości GL, kontrola bieżącego kontekstu i wątku; kontekst SDL pozostaje własnością `Window` |
| `gpu::detail::ShaderLibrary` | Kompilacja i własność programów oraz opcjonalny cache binarny |
| `gpu::detail::SceneRenderer` | Cele renderowania, VAO, rysowanie świata i jawny zapis obrazu |
| `app::SimulationWorld` | Jedyny aktywny `ParticleWorld`, jawna konfiguracja sceny, wspólny zegar i bufor wejścia |
| `app::SessionRecording` | Ograniczony zapis poleceń sceny i jego wczytanie |
| `app::ScenarioRunner` | Sekwencje testów okna, raporty i eksport profilu; korzysta z działań i kroku produkcyjnej aplikacji |
| `app::run_diagnostic_scenario` | Dotychczasowe testy/benchmarki GPU, wydzielone z inicjalizacji aplikacji |

`ParticleWorld` nadal udostępnia istniejące API walidacyjne. Nie wprowadzono drugiego solvera. Transport wyników nadal używa `ReadbackRing` z ochroną generacji i rewizji; zmiana widoku nie zmienia generacji. Jawny reset/zmiana sceny zachowuje dotychczasowe unieważnianie wyników. Zasoby świata i biblioteka programów są niszczone przed kontekstem okna. Renderer unieważnia tracker wiązań po swoim przebiegu.

Dla domyślnej konfiguracji aplikacja tworzy **jeden świat i 42 programy**. Wcześniejsze dwa właściciele oznaczali drugi zestaw programów i buforów po odwiedzeniu obu widoków. Nie traktujemy tej redukcji zasobów jako dowodu przyspieszenia fizyki.

## Cache programów

Włączenie: `StrefaZero.exe --mission --shader-cache on`. Domyślnie OFF. Pliki powstają w `shader-cache/` katalogu profilu. Klucz uwzględnia vendor/GPU, wersję GL i GLSL, profil kontekstu, rozwinięte źródła wraz z include, definicje oraz format binarny. Dane mają wersję, rozmiar, limit i sumę kontrolną. Odczyt wymaga poprawnego linkowania; odrzucony wpis prowadzi do kompilacji źródeł. Brak obsługi binariów lub problem zapisu również pozostawia działającą ścieżkę źródłową.

Po zakończeniu sesji `gpu-startup.txt` raportuje utworzenia świata, liczbę programów, kompilacje, trafienia/chybienia/odrzucenia cache, błędy zapisu i czasy. `first_world_ms` obejmuje inicjalizację świata, a `view_switch_*` otwarcia już istniejącego świata. `compile_ms` i `cache_load_ms` są oddzielne. Gdy cache jest wyłączony, obsługa binariów nie jest odpytywana. Pierwsza kompilacja na nowym sterowniku nie ma obiecanego skrócenia; sterownik może posiadać własny niezależny cache.

Testy wymagające odczytu oryginalnych źródeł z obiektów GL wykonujemy z cache OFF. Binarny program nie musi mieć dołączonych shaderów źródłowych.

## Nagrania i eksport

„Nowe nagranie (reset sceny)” jawnie resetuje aktywną scenę i zaczyna zapisywać jej polecenia. Zapis obejmuje reset/wybór sceny, pędzel oraz rzeczywiście wykonane ticki i ich wejście. Przełączenia widoków nie resetują nagrania. Limit wynosi 65536 poleceń; przekroczenie jest raportowane przy eksporcie, bez publikacji niekompletnej sesji jako poprawnej.

„Zapisz nagranie + obraz” tworzy w profilu `recordings/gpu-…/`:

- `session.txt`: wersja, konfiguracja solvera i polecenia;
- `metrics.csv`: tick zlecony i osobny tick/generacja ostatniej asynchronicznej próbki HUD, statystyki oraz ostatnie czasy;
- `preview.bmp`: jawny zrzut aktualnie renderowanego świata.

Eksport odbywa się poza krokiem fizyki; CSV nie wprowadza synchronicznego odczytu cząstek. Zrzut obrazu jest osobną, synchroniczną czynnością, oznaczoną jako capture w profilu klatki. „Odtwórz ostatnie nagranie” wczytuje ostatnio zapisany plik z tej sesji, sprawdza cały format i konfigurację przed zmianą świata, a następnie wykonuje zapisane kroki tym samym solverem. Jest to odtworzenie wsadowe; długi zapis może potrwać. Wynik pozostaje zatrzymany do wznowienia. Nie jest to binarny snapshot arbitralnego stanu ani obietnica identyczności bitowej długich trajektorii GPU.

## Walidacja

Nowe testy początkowe: **2/2 PASS**.

- `app.shared_session`: 130 klatek, 10 kontroli. Gra → narzędzia → gra → menu → gra; porównanie pełnych stanów cząstek, terenu i misji podczas pauzy, edycja na żywej scenie, pojedynczy krok, jawna zmiana sceny, wspólny zapis/odtworzenie. Jeden świat, 42 programy, zero światów CPU.
- `gpu.shader_cache`: osiem prób z rzeczywistym compute i weryfikacją wyniku bufora: kompilacja, trafienie binarne, uszkodzony wpis i fallback, ponowne trafienie oraz osobne unieważnienie po zmianie include i definicji.

GCC 13.3, Release, `STREFA_BUILD_APP=OFF`, `STREFA_WARNINGS_AS_ERRORS=ON`: **6/6 PASS**. Poprawiono istniejące mylące wcięcia w `world.cpp`, `thermal.cpp`, `scenes.cpp` i teście materiałów, bez zmiany zakresu instrukcji. `strefa_clock_tests` dodano do listy IPO. MSVC 19.51 kompiluje pełną aplikację z `/WX`. Dostępny Clang 18 z libstdc++ 13 nie udostępnia `std::expected`; próba headless kończy się na dotychczasowym interfejsie core/settings. Nie wyciszono ostrzeżeń ani nie podstawiono obejścia biblioteki standardowej.

Pełna regresja Windows: **53/54 PASS** w 743,53 s. `gpu.mission_ui` przekroczył limit 60 s; pozostałe testy, w tym sprzężenie, warianty PBF, sąsiedztwo, transport, GL 3.3 i laboratorium, przeszły. Uruchomienie identycznego scenariusza z limitem 60 s na poprzedniej opublikowanej I04.5-G2 również zakończyło się timeoutem. To potwierdza występowanie problemu poza I05; nie identyfikuje przyczyny. Nie zmieniono limitu testu ani tolerancji. Wynik pozostaje niezaliczony, a odbiór wydajności okna otwarty zgodnie z odłożeniem I04.5.

Dodatkowa kontrola `SessionRecording` skompilowana GCC z ostrzeżeniami jako błędami przeszła roundtrip oraz **12/12 odrzuceń** nieprawidłowego formatu, konfiguracji, poleceń, danych niefinitywnych, limitów i przepełnienia. Kod harnessu i log są zachowane wraz z wynikami.

Kontrola okna misji na I05 również ponownie przekroczyła 60 s. Oba wyniki kontrolne zachowano. Końcowe testy wspólnej sesji i laboratorium oraz pomiary startu opisano poniżej. Historyczne wyniki I04.5 pozostają w swoich raportach i nie są pomiarem I05.

## Końcowy build i start

Po pełnej regresji dodano wyłącznie wyświetlanie już istniejących komunikatów zapisu/odtwarzania w panelu narzędzi. Brak zmian solvera i harmonogramu. Logi oraz `final-checks.json` zachowują osobne SHA256 buildu pełnej regresji i końcowego EXE.

- `previous-mission-window`: exit -1, timeout true.
- `i05-mission-window`: exit -1, timeout true.
- `final-session`: exit 0, timeout false.
- `final-laboratory`: exit 0, timeout false.

| Próba | Kompilacje źródeł | Trafienia cache | Kompilacja [ms] | Wczytanie cache [ms] | Inicjalizacja świata [ms] | Maks. wybór istniejącego świata [ms] |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| cold | 42 | 0 | 40265.6 | 23.5193 | 41033 | 0.0305 |
| warm | 0 | 42 | 0 | 112.767 | 337.564 | 0.0328 |

Obie próby wykonały ten sam scenariusz 130 klatek i 10 kontroli. Każda utworzyła jeden świat, 42 programy i zero modeli CPU. `cold` oznacza pusty cache aplikacji; własny cache sterownika nie był czyszczony. Czas wyboru istniejącego świata obejmuje otwarcie właściciela, bez budowy UI, renderowania i present; nie jest czasem całej klatki. Te pomiary dotyczą startu/zasobów i nie zaliczają celu fizyki 1,5 ms.

[Surowy wynik regresji](i05/ctest.log), [wynik GCC](i05/gcc-ctest.log), [kontrole końcowe](i05/final-checks.json), [pomiary startu](i05/startup-summary.json), [kontrole formatu nagrania](i05/recording-guards.txt).
