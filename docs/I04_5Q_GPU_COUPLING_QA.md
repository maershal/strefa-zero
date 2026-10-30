# I04.5-Q — wiarygodna weryfikacja sprzężenia

Prace: 2026-10-30. Baza: lokalne A/F/C/B/D/E0 na `6f318ea`. **Pełny CTest 38/38 w nowym protokole Q; 12/12 powtórek, 1080 wspólnych stanów i 12/12 odrzuconych prób z celowo wyłączonym sprzężeniem.** Zmieniono metodę odbioru testu, nie solver. Nie oznacza to zaliczenia dawnego porównania niezależnych trajektorii ani zapewnienia bitowej deterministyczności całej symulacji.

## Co powodowało fałszywe odrzucenie

Stara reguła porównywała po 90 krokach trzy niezależne światy: referencję A, referencję B i local. Progi wynosiły `max(minimum, 2 × różnica A/B)`. Jedna przypadkowo zbieżna para referencji mogła więc ustawić próg dużo ostrzej niż inna para, mimo identycznej implementacji. Był to problem kalibracji kryterium, nie dowód wykrycia konkretnej usterki sprzężenia.

Dodano kontrolę A/A: trzeci świat także używa referencji, z tymi samymi ustawieniami, sceną, wejściem i regułą oceny. **Stara reguła odrzuciła 1 z 16 takich prób.** W próbie 9 różnica centroidu A/B wyniosła 0,00341257, próg `max(0,02; 2 × 0,00341257)` dał 0,02, a różnica trzeciej identycznej referencji względem A wyniosła 0,023376. Samo to wystarczyło do odrzucenia.

To wynik tej serii na jednym GPU, nie oszacowanie ogólnej częstości błędów. Seria powstała na wstępnym buildzie kontrolnym `1619fd316025e2be7b81fec9dea0938be39e580af5879ff40ff682a52b366900`. [Wszystkie 16 wyników](gpu-i04-5q/reference-only-results.json), [próba 9](gpu-i04-5q/reference-only/9/coupling-tests.txt), [rekonstrukcja reguły](gpu-i04-5q/coupling-qa-analysis.json), [hash kontroli](gpu-i04-5q/control-build.json).

Kod zawiera kompakcję aktywnych ID i reakcji przez atomiki oraz sumowanie float według uzyskanej kolejności. Stałe drzewo redukcji nie ustala kolejności jego wejścia. To mechanizm zgodny z obserwowaną zmiennością; nie izolowano wszystkich możliwych źródeł różnic GPU. Dowód A/A dotyczy bezpośrednio wadliwości reguły odbioru. [Referencja i test](../src/gpu/coupling_validation.cpp), [kompakcja ID](../assets/shaders/granular.comp), [redukcja referencyjna](../assets/shaders/mission.comp), [kolejka reakcji](../assets/shaders/particle_common.glsl).

## Nowy protokół Q i jego granice

| Własność | Dawna reguła | Q |
| --- | --- | --- |
| Dane wejściowe porównania | trzy niezależne długie trajektorie | ten sam autorytatywny stan przed każdym krokiem |
| Referencja kontrolna | jedna różnica dwóch pełnych trajektorii kalibruje próg | powtórka referencji kontroluje odtwarzanie, bez rozszerzania progów |
| Próg | losowo zależny od pojedynczej pary | stałe najniższe dotychczasowe minima |
| Zakres | local względem referencji | referencja, local i produkcyjne combined |
| Negatywna kontrola fizyki | brak celowo usuniętego sprzężenia w tej scenie | rzeczywiste wyłączenie sprzężenia musi zostać odrzucone |

Referencyjny świat nadal wykonuje ciągłą trasę 90 ticków. Scena ma 1443 cząstki cieczy/ziaren, podłogę i kontakt z kapsułą; w ticku 45 postać jest przenoszona jak w dawnym teście. Przed każdym krokiem zapisuje się pełny stan cząstek i postaci. Repliki otrzymują go przez produkcyjne ładowanie danych i testową funkcję odtworzenia stanu misji, z własną generacją oraz zachowanym licznikiem ticków silnika. Następnie każda wykonuje ten sam rzeczywisty krok GPU i to samo wejście.

