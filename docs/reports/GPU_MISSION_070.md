# Strefa Zero 0.7 — misja GPU

Jedna ręcznie przygotowana misja 320 × 192: trzy tarcze, zielona strefa wyjścia, zbiornik wody z olejem i zniszczalną zaporą, silos piasku ze zniszczalnym dnem. Postać, broń, granaty, destrukcja i materiały korzystają z tego samego stanu na GPU. Poligon CPU i termiczne laboratorium CPU pozostają odrębnymi trybami.

## Sterowanie i zakres

Menu „Misja GPU” albo `StrefaZero.exe --mission`. A/D lub strzałki: ruch; Spacja: skok; PPM: plecak; LPM: strzał; R: przeładowanie; G: granat; F5: od początku; Esc: menu. Celownik musi znajdować się nad planszą. Postać jest niewrażliwa. Pomarańczowe przegrody można niszczyć; szara konstrukcja jest stała. Po trafieniu wszystkich tarcz dotrzyj do zielonego wyjścia.

Ten etap nie dodaje botów, multiplayera, generatora map, ragdolli ani termiki na GPU. Wybuch jest osłoniętym impulsem do gry; nie jest symulacją fali ciśnienia ani gazów.

## Stan i przepływ danych

- OpenGL 4.3, osiem wiązań SSBO na przebieg compute. Wspólne deklaracje `std430` i statyczne kontrole rozmiarów po stronie C++.
- Cząstki zachowują stabilne identyfikatory. Kompaktowana lista aktywnych ID i argumenty indirect dispatch/draw powstają na GPU po edycji. Przebiegi fizyki i rysowania nie przechodzą przez nieaktywne miejsca.
- Po zmianie przynależności synchronizowane są obie kopie ping-pong. Bez tego nieaktywna cząstka mogłaby powrócić przy kolejnym pełnym skanowaniu. Bariery SSBO, komend pośrednich i transferów buforów odpowiadają rodzajowi następnego dostępu.
- CPU wysyła sterowanie i ewentualne zdarzenia wybuchu. Stan postaci, pociski, trafienia, amunicja, wynik i warunek zakończenia są autorytatywne na GPU. Renderer czyta ten sam stan i istniejące atlasy części żołnierza oraz broni.
- Do HUD wraca tylko małe podsumowanie przez staging buffer i fence odpytywany bez oczekiwania. Generacja świata i numer kroku odrzucają nieaktualne podsumowania po resecie. Odczyt pełnego świata jest dostępny wyłącznie dla walidacji i eksportu.

## Model postaci i materiałów

Zewnętrzny krok GPU to 1/120 s, z dwoma podkrokami 1/240 s. Pozostają wcześniejsze iteracje kontaktów i PBF. Wejście skoku, granatu i przeładowania jest buforowane do jednego kroku, niezależnie od FPS. Misja wykonuje najwyżej cztery kroki na klatkę i zachowuje do 24 zaległych kroków (0,2 s). Krótkie opóźnienie okna jest nadrabiane w kolejnych klatkach; czas ponad ten limit jest nadal jawnie raportowany. Tryby referencyjne zachowują poprzednie zasady zegara.

Postać jest pionową kapsułą 8 × 18, o polu `80 + 16π`. Jej masa wynika z tego pola i względnej gęstości 0,9 gęstości wody; olej ma gęstość 0,8 wody. Kapsuła nie obraca się. Silnik ruchu i plecak są jawnymi siłami sterowania, a ograniczenia prędkości są parametrami rozgrywki.

Kontakty z ziarnami rozdzielają poprawki pozycji według odwrotności mas. Reakcje na postać zbiera stała redukcja GPU, bez atomowych dodawań float. Tarcie materiałów pozostaje częścią istniejącego modelu. Ruch względem terenu jest dzielony na odcinki najwyżej 0,35 komórki; kolizje kapsuły sprawdzają rzeczywiste komórki ścian.

W PBF próbki wnętrza kapsuły uzupełniają gęstość przy granicy. Gradient względem przesunięcia całej kapsuły trafia do mianownika ograniczenia z jej odwrotnością masy; reakcja ma przeciwny kierunek do poprawki płynu. To daje wyporność bez osobnego arbitralnego przyspieszenia „w górę”. Kontakt geometryczny dodatkowo ogranicza przenikanie. Lepkościowy opór przy kapsule wymienia pęd z cieczą i zależy od `1-exp(-k·dt)`.

