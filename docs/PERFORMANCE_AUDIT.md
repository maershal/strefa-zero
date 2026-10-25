# Audyt wydajności i matematyki — Strefa Zero 0.4.2

Pomiar dotyczy obliczeń fizyki, nie tylko płynności okna. 0.4.1 usunęło blokowanie interfejsu, ale nadal wykonywało wiele niepotrzebnych przebiegów po pamięci i kosztownych iteracji ciśnienia. 0.4.2 zmienia te obliczenia oraz poprawia znalezione warunki stabilności.

## Metoda pomiaru

Windows x64, Intel Core i5-9300HF (4 rdzenie / 8 wątków logicznych), MSVC 19.51, Release, `/O2`, IPO/LTO, bez `/fp:fast`. Świat 320 × 192, krok zewnętrzny 1/60 s. Przebieg obejmuje 180 kolejnych kroków sceny, włącznie z początkowym uderzeniem i reakcjami. Nie odrzucamy tych kosztownych kroków jako „rozgrzewki”. Konstrukcja świata, renderowanie i eksport nie są mierzone jako fizyka.

Porównanie wykorzystuje zachowany plik StrefaPhysics.exe z paczki 0.4.1 i nowy plik 0.4.2. Dla pięciu scen powtarzamy kolejność stara–nowa–nowa–stara (ABBA), bez równoległych buildów/testów. Pełny zestaw obejmuje 14 scen oraz poligon. Dodatkowo porównujemy 1/2/4 wątki na tym samym nowym pliku wykonywalnym. Nie zmieniamy planu zasilania Windows ani ustawień Turbo; wyniki nadal podlegają obciążeniu systemu i zmianom taktowania. Raport podaje zakres powtórzeń, nie uniwersalną obietnicę FPS.

| Scena | 0.4.1 mediana [ms], zakres | 0.4.2 mediana [ms], zakres | Przyspieszenie* | p95 stare → nowe [ms] |
|---|---:|---:|---:|---:|
| dam-break | 75.47–83.90 | 54.46–61.76 | 1.37× | 111.30 → 81.53 |
| sand-water | 73.83–77.04 | 53.64–57.09 | 1.36× | 103.16 → 89.02 |
| smoke-vent | 19.52–28.73 | 12.77–14.44 | 1.77× | 34.46 → 25.07 |
| quench-metal | 59.74–64.69 | 36.91–39.47 | 1.63× | 96.75 → 50.05 |
| arena | 10.86–11.32 | 6.47–6.68 | 1.69× | 37.70 → 24.21 |

*Iloraz średniej z dwóch median starej i nowej wersji; p95 również uśrednione z dwóch przebiegów. Zmiany poprawiają również model transportu, więc porównanie wersji obejmuje zarówno implementację, jak i korekty fizyki.

Pełny przebieg końcowej wersji:

| Scena (180 kroków) | mediana | p95 | p99 | maksimum [ms] |
|---|---:|---:|---:|---:|
| falling-water | 23.43 | 55.49 | 71.90 | 86.37 |
| dam-break | 57.50 | 83.99 | 96.90 | 105.52 |
| connected-vessels | 28.88 | 33.48 | 41.53 | 41.73 |
| oil-water | 17.88 | 22.08 | 25.44 | 25.46 |
| sand-pile | 6.80 | 9.26 | 14.05 | 14.55 |
| sand-water | 67.42 | 92.96 | 107.90 | 127.31 |
| smoke-ceiling | 27.50 | 31.12 | 35.78 | 35.82 |
| smoke-vent | 27.26 | 31.62 | 33.99 | 34.68 |
| wood-fire | 36.50 | 49.28 | 89.52 | 109.15 |
| extinguish | 99.06 | 120.80 | 144.29 | 214.28 |
| melting-metal | 38.82 | 46.36 | 48.63 | 50.68 |
| quench-metal | 42.44 | 54.31 | 59.40 | 60.18 |
| ice-fire | 31.64 | 36.26 | 40.21 | 43.12 |
| shielded-explosion | 29.15 | 37.19 | 46.80 | 60.75 |
| arena | 8.74 | 30.41 | 35.40 | 36.76 |

Skalowanie tego samego nowego modelu (120 kroków, dwa przebiegi, bez zmiany stanu końcowego):

| Scena | Wątki | Zakres median [ms] |
|---|---:|---:|
| dam-break | 1 | 65.11–67.29 |
| dam-break | 2 | 55.35–58.49 |
| dam-break | 4 | 54.82–56.34 |
| smoke-vent | 1 | 30.30–30.49 |
| smoke-vent | 2 | 23.68–25.68 |
| smoke-vent | 4 | 18.57–19.66 |

15/15 scen zachowało skończony stan i zbieżność PCG. Maksymalny błąd masy: 3.49e-09; entalpii: 1.67e-06. Dane surowe: [benchmarks](benchmarks/final/all/summary.csv).


