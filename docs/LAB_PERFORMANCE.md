# Laboratorium 0.4.1 — usunięcie blokowania okna

W 0.4 fizyka pracowała w tej samej pętli co SDL i ImGui. Pojedynczy krok trwał dziesiątki milisekund, a FixedClock próbował nadrabiać do ośmiu kroków w jednej klatce. To powodowało kilkusetmilisekundowe przerwy w obsłudze myszy i przycisków.

W 0.4.1 laboratorium ma jeden osobny wątek symulacji. SDL, ImGui i OpenGL pozostają na wątku okna. Solver publikuje niezmienne obrazy i próbki diagnostyczne; okno nie blokuje się na World::step i nie czyta zmienianego świata. Polecenia pędzla, pauzy, resetu, nagrywania i kroków przechodzą przez ograniczoną kolejkę. Wątki nie współdzielą modyfikowanego stanu materiałów.

Nie ma serii nadrabiania po osiem kroków. Każdy krok nadal integruje dokładnie 1/60 s modelu. Jeśli CPU nie nadąża, czas symulacji biegnie wolniej, a interfejs działa dalej. Panel pokazuje osobno faktyczne kroki/s i koszt fizyki. Tekstura jest wysyłana do GPU tylko po otrzymaniu nowego obrazu; pauza i samo przesunięcie kursora nie przeliczają całej mapy.

PCG korzysta teraz ze zwartego bufora mokrych komórek i cache pięciopunktowego operatora. Wersja odniesienia pozostaje dostępna przez PhysicsConfig::reference_pressure. Test porównuje ciśnienie, prędkości i objętości obu solverów w trzech scenach. Tolerancja PCG 1e-4, liczba iteracji, siatka i reguły materiałów pozostały takie same. Usunięto też powtórny pełny odczyt termiki po zakończonym przewodzeniu.

Test app.laboratory_responsiveness wykonuje 300 klatek ze sceną przerwanej zapory, pauzą, wznowieniem oraz przejściem do menu. Przy ograniczeniu okna do 60 FPS zmierzono: mediana 16,6684 ms, p95 16,6694 ms, p99 16,9920 ms, maksimum 17,2516 ms. Ostatni krok fizyki trwał 91,0151 ms. Test sprawdza więc niezależność obsługi okna od ciężkiego solvera.

W natywnym oknie zaobserwowano także 144 FPS interfejsu przy około 25 krokach fizyki/s. Ręczna próba pauzy, nagrywania i trzech pojedynczych kroków utworzyła dokładnie trzy rekordy CSV, jedną klatkę BMP i index.html.

Zestaw Release: 6/6 testów zaliczonych. Po poprawce zapisu raportu ponownie uruchomiono oba testy aplikacji: 2/2. Surowe wyniki są w LAB_UI_PERFORMANCE.txt i TEST_RESULTS.txt.

Ograniczenie pozostaje: rozległe ciecze, ciepło i reakcje nadal bywają zbyt kosztowne dla 60 kroków fizyki/s. W osobnych przebiegach CPU mediana demonstracji wyniosła 51,47 ms, a przerwanej zapory 81,24 ms; taktowanie i obciążenie systemu nie były kontrolowane, więc nie deklarujemy na tej podstawie stałego przyspieszenia całego modelu. Poprawka usuwa zamrażanie interfejsu i zachowuje dokładność modelu. Animacja materiałów w ciężkich scenach może aktualizować się rzadziej niż interfejs.
