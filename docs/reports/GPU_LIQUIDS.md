Dokument historyczny 0.6.0. Zmiany modelu w 0.6.1: [przegląd klepsydry i cieczy](PHYSICS_REVIEW_061.md).

# Strefa Zero 0.6 — ciecze na GPU

Woda, olej i ziarna działają razem w laboratorium GPU. Pięć nowych scen obejmuje zbiornik, przerwaną zaporę, naczynia połączone, warstwy oleju i wody oraz piasek w wodzie. Powierzchnia cieczy powstaje z pola pokrycia cząstek, z zachowaniem pikselowej estetyki. Renderowanie nie zmienia fizyki. Start: `StrefaZero.exe --gpu-laboratory`.

## Wybór metody i zakres

Zastosowano własny, dwuwymiarowy wariant [Position Based Fluids — Macklin i Müller](https://mmacklin.com/pbf_sig_preprint.pdf). Ciągłe cząstki pasują do istniejącego solvera kontaktów i łatwo pozostają na karcie pomiędzy symulacją a rysowaniem. Ta wersja jest alternatywnym modelem do gry. Nie jest przeniesieniem CPU/MAC/PCG na GPU i nie deklaruje równoważności obu modeli ani przyspieszenia tego samego równania o określony mnożnik.

Ogień, entalpia, przemiany fazowe, gazy, reakcje oraz poligon nadal korzystają z oddzielnego świata CPU. Nie ma jeszcze sprzężenia termicznego z cieczą GPU. Wymagania pozostają bez zmian: C++23, SDL3, Dear ImGui; GPU potrzebuje OpenGL 4.3, a tryby CPU nadal obsługują 3.3. Bez CUDA, nowych bibliotek i odczytu całego stanu w pętli interaktywnej.

## Matematyka

Jednostką długości jest komórka, czas jest w sekundach. Każdy nośnik zajmuje umowną objętość 1. Masa jest przypisana do materiału i nie zmienia się podczas ruchu. Historyczną jednostką masy pozostaje ziarno piasku; względne gęstości piasek/woda i olej/woda wynoszą odpowiednio 2,65 i 0,8. To skala gry, nie pełny model sedymentacji w jednostkach SI.

Dla odległości `r < h = 2` używany jest znormalizowany **2D** Poly6:

```
W(r) = (4 - r²)³ / (64π)
∇W(d) = -6 (4 - |d|²)² d / (64π)
```

Poza promieniem h obie wartości są zerowe. Pochodna pochodzi z tego samego jądra; nie mieszamy normalizacji 3D z symulacją 2D. Suma na jednostkowej siatce kwadratowej daje odniesienie `n0 = 1.014612762210833`.

```
C_i = max(Σ_j W(x_i-x_j) / n0 - 1, 0)
λ_i = -C_i / (w_i |∇_i C_i|² + Σ_j w_j |∇_j C_i|² + 0.0001)
Δx_i = (w_i / n0) Σ_j (λ_i + λ_j + s_corr) ∇W(x_i-x_j)
```

`w` to odwrotność masy. Płyn wymusza ograniczenie objętości, a nie jednakową masę wody i oleju. Ziarna mają własne więzy kontaktów; uczestniczą w sumie zajętości płynu, a korekta działa na obie strony. Nieruchome próbki granicy w środkach komórek ścian mają odwrotną masę zero. Granica jest przybliżeniem geometrycznym; nie jest analitycznym warunkiem hydrostatycznym.

Stosujemy tylko dodatnie błędy gęstości: nie ma przyciągania wymuszającego pełną gęstość na swobodnej powierzchni. Mała symetryczna repulsja płyn–płyn `s_corr = -0.0001 (W(r)/W(0.6))⁴` ogranicza skupiska. Nie jest skalibrowanym napięciem powierzchniowym. Korekta jest relaksowana współczynnikiem 0,8 i ograniczona do 0,2 komórki. Ograniczenie poprawia stabilność, ale oznacza niedokładne więzy i brak gwarancji zachowania energii mechanicznej/pędu po projekcji na ściany.

Zewnętrzny krok wynosi 1/120 s i zawiera dwa podkroki 1/240 s. Każdy podkrok ma 8 projekcji gęstości, a w obecności ziaren również 12 projekcji kontaktów. Pozycje poprzednie służą do odtworzenia prędkości oraz osobno do interpolacji obrazu. Grawitacja wynosi 180 komórek/s²; limit prędkości to 80 komórek/s. Te ograniczenia są częścią modelu, nie automatyczną adaptacją CFL dla dowolnego źródła energii.

Lepkość jest przybliżona przez symetryczne wygładzanie XSPH. Waga czasowa wynosi `1-exp(-k dt)`, z k=18/s dla par oleju i 1,5/s dla pozostałych par cieczy. Wymiana ma czynnik masowy `2 w_i/(w_i+w_j)`. Wygładzanie tłumi energię; parametr nie jest fizyczną lepkością dynamiczną w Pa·s. Granica woda–olej zachowuje tożsamości nośników, ale nie ma osobnego równania napięcia międzyfazowego ani gwarancji rozdzielania dowolnie zmieszanej emulsji.

## Szczelność i przepływ

Ruch przewidywany sprawdza kolizję koło–AABB w czterech mikrokrokach. Korekty PBF przechodzą przez dwie połówki z ponowną projekcją na teren. Cienka ściana blokuje też oddziaływania ciśnieniowe i lepkościowe: dokładne przejście po komórkach odrzuca sąsiada znajdującego się po drugiej stronie. Cache sąsiedztwa ścian pozwala pominąć przejście w otwartej przestrzeni. Jest odbudowywany po każdej edycji terenu, nie w każdej iteracji.

Świat 320×192 ma zamknięte brzegi. Ciecze nie znikają na krawędziach. Limit 32768 obejmuje wszystkie aktywne cząstki; pędzel nie tworzy kolejnych po jego osiągnięciu. Usuwanie pędzlem jest jawną edycją masy. Malowanie ściany usuwa pokryte nośniki. Solver sam nie tworzy ani nie usuwa cząstek.

## Architektura i koszty

`gpu::ParticleWorld` posiada bufory ping-pong, listy sąsiadów, teren, mnożniki PBF oraz render targety. `granular.comp` obsługuje predykcję, kontakty i edycje, `liquids.comp` — gęstość, korekty i lepkość; `particle_common.glsl` definiuje wspólne ABI i kolizje. Każdy przebieg zapisuje tylko własny identyfikator. Wyjątkiem są całkowitoliczbowe atomiki do list i redukcji diagnostycznych. Bariery SSBO oddzielają zależne przebiegi.

Nie ma oczekiwania na GPU ani pełnych odczytów stanu w klatce gry. Pierścień zapytań czasowych jest sprawdzany bez blokowania. 32 bajty liczników i maksimum błędu przechodzą przez bufor pośredni i fence. Metryka ściśnięcia jest aktualizowana co 30 kroków i po edycjach; jest to `max(n/n0-1,0)`, **nie ciśnienie w paskalach**. Nie należy nazywać jej residuum PCG.

Na klatkę rysowane są addytywne splaty wody i oleju do dwóch kanałów tekstury RG16F, następnie powierzchnia i ziarna. Stan jest interpolowany między krokami. Wygładzona powierzchnia może nieznacznie wyjść poza promień fizycznego nośnika; ściany są rysowane na wierzchu. To zabieg obrazu, bez wpływu na bilans masy.

Optymalizacja mieszanki polega na oddzielnym budżecie 8 iteracji cieczy i 12 kontaktów; nie uruchamiamy niepotrzebnych czterech projekcji cieczy. W scenach bez ziaren pomijane są ich projekcje. Flagi aktywności są konserwatywne po edycji — spóźniony odczyt licznika nie może wyłączyć potrzebnego solvera. Reset przywraca dokładne flagi. Usunięcie ostatniej kropli pędzlem może więc pozostawić koszt solvera do resetu.

## Weryfikacja

`--gpu-liquid-test --profile <osobny-folder>` sprawdza sumę gęstości względem niezależnego obliczenia CPU w double, zmniejszenie błędu ściśniętej próbki, kolizję i brak wpływu przez cienką ścianę, edycję cache ścian, liczby wody/oleju, zachowanie identyfikatorów i mas, 30/60/144 FPS oraz pięć scen. Naczynia pracują przez 1200 kroków, inne sceny przez 480. Test pełnych 32768 nośników trwa 60 kroków i sprawdza skończoność oraz masę; nie dowodzi płynności przy pełnej pojemności.

Różnica poziomów naczyń jest estymowana z dwukrotności różnicy średnich wysokości nośników, odpowiedniej dla prostokątnych ramion z równomierną objętością. To test wyrównywania poziomów, nie pomiar p=ρgh. Test warstw oleju i wody sprawdza zachowanie poprawnie ułożonych warstw; nie dowodzi separacji odwróconych warstw czy emulsji. Test FPS izoluje pojedynczą kroplę: sumowanie sąsiadów na GPU nie jest bitowo deterministyczne dla gęstego płynu.

Testy oraz benchmarki zapisują BMP po 120 krokach i na końcu sceny. Nie jest to jeszcze interaktywne nagrywanie GPU co trzy kroki. Odczyty, eksport, renderowanie i synchronizacja testu są poza zapytaniem `GL_TIME_ELAPSED`; pomiar obejmuje oba podkroki i okresową diagnostykę. Pełne klatki aplikacji są mierzone oddzielnie w teście okna z mieszanką piasku i wody, pauzą i przejściem przez menu.

### Wyniki paczki Windows

GTX 1650, i5-9300HF, Release / IPO. GPU wykonuje 120 kroków na sekundę symulacji, więc budżet pojedynczego kroku wynosi 8,33 ms, a nie 16,67 ms. Czasy nie obejmują rysowania. Taktowania i inne aplikacje pulpitu nie były kontrolowane.

| Scena | Cząstki | Mediana [ms] | p95 | p99 | max | maks. ściśnięcie |
|---|---:|---:|---:|---:|---:|---:|
| Zbiornik | 4760 | 4.1607 | 4.62643 | 4.83114 | 4.94592 | 0.79% |
| Przerwana zapora | 4644 | 4.58883 | 5.04013 | 5.1712 | 5.3361 | 1.35% |
| Naczynia połączone | 7662 | 4.37776 | 4.89603 | 5.03046 | 5.24928 | 1.48% |
| Woda i olej | 6664 | 4.10941 | 4.62435 | 4.72838 | 4.97808 | 1.13% |
| Piasek w wodzie | 5284 | 5.95648 | 6.48397 | 6.61962 | 6.94304 | 3.28% |

Okno, mieszanka piasku i wody: mediana 16.6682 ms, p95 16.677 ms, p99 16.9292 ms, maksimum 32.1338 ms. 240 klatek, 409 kroków, pominięty czas 0 s. Test ma limit 60 FPS oraz celowe pauzy/menu; nie jest pomiarem maksymalnego FPS.

Bezpośredni test buildu miał p95 16,675 ms, ale także pojedynczą klatkę 100,4 ms i 0,0667 s pominiętego czasu. W przebiegu paczki nie było pominiętego czasu, a maksimum nadal przekraczało budżet jednej klatki. Średnia płynność jest dobra, jednak te pomiary nie gwarantują braku sporadycznych opóźnień systemu/sterownika. [Drugi przebieg okna](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu060/build/gpu-ui.txt).

Błąd sumy gęstości względem CPU double: 5.67815e-07. Ściśnięta próbka: 0.360201 → 2.40803e-05 po 80 projekcjach testowych. Różnica estymowanych poziomów naczyń: 0.666572 komórki. Piasek: średnie y=183.192, woda mieszanki: y=174.936 (oś y w dół). We wszystkich scenach zachowano masy i tożsamości nośników.

Pełny zestaw 12/12 przeszedł w 125,78 s. Po końcowej zmianie proporcji gęstości przeszły ponownie cztery testy GPU/GL 3.3. Przenośna paczka przeszła test cieczy i okna; wymuszone GL 3.3 poprawnie pomija GPU kodem 77. [Pełne logi](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/legacy/TEST_RESULTS-0.6.0.txt), [końcowe testy](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/legacy/TEST_GPU_060.txt), [QA](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/legacy/QA-0.6.0.json).

Surowe wyniki: [paczka](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu060/runtime/gpu-liquid-benchmark.csv), [build](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu060/build/gpu-liquid-benchmark.csv), [fizyka](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu060/runtime/gpu-liquid-tests.txt), [okno](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu060/runtime/gpu-ui.txt).

Pierwszy pomiar roboczy dawał około 8,09 ms dla mieszanki; oddzielenie budżetów iteracji obniżyło go do około 5,81 ms przed końcową korektą mas. To obserwacja dwóch przebiegów rozwojowych, nie kontrolowany benchmark A/B. Tabela powyżej dotyczy dostarczonego wariantu.

Pozostały zakres: powiązanie entalpii z nośnikami, przepływ i tlen na siatce, przemiany fazowe, wymiana z terenem oraz lokalne zapytania dla poligonu. Rozbudowa wymaga zachowawczego bilansu źródeł i produktów. Dotychczasowe pomiary nie gwarantują tej samej wydajności po dodaniu całej termochemii.