## Wdrożone optymalizacje

Porównanie ABBA powyżej używa dwóch wątków nowej wersji. Domyślne dwa są kompromisem między przepustowością a zapasem CPU dla okna. Cztery pomagały szczególnie w gazach; zysk przy zaporze był mały, ponieważ przeważa sekwencyjne ciśnienie. W aplikacji można wybrać `--physics-threads 4`, a w narzędziu `StrefaPhysics` — `--threads 4`. Parametr obsługuje 1–4, bez przebudowy programu.

- PCG korzysta domyślnie z MIC(0), z wagą 0,97 i dodatnim zabezpieczeniem pivota. Zachowana jest poprzednia macierz, warm start, tolerancja `1e-4` i limit 200 iteracji na podkrok. Nie wykonujemy niepotrzebnego preconditioningu po osiągnięciu tolerancji. Na końcu jawnie liczymy `b - A*p` i raportujemy prawdziwe residuum.
- MIC jest zapisany w równoważnej postaci `LDLᵀ`, dzięki czemu przebieg wsteczny wymaga mniej mnożeń, a faktoryzacja nie oblicza pierwiastka dla każdej komórki. Poprzedni IC zachowano jako niezależny wariant porównawczy.
- Dla co najmniej 256 mokrych komórek działa dodatkowa korekcja: `A_c = Pᵀ A P`, z agregatami 8 × 8. Mały operator jest faktoryzowany pasmowym Choleskym. Preconditioner jest sumą dodatnio określonego MIC i `P A_c⁻¹ Pᵀ`; pozostaje liniowy i zgodny z PCG. Równanie końcowe, residuum i ściany pozostają na pełnej siatce. Nieudana faktoryzacja pomocnicza wraca do MIC, a status zbieżności właściwego PCG jest nadal jawny. `--mic-only` pozwala wyłączyć ten etap do porównania.
- Stała pula C++ obsługuje niezależne wiersze pól, cache termiczny, przygotowanie cieczy, akustykę oraz kanały transportu/dyfuzji gazów. Domyślnie są dwa wątki fizyki łącznie z właścicielem świata; na małym CPU jeden. Dostępne są 1–4. Wątki powstają przy tworzeniu świata, czekają przez `atomic::wait` i są dołączane przy zamknięciu. Nie ma tworzenia zadań na stercie w każdym podkroku ani aktywnego oczekiwania.
- Każde zadanie zapisuje rozłączny zakres lub własny kanał. Bilans i sumy energii są redukowane w stałej kolejności. Reakcje, ruch proszków i trójkątne przebiegi MIC pozostają sekwencyjne, ponieważ mają zależności przestrzenne.
- Przepływ cieczy pracuje w prostokącie zawierającym wszystkie niezerowe udziały z marginesem. Dotyczy to również niewidocznych śladów: nie są usuwane dla przyspieszenia. Zerowane są prędkości nieaktywnych ścian. Projekcja zachowuje rozdzielczość cienkich osłon.
- Pomijane są dokładnie zerowe kanały cieczy i gazów, puste światy cieczy oraz wymiana gęstości przy jednym składniku. Niezmieniony świat nie wymaga ponownego liczenia bilansu. Lepkość komórki jest buforowana przed obliczaniem ścian.
- Listy przepływów, składowych spójnych, buforów solvera i wymian ciepła są używane ponownie. Pamięć robocza jest większa niż w 0.4.1, aby kanały mogły pracować bez blokad i bez kolizji zapisów.
- Panel „Koszt obliczeń” oraz pliki `*-profile.csv` pokazują czasy poszczególnych systemów. Czasy ciśnienia/transportu/cache są podzbiorami czasów systemów, więc nie należy dodawać wszystkich kolumn do siebie.

## Korekty matematyczne