Model nawiązuje do [Unified Particle Physics for Real-Time Applications, sekcja 7.1](https://mmacklin.com/uppfrta_preprint.pdf). Pozostaje przybliżeniem dla gry: masa kapsuły jest efektywna, granice są próbkowane, liczba iteracji jest skończona, a postać ma aktywny napęd. Nie deklarujemy zachowania energii przez sterownik postaci ani równoważności z solverem MAC na CPU.

## Broń i destrukcja

Karabin: 30 nabojów, odstęp 0,1 s, przeładowanie 1,5 s, prędkość pocisku 600 komórek/s. Granaty: początkowo trzy, zapalnik 2 s. Strzał powstaje z aktualnej pozycji lufy. Trafienie kruchej przegrody uszkadza otoczenie 3 × 3 komórki, aby zachowany gruz nie zatykał wyłącznie jednopikselowego otworu. Supercover DDA sprawdza odcinek od barku do lufy, drogę pocisku i promienie osłony wybuchu, włącznie z narożnikami cienkich ścian.

Wybuch nadaje ograniczony impuls i uszkadza widoczną powierzchnię kruchego terenu. Widoczność jest liczona przed konwersją komórek w gruz, więc pojedynczy wybuch nie usuwa kolejnych warstw osłony w zależności od kolejności wątków. Jednoczesne detonacje są wykonywane kolejno, bez nadpisywania zdarzeń. Zewnętrzne zdarzenia mają rosnący, niezerowy identyfikator w obrębie generacji świata; powtórzenie ID nie wykonuje wybuchu ponownie.

Każda krucha komórka staje się jednym ziarnem gruzu o jednostkowej masie modelu. Gdy pula 32768 cząstek jest pełna, zniszczona komórka nadal zajmuje teren i pozostaje w liczniku oczekującego gruzu. Znika dopiero po atomowej rezerwacji wolnego miejsca. Nie ma cichego kasowania nadmiaru materiału. Nie jest to model mechaniki pękania ani automatycznego zawalania całych budynków.

## Walidacja i pomiary

`--mission-test --profile <folder>` sprawdza aktywne ID, kasowanie/odtwarzanie materiałów, teren, piasek, wodę i olej, strzały, granaty, szczelność ścian, sufit, osłonę wybuchu, wyczerpanie puli, reset oraz pełną misję przy 30/60/144 FPS. `--mission-smoke-test --profile <folder>` sprawdza okno i rendering. Pełny zestaw obejmuje też wcześniejsze regresje klepsydry, cieczy oraz CPU/GL 3.3.

Czasy GPU pochodzą z nieblokujących zapytań `GL_TIME_ELAPSED`. Fizyka i renderowanie są mierzone oddzielnie. Czas CPU oznacza wysyłanie pracy fizyki i obsługę gotowych odczytów, wraz z ewentualnym oczekiwaniem sterownika przy pełnej kolejce GPU; nie jest sumowany z czasem GPU. Walidacja może synchronizować urządzenie i eksportować BMP poza mierzonym krokiem. Desktop i zegary GPU nie są laboratoryjnie kontrolowane. Dla 16k/32k raportujemy pomiary, bez gwarancji 60 FPS.

### Wyniki wydania

Przeszło **16/16 testów**, a przeniesiona paczka dodatkowo przeszła misję, test okna oraz OpenGL 3.3. Automatyczna trasa kończy misję przy 30/60/144 FPS; w ostatnim przebiegu (144 FPS) wypuściła 728 cząstek wody i 464 ziaren piasku.

| Scena | Cząstki | Mediana GPU [ms] | p95 | p99 | max |
|---|---:|---:|---:|---:|---:|
| painted | 6064 | 1.14266 | 1.86368 | 2.28202 | 2.59686 |
| loaded | 6064 | 1.14234 | 1.85139 | 2.4439 | 43.8091 |
| mission | 3096 | 3.73203 | 5.11046 | 5.66518 | 6.56563 |
| stress_16k | 16000 | 4.01555 | 5.47389 | 6.01402 | 6.20282 |
| stress_32k | 32768 | 8.06912 | 9.96966 | 10.4311 | 10.5143 |

Pełny przebieg okna misji: mediana 16.6685 ms, p95 16.7335 ms, p99 17.235 ms, maksimum 20.8874 ms. Pominięty czas: 0 s. Maksimum obejmuje sporadyczne zastoje; nie deklarujemy braku pojedynczych dłuższych klatek. Misja ma 3096 początkowych cząstek. Pomiary CPU i renderowania są osobnymi wierszami CSV.

Pierwsze próby wykazały, że natychmiastowe odrzucanie zaległych kroków po krótkim zastoju gubiło czas. Misja zachowuje teraz do 24 zaległych kroków, nadrabiając najwyżej cztery na klatkę; osobny test sprawdza dokładne księgowanie przeciążenia. Eksport obrazu wykonywany jest po ostatnim mierzonym kroku.

Dane: [testy](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/legacy/TEST_RESULTS.txt), [QA](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/legacy/QA.json), [misja](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu070/build/gpu-mission-tests.txt), [benchmark](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu070/build/gpu-mission-benchmark.csv), [okno](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu070/build/mission-ui.txt), [przebieg 60 FPS](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu070/build/route-60.csv), [podgląd](https://github.com/maershal/strefa-zero-measurements/blob/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu070/build/mission-window.bmp).