Odtwarzanie dotyczy tej sceny bez pocisków i zdarzeń destrukcji. Nie jest ogólnym zapisem sesji ani realizacją I05. Pola masy i materiału każdego aktywnego ID muszą pozostać identyczne; stan musi być skończony, kolejki opróżnione, bez przepełnienia, a realne reakcje obecne w local i combined. Pozycję porównuje się z kompensacją. Odtwarzanie i synchroniczne odczyty występują wyłącznie w walidacji, nie w pętli gry ani benchmarku.

Nie podniesiono wartości progów: **0,05 dla średniej różnicy cząstek, 0,02 dla średniego przesunięcia wektora cząstek i 0,15 dla postaci**. Teraz obowiązują jako stałe budżety każdego kroku i skumulowanego defektu na wspólnych stanach. Sumuje się nieujemną średnią różnicę pozycji, a osobno wektory średniego przesunięcia cząstek i postaci; normy tych sum także muszą zmieścić się w budżetach. Centroid jest średnią arytmetyczną aktywnych cząstek, nie środkiem masy.

**Zmienił się eksperyment i kryterium odbioru.** Skumulowany defekt przy wspólnych stanach nie jest granicą rozbieżności dwóch niezależnych, nieliniowych trajektorii. Dawna trasa 90 kroków nadal sprawdza skończoność, ID i rzeczywiste reakcje oraz zapisuje stare różnice; jej statystyczny iloraz jest diagnostyką. W serii nowych testów dawny warunek liczbowy nie przeszedł w 1/12 prób — ten wynik zachowano. Nie przedstawiamy 38/38 Q jako naprawy ani zaliczenia dawnego warunku.

Można jawnie uruchomić dawną regułę dla local albo kontrolę trzech referencji:

```powershell
.\StrefaZero.exe --gpu-coupling-check --profile .\qa-q
.\StrefaZero.exe --gpu-coupling-legacy-check --profile .\qa-legacy
.\StrefaZero.exe --gpu-coupling-control --profile .\qa-reference-only
python .\scripts\analyze-coupling-qa.py --input '<katalog surowych serii Q>'
```

Dwa ostatnie tryby zachowują dawną regułę i mogą zwrócić błąd. Standardowe `--gpu-coupling-check` korzysta z Q. Nie zmieniono shaderów, równań, iteracji ani ustawień sterownika. Nie dodano synchronizacji do normalnej gry.

## Wyniki i wykrywanie rzeczywistej usterki

Wykonano 12 osobnych natywnych procesów. Każdy sprawdził 90 checkpointów w czterech rolach, łącznie 4320 porównań. Poniżej największe wartości skumulowanych defektów w tych 12 próbach. Wszystkie wyniki, granice per krok i sumy zostały niezależnie uzgodnione przez skrypt analizy CSV.

| Rola | Suma średnich błędów pozycji | Norma sumy przesunięć centroidu | Norma sumy przesunięć postaci | Wynik |
| --- | ---: | ---: | ---: | --- |
| Powtórka referencji | 5.0279073e-06 | 2.7455559e-07 | 0 | 12/12 PASS |
| Local | 5.2799044e-06 | 4.1459127e-07 | 0 | 12/12 PASS |
| Combined | 0.0014212096 | 6.5710596e-05 | 0.0056987236 | 12/12 PASS |
| Sprzężenie wyłączone celowo | 0.15438462 | 0.044319944 | 0.45879929 | 12/12 odrzucone, oczekiwane |
| Budżet | 0,05 | 0,02 | 0,15 | każdy krok i suma |

Kontrola usterki używa istniejącego wyłączenia sprzężenia w prawdziwych shaderach GPU, nie zmiany zapisanych liczb. Została odrzucona w każdej próbie. Pozostają też wcześniejsze testy kolejki: pusta partia, ogony grup, pełne dwa kanały 65536, hierarchia 64/128/256, podparcie, replay i reset. Wzmocniono niezależny test: znana suma reakcji musi nie tylko trafić do `last_sum`, lecz także faktycznie przesunąć postać w wolnej przestrzeni z błędem <0,00001 względem sumy double na CPU. [Dane każdej powtórki](gpu-i04-5q/paired-repeat/), [podsumowanie i maksima](gpu-i04-5q/coupling-qa-analysis.json), [implementacja replay](../src/gpu/coupling_replay.cpp).

