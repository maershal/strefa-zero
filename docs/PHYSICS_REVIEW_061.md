# Przegląd fizyki 0.6.1: klepsydra, woda i olej

## Co było niewystarczające w 0.6.0

1. Geometria kontaktu była sumą kwadratów. Skośna ściana narysowana pędzlem składała się z małych poziomych półek. Tarcie działało względem tych półek, zamiast ciągłego skosu. Test swobodnego spadku i usypu nie sprawdzał opróżniania lejka.
2. Woda i olej miały różne masy, ale brakowało sił zależnych od rodzaju fazy. Wspólne ciśnienie i lepkość nie tworzyły samodzielnie spójnej granicy faz. Pojedyncze nośniki oleju mogły odrywać się i mieszać.
3. Test warstw zaczynał od oleju na wodzie. Potwierdzał utrzymanie prawidłowej kolejności, ale nie sprawdzał wypływania zanurzonego oleju ani spójności kropli. Ten limit był zapisany w raporcie 0.6; nowe testy sprawdzają silniejsze warunki.
4. Pochodna Poly6 użyta do wszystkich korekt słabnie przy małej odległości. W naszych testach powodowała nienaturalnie stabilne ułożenie odwróconych faz. Zmiana promienia sąsiedztwa i samo zwiększanie kohezji nie rozwiązywały tego problemu. Przejście na operator Spiky przy zachowaniu estymatora gęstości Poly6 usunęło blokowanie przepływu.
5. Bezpośrednie dodawanie małych korekt do dużych współrzędnych FP32 gubiło ruch mniejszy od jednostki reprezentacji liczby. To osobny błąd numeryczny, wykrywany przez nowy test bardzo małego przemieszczenia, niezależnie od testu wyporności.

## Model i źródła