**Ciśnienie.** Operator `A = -div((1/rho)*grad)` ma symetryczne połączenia. W szczelnej składowej warunek Neumanna wymaga sumy prawej strony równej zero. Odejmujemy jej średnią przed ustaleniem jednej kotwicy ciśnienia; kotwica nie może działać jako sztuczne ujście objętości. MIC jest preconditionerem, nie zmianą równania. Test niezależnie konstruuje operator o zmiennych współczynnikach i znane rozwiązanie sinusoidalne. Przy tolerancji `1e-10` IC potrzebował 88 iteracji, MIC 43; maksymalny błąd rozwiązania był poniżej `4e-11`. Metoda opiera się na opisie [Bridsona, rozdział o PCG i MIC](https://www.cs.ubc.ca/~rbridson/fluidsimulation/fluids_notes.pdf); implementacja jest własna.

W tym samym teście MIC z korekcją Galerkina potrzebował 39 iteracji i osiągnął maksymalny błąd około `7e-12`. Jest to kontrolowany przykład jakości solvera, nie obietnica takiej samej proporcji czasu dla dowolnej sceny.

**CFL cieczy.** Dla jawnej adwekcji upwind i laplasjanu lepkości, przy `dx = dy = 1`, wystarczający warunek dodatnich wag brzmi `dt*(max|u| + max|v| + 4*nu_max) <= 1`. Poprzedni kod brał tylko większą składową prędkości. Nowy wyznacza podkrok ponownie po każdej projekcji, uwzględnia oba kierunki, faktycznie obecne lepkości oraz zapas na grawitację. Limit wynosi 0,9 i pozostawia dodatni zapas; można ustawić 0,45 w konfiguracji odniesienia. Test porównuje tor swobodnego spadania przy obu limitach. Zmiana podkroków oznacza, że obrazy 0.4.1 i 0.4.2 nie muszą być identyczne. Masa i entalpia pozostają zachowane; nie jest to test bitowej równoważności różnych wersji modelu.

**Powierzchnia i strumień cieczy.** Test zagęszczania czasu wykazał ponad 14 komórek różnicy toru spadania. Dawny pionowy strumień `min(v*dt, fill)` zakładał, że każda częściowo wypełniona komórka ma ciecz na swoim dnie. Płyn wpływający z góry mógł przez to przesuwać małą porcję o całą komórkę w każdym podkroku. Zastąpiono go zachowawczym strumieniem `v*dt*phi_face` w obu kierunkach, z ograniczoną rekonstrukcją liniową minmod; przy zerowym nachyleniu otrzymujemy donor-cell. Nie udaje on geometrycznej rekonstrukcji VOF. Limiter ogranicza rozmywanie śladów i obciążenie ciśnienia bez kasowania małych mas. Zasady takich limiterów opisuje [MPI-AMRVAC](https://amrvac.org/md_doc_2limiter.html); nie deklarujemy pełnej gwarancji TVD dla naszego wielowymiarowego modelu mieszaniny. Dodatkowo prędkość MAC jest przedłużana o dwie ściany poza powierzchnię, z blokadą ścian stałych; nowo zajęte komórki nie tracą pędu wskutek zerowania otoczenia. Po poprawce różnica środka masy przy CFL 0,9 i 0,45 wynosi około 0,184 komórki po 0,5 s. Oba warianty są w odległości poniżej jednej komórki od rozwiązania balistycznego grawitacji. Nie poluzowano testu, aby przepuścić poprzedni błąd.

**Akustyka.** Rozdzielamy stabilność fali od adwekcji składu. Dla naprzemiennej aktualizacji ciśnienia i prędkości MAC stosujemy `c*dt*sqrt(2) <= 0,45`. Nie dodajemy prędkości adwekcji do osobnego równania falowego. Transport gazów ma własny limit odpływu i podkroki. Maksymalna szybkość dostarczania źródła ciśnienia jest proporcjonalna do `dt`; pozostały impuls zostaje w buforze. Równania bazowe opisuje [Clawpack: Acoustics](https://www.clawpack.org/riemann_book/html/Acoustics.html).

**Ciepło.** Ograniczenie pojedynczej pary nie gwarantuje stabilności komórki mającej wiele kontaktów. Zbieramy współczynniki wszystkich wymian. Dla każdej pary mnożymy strumień przez `min(1, 1/load_i, 1/load_j)`, gdzie `load_i = sum(G_ij*dt/C_i)`. Suma wag nowej temperatury nie przekracza jedności, a obie strony pary otrzymują tę samą wartość z przeciwnym znakiem. Test ma komórki o małej pojemności cieplnej i bardzo duży współczynnik kontaktu; sprawdza zasadę maksimum oraz bilans. Skraplanie używa entalpii po bieżącej wymianie, bez opóźnienia temperatury o krok.

**Reakcje.** Aktywacja spalania gazu zależy od ilości paliwa i temperatury, nawet jeśli renderer opisuje mieszaninę jako płomień. Usuwa to błąd pomijający część bardzo gorących gazów oraz zbędną pracę przy zimnym paliwie. Kinetyka gazu i lokalnej separacji cieczy używa `-expm1(-k*dt)`, bez zależności szybkości od liczby podkroków wynikającej z liniowej aproksymacji. Chemia pozostaje modelem do gry.

## GPU i dalsze możliwości

GPU pozostaje używane do renderowania OpenGL; fizyka w tym wydaniu działa na CPU. [GPU Gems, rozdział 38](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu) pokazuje efektywną symulację z polami w teksturach, ale opisany solver nie uwzględnia powierzchni swobodnej naszej wielomateriałowej cieczy. Nie można podmienić nim PCG bez zmiany modelu.

[NVIDIA Best Practices](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#data-transfer-between-host-and-device) zaleca ograniczanie transferów CPU–GPU i pozostawianie danych na karcie między etapami. Tutaj reakcje, edycje, kolizje, entalpia i diagnostyka używają wspólnego stanu CPU. Ocena architektury wskazuje, że częściowy port wymagałby transferów i synchronizacji; nie wykonano benchmarku backendu GPU i nie twierdzimy, że zawsze byłby wolniejszy.

Pełny backend GPU ma sens jako kolejny osobny etap: rezydentne pola, wspólne kernele transportu i ciepła, asynchroniczne próbki diagnostyczne oraz solver ciśnienia przystosowany do GPU. Compute shaders są częścią [OpenGL 4.3](https://registry.khronos.org/OpenGL/specs/gl/glspec43.core.pdf); wariant fragment shaderów może działać na starszym GL, ale wymaga innego organizowania przebiegów. Obecne wymaganie OpenGL 3.3 i wersja CPU bez SDL pozostają zachowane.

Wdrożona korekcja ma dwa poziomy. Kolejnym kandydatem jest pełny cykl wielosiatkowy: [Basilisk Poisson](https://basilisk.fr/src/poisson.h) pokazuje korekcję błędów na różnych skalach. Przy cienkich ścianach, nieciągłej gęstości i rozłącznych zbiornikach potrzebna jest zgodna topologia poziomów. Zwykłe zmniejszenie rozdzielczości właściwego równania mogłoby przepuścić falę lub ciecz przez jednopikselową osłonę. W tym wydaniu mniejszy operator przyspiesza tylko preconditioning; pełna siatka definiuje wynik.

## Weryfikacja i ograniczenia

`sim.numerics_parallel` obejmuje rozwiązanie wzorcowe Poissona, bitowe porównanie 1/2/4 wątków, cykle tworzenia/zamykania puli, stabilność sumy kontaktów cieplnych, dwa szczelne zbiorniki, aktywację gorącego paliwa, zmianę CFL i odrzucanie błędnej konfiguracji. Istniejące testy obejmują fazy, bilanse, cienkie ściany, tlen, osłony, sterowanie i niezależność od FPS. Testy okna używają rzeczywistego SDL/OpenGL.

Końcowy Release przeszedł **8/8 testów CTest** (84,53 s). Osobny build Debug bez SDL/GPU przeszedł test ustawień i test cyklu życia laboratorium. Po instalacji sprawdzono zgodność SHA-256 plików wykonywalnych z buildem oraz trzy uruchomienia przenośne: pełny test ekranów/renderera i laboratorium na dwóch oraz czterech wątkach. Wszystkie zakończyły się kodem 0. Log: [TEST_RESULTS.txt](TEST_RESULTS.txt); szczegóły środowiska i wyniki: [QA.json](QA.json).

Testy przenośnego laboratorium obejmują 300 klatek, pauzę, wznowienie i przejście przez menu, przy ograniczeniu okna do 60 FPS. Na dwóch wątkach mediana klatki wyniosła 16,67 ms, p95 20,19 ms, p99 41,43 ms, maksimum 56,20 ms; na czterech odpowiednio 16,67 / 16,67 / 17,11 / 17,16 ms. Tempo fizyki wyniosło 12,55 i 16,81 kroku/s. Są to krótkie testy integracyjne, bez naprzemiennego powtarzania; nie stanowią samodzielnego dowodu przewagi czterech wątków. Pomiary skalowania w tabeli wyżej dotyczą fizyki bez okna.

To nadal dyskretny model do gry: adwekcja prędkości i gazów pierwszego rzędu, ograniczona rekonstrukcja liniowa udziałów cieczy, brak pełnego PLIC/VOF, pęcherzyków i napięcia powierzchniowego. Nadmiar objętości jest raportowany i korygowany ciśnieniem, nie kasowany. W scenie chłodzenia metalu maksymalne lokalne przepełnienie wyniosło **0,13611 objętości komórki**, wobec 0,00109 w 0.4.1. Jest to pogorszenie lokalnego ograniczenia objętości przy zmienionym transporcie, mimo zachowanego globalnego bilansu i zbieżności PCG; wymaga dalszej poprawy sprzężenia strumieni z przemianami fazowymi. W pozostałych 14 scenach maksimum było mniejsze niż 0,00005. Testy bilansu nie dowodzą ścisłej nieściśliwości każdej komórki.

Lokalny gaz może pozostawać uwięziony pod materiałem — nie jest to pełne sprzężenie objętości gazu i cieczy. Bilans entalpii nie obejmuje energii kinetycznej i potencjalnej. Przyspieszenie nie gwarantuje 60 kroków fizyki/s w każdej ciężkiej scenie. FPS interfejsu i tempo symulacji są mierzone osobno. Poligon nadal posiada synchroniczną pętlę logiki gry.