**Pełny CTest: 38/38 PASS, kod 0, 302,20 s w protokole Q.** Przeszły również misje, warianty transportu i C/D, diagnostyka E0, sąsiedztwo, fizyka, UI, CPU i GL 3.3. Historyczne 37/38 E0 pozostaje bez zmian w jego raporcie. [Log](gpu-i04-5q/ctest.log), [JUnit](gpu-i04-5q/ctest.xml), [podsumowanie](gpu-i04-5q/ctest-summary.json), [kolekcja świeżych plików](gpu-i04-5q/qa/collection-provenance.json).

## Kontrola zwykłej gry i paczki

Porównano niezmienione E0 i Q w dotychczasowym `--gpu-stabilization-profile`: po trzy osobne procesy, naprzemienny porządek tworzący ABBA. **6/6 kod 0, 108 wierszy analizy, zero ostrzeżeń.** GTX 1650, NVIDIA 610.47, GL 4.3, Release MSVC /W4 /WX i IPO. Elapsed, liczniki ON, HUD auto/2, 120 ticków rozgrzewki i 240 pomiarowych na świeżą scenę; full/direct, opcje C i diagnostyka kosztów OFF.

Mediana trzech median, ms/tick; p95 jest medianą trzech percentyli, nie percentylem połączonych danych.

| Build | GPU misja | Misja p95 | GPU 16k | GPU 32k | CPU submit misji |
| --- | ---: | ---: | ---: | ---: | ---: |
| E0 | 2,94032 | 3,34029 | 1,79405 | 3,19363 | 0,00780 |
| Q | 2,84374 | 3,40765 | 1,79165 | 3,19651 | 0,00800 |

Q nie jest optymalizacją solvera. Nie wykonano testu istotności i nie przypisujemy różnic czasu zmianie metody QA; misja ma nieco niższą medianę, ale wyższe p95. Shadery i pozostałe assety są identyczne z E0. Cel GPU 1,50 ms nadal otwarty. [Pełne zakresy](gpu-i04-5q/performance-controls.json), [procesy](gpu-i04-5q/disabled-control/runs.csv), [build](gpu-i04-5q/build-config.json).

Własne kontrole paczki: `--gpu-coupling-check` i `--gpu-profile-check` kod 0; wymuszone `--opengl33 --gpu-coupling-check` oczekiwane 77. Widoczne okno: 900 klatek, 1798 ticków, wynik 3, brak utraty czasu; fokus 870/870, minimalizacja 0. Mediana pełnej klatki po 30 klatkach rozgrzewki 16,66850 ms, p95 16,66910 ms, p99 16,69290 ms, maksimum 47,14130 ms, wliczając przechwytywanie obrazu. To jedna próba, nie sześć wymaganych do odbioru całego I04.5.

KHR_debug id131186 według fazy odbioru: runtime 0, validation 1; logger zgubił 0 komunikatów. Asynchroniczny callback nie dowodzi przyczyny historycznych spowolnień ani stałej rezydencji VRAM. [Kontrole paczki](gpu-i04-5q/package-checks.json), [okno i debug](gpu-i04-5q/package-window-summary.json).

EXE SHA256 `50dd13f0f4999f1aca209c1fc4783483aba0454a31995dc36578949335c7fdb4`. EXE i assety odpowiadają finalnemu buildowi/źródłom; ZIP ma własne sumy SHA256 i kontrolę CRC. To paczka diagnostyczna, bez nowego tagu wydania. Zweryfikowano niezmienność 3717 plików historycznych raportów/danych i sumy paczek A/B/C/D/E0. [Stan źródeł i hashy](gpu-i04-5q/source-state.json).

## Decyzja

Q przywraca użyteczny odbiór sprzężenia w jawnie zmienionym protokole. Długie trajektorie nadal mogą różnić się liczbowo; nie zamknięto wszystkich przyczyn tej zmienności ani dawnego problemu warunków GPU. Następny etap to punktowy eksperyment PBF G, z Q wykonywanym w każdej konfiguracji. Lokalne Dmax pozostaje odroczone, a I05 czeka na ocenę całego I04.5. Wyniki i deklaracje dostarczonego review nadal są materiałem do weryfikacji, nie automatycznym odbiorem.