Wspólny PBD/PBF z różnymi masami pozwala modelować wyporność. Dla faz niemieszających się można stosować wspólny warunek objętości oraz kohezję wyłącznie wewnątrz fazy. Takie podejście opisuje [Macklin i in., Unified Particle Physics, §6–7](https://mmacklin.com/uppfrta_preprint.pdf). Autorzy omawiają też zależność tarcia od metody projekcji. Nasza wersja pozostaje uproszczeniem 2D do gry.

Symetryczne siły parowe są praktycznym sposobem modelowania granic faz. Ich związki z efektywnym napięciem opisuje [praca o PF-SPH i przepływach międzyfazowych](https://orca.cardiff.ac.uk/id/eprint/107793/1/Pairwise%20-%2007932108.pdf). Parametry tutaj mają skalę gry; nie są pomiarem napięcia olej–woda w N/m.

### Kontakt piasku ze skosami

Na GPU po zmianie terenu powstaje lokalne pole odległości ze znakiem, zapisane obok maski ścian. Skalar jest interpolowany kubicznym B-splinem, a normalna jest jego pochodną. Projekcja Newtona rozwiązuje warunek `φ(x) >= r`, z ograniczeniem długości korekty i trzema iteracjami. Płaska ściana zachowuje położenie, natomiast subpikselowe schodki na skosie stają się zaokrąglonym konturem. W wypadku zdegenerowanej normalnej pozostaje awaryjny kontakt koło–AABB.

Ciecze nadal używają dokładnego koło–AABB, aby zachować prześwit jednokomórkowych kanałów. Test sprawdza zarówno zamkniętą ścianę, jak i przepływ kropli przez taki otwór. Kontur dla ziaren jest przybliżeniem w skali piksela, a nie dokładnym kształtem każdego kwadratu. Bardzo ciasne, szorstkie przewężenia nadal mogą się zakleszczyć; nie ma gwarancji opróżnienia dowolnej geometrii mniejszej niż kilka średnic ziarna.

Współczynnik tarcia piasek–ściana wynosi 0,28; kontakt ziaren pozostaje silniej cierny. Współczynnik pary ziaren jest symetryczny (średnia geometryczna), zamiast zależeć od kolejności cząstek. Nowe cząstki z pędzla otrzymują deterministyczne odchylenie początkowe do ±0,06 komórki, żeby nie tworzyć idealnej kryształowej siatki. Odchylenie nie jest dodawane w kolejnych krokach i nie rozpycha gotowych zatorów. Testy dwóch przeciwnych skosów oraz poziomej półki sprawdzają brak sztucznego dryfu.

### Rozdział cieczy

Gęstość nadal korzysta ze znormalizowanego 2D Poly6, z promieniem h=2 i dotychczasową gęstością odniesienia. Operator korekty ciśnienia jest teraz gradientem 2D Spiky:

```
G(d) = -30 / (π h⁵) · (h-r)² · d/r,    0 < r < h
```

Zarówno mianownik λ, jak i korekta pozycji korzystają z G. To przybliżony Jacobian, a nie dokładna pochodna estymatora Poly6. Takie rozdzielenie estymatora i operatora stosuje również [oryginalny PBF, §3](https://mmacklin.com/pbf_sig_preprint.pdf). Nie deklarujemy już ścisłego kroku Newtona dla Poly6; poprawność ograniczenia objętości ocenia rzeczywisty błąd gęstości, niezależnie od operatora korekty.

Woda i olej zachowują osobne, stałe masy i identyfikatory. Stosunek gęstości olej/woda wynosi 0,8. Nie ma zamiany etykiet, sortowania materiałów według wysokości ani dodatkowej siły nakazującej olejowi poruszać się w górę.

Kohezja pochodzi z potencjału parowego `U_ij=-κ W_Poly6(r)`, κ=60 w jednostkach gry. Działa tylko między cząstkami tej samej fazy. Siła `F_ij=κ∇W_Poly6(x_i-x_j)` jest centralna i przeciwna dla obu członów pary. Nie używamy Spiky do tej siły, ponieważ tutaj potrzebna jest pochodna zadanego potencjału. Ciśnienie utrzymuje objętość; kohezja ogranicza rozpad zanurzonej kropli. Na swobodnej powierzchni olej może rozlać się na oddzielne fragmenty cienkiej warstwy — nie wymuszamy zachowania jednego sztywnego obiektu.

Lepkość XSPH zachowuje symetrię masową. Dla pary stosujemy średnią harmoniczną współczynników 1,5/s (woda) i 18/s (olej), więc mieszana para ma około 2,77/s. Waga czasowa pozostaje `1-exp(-k dt)`. Osobny test sprawdza poziomy pęd mieszanej pary po odjęciu jawnego oporu powietrza. Potencjał fazowy nie jest pełnym modelem kąta zwilżania ani skalibrowanym napięciem powierzchniowym w jednostkach SI.

### Precyzja i koszt

Stan cząstki ma teraz 64 bajty std430. Dodatkowe cztery floaty przechowują resztę zaokrąglenia pozycji i jej poprzednią wartość. Dodawanie przesunięcia jest kompensowane (`precise` w GLSL zapobiega algebraicznemu usunięciu kompensacji). Prędkość i odległości sąsiadów uwzględniają te reszty. To nie jest obliczanie całego solvera w FP64, ale małe korekty nie giną przy dodawaniu do współrzędnych rzędu setek.

Scena zna zakres używanych identyfikatorów, więc compute pomija pusty ogon bufora 32768 miejsc. Dodanie materiału pędzlem konserwatywnie przywraca pełny zakres, ponieważ atomowy przydział może wykorzystać dowolny slot. Nie ma odczytu pełnego stanu w interaktywnej pętli. Mniejszy zakres nie zmienia kroku, współczynników ani liczby iteracji.

## Testy zachowania

Klepsydra powstaje przez publiczne `ParticleWorld::paint`, tak samo jak rysowana w UI: skośne boki, szyjka i dolna komora. Test sprawdza przepływ, zachowanie identyfikatorów oraz mas, ucieczki przez ściany i osobno zamkniętą szyjkę. Nie ma losowego rozpychania zatorów, przenoszenia ziaren pod lejek ani kodu rozpoznającego klepsydrę w solverze.

Odwrócone warstwy zaczynają od cięższej wody na oleju. Małe sinusoidalne zaburzenie granicy usuwa idealną symetrię początkowej siatki; dalszy ruch wynika z solvera. Druga scena zawiera kroplę oleju zanurzoną w wodzie. Spójność jest sprawdzana podczas zanurzenia (sąsiedztwo do 2,1 komórki). Na końcu sprawdzamy udział oleju przy powierzchni i zapisujemy liczbę cząstek w największym połączonym fragmencie. Pierwotne wymaganie jednego fragmentu także po rozpłynięciu na powierzchni zostało zastąpione warunkiem wynurzenia; rozdzielone fragmenty warstwy są jawnie pokazywane w obrazach i metrykach.

Wszystkie testy wykorzystują rzeczywisty OpenGL 4.3. FPS obrazu nie zmienia kroku fizyki. Licznik czasu GPU nie obejmuje eksportu BMP i odczytów walidacyjnych. Zegary GPU i obciążenie pulpitu nie są kontrolowane. Limit pojemności nie jest deklaracją utrzymania 60 FPS przy dowolnej scenie.

### Wyniki

| Próba | 0.6.0: pomiar bazowy | 0.6.1: końcowy pomiar |
|---|---:|---:|
| Klepsydra: przesypane ziarna po 20 s | 9,34% | 100% z 3084 |
| Przecieki przez ściany | 0 | 0 |
| Zamknięta szyjka: zatrzymane ziarna | 3084 | 3084 |
| Odwrócone warstwy: średnie y woda / olej po 15 s | 136,08 / 154,61 | 151.287 / 137.336 |
| Zanurzona kropla: końcowe średnie y oleju | 149,15 | 125.818 |

Oś y rośnie w dół. W końcowym przebiegu 100% oleju z kropli dotarło do pasa powierzchni. Spójność podczas zanurzenia: 100%. Największy fragment warstwy po rozpłynięciu: 51.4%. Nie ukrywamy rozdzielenia powierzchniowej warstwy na fragmenty.

Pojedyncze ziarna na przeciwnych skosach przemieściły się o 79.7712 / -80.5649 komórki; na płaskiej półce dryf wyniósł 0. Błąd bardzo małego przemieszczenia: 3.76429e-11 komórki. Błąd poziomego pędu pary woda–olej: 5.42149e-09. Kropla przeszła przez otwór szerokości jednej komórki (końcowe x=162.829).

| Scena | Cząstki | Mediana GPU [ms] | p95 | p99 | max |
|---|---:|---:|---:|---:|---:|
| Klepsydra | 3084 | 0.813056 | 0.824928 | 0.8328 | 1.47462 |
| Olej pod wodą | 3840 | 2.34522 | 2.4456 | 2.49446 | 2.87011 |
| Kropla oleju | 3840 | 2.14845 | 2.26582 | 2.31926 | 2.80957 |

Źródłowy test okna z piaskiem i wodą: mediana 16.6681 ms, p95 16.6686 ms, p99 16.6858 ms, maksimum 18.5006 ms; pominięty czas 0 s. Limit okna wynosił 60 FPS. Pojedynczy krok GPU ma budżet 8,33 ms przy 120 Hz.

Przeszło 13/13 testów: pięć GPU/GL 3.3 i osiem pozostałych CPU/aplikacji. Przenośna paczka przeszła dodatkowo test przepływu oraz okna. Testy paczki częściowo pokrywały się z testami CPU, dlatego tabela wydajności używa szeregowego przebiegu źródłowego. Nie wyprowadzamy mnożnika przyspieszenia z czasów przebiegu bazowego: obciążenie pulpitu nie było kontrolowane.

Dane: [fizyka](gpu061/build/gpu-flow-tests.txt), [przebieg w czasie](gpu061/build/gpu-flow-history.csv), [benchmark](gpu061/build/gpu-flow-benchmark.csv), [pomiary cieczy](gpu061/build/gpu-liquid-benchmark.csv), [testy GPU](TEST_GPU_061.txt), [testy CPU](TEST_CPU_061.txt), [QA](QA-0.6.1.json).

Obrazy: [klepsydra przed poprawką](gpu061/baseline/flow-9-2400.bmp), [klepsydra po poprawce](gpu061/build/flow-9-2400.bmp), [odwrócone warstwy po rozdzieleniu](gpu061/build/flow-10-1800.bmp), [olej na powierzchni](gpu061/build/flow-11-1800.bmp).
