# Strefa Zero 0.4.2 — fizyka do gry

Uruchom `StrefaZero.exe`. Zachowaj `SDL3.dll`, `assets/` oraz `licenses/` w tym samym folderze. Wybierz Poligon lub Laboratorium materiałów.

## Poligon

A/D — ruch; Spacja — skok; PPM — plecak; LPM — karabin; R — przeładowanie; G — granat; F5 — reset; Esc — menu. Gracz pozostaje niewrażliwy, ale reaguje na podmuchy. Zniszcz trzy tarcze. Ogień i termit ogrzewają materiały; metal może się stopić i ponownie skrzepnąć. Pociski śledzą całą drogę, a osłony ograniczają podmuchy.

## Laboratorium

Wybór scen i map pól znajduje się po prawej. Najedź kursorem na świat, aby obejrzeć temperaturę, ciśnienia, prędkości, masę, paliwo i tlen. Panel można przewijać. LPM rysuje, PPM usuwa, kółko zmienia pędzel, Spacja zatrzymuje czas, F wykonuje pojedynczy krok.

Rozwiń „Koszt obliczeń”, aby sprawdzić czasy cieczy, ciśnienia, gazów i ciepła. Wersja 0.4.2 korzysta domyślnie z maksymalnie dwóch wątków fizyki, zoptymalizowanego PCG oraz poprawionego transportu cieczy. Okno nadal działa niezależnie od symulacji. Ciężka scena może pracować poniżej 60 kroków/s; pełny audyt i pomiary znajdują się w REPORT.md paczki Windows.

Aby porównać cztery wątki, uruchom `StrefaZero.exe --laboratory --physics-threads 4`. Parametr działa także z `--arena`. Wybór 1–4 nie zmienia szybkości czasu gry ani współczynników materiałów; zmienia podział pracy CPU.

19 materiałów i gumka: kamień, piasek, woda, olej, drewno, ogień, dym, para, kwas, lawa, gaz, węgiel, proch, termit, nitrogliceryna, metal, lód, stopiony metal, popiół. Temperaturę obliczamy w K, pokazujemy w °C; ciśnienie i masa mają jednostki modelu gry.

„Nagrywaj co 3 kroki” zapisuje klatki BMP i CSV w katalogu profilu, pod `recordings/`. Po zatrzymaniu otwórz `index.html` z tego folderu. Nagranie odtwarza się z szybkością 20 FPS. Dla długich sesji zaplanuj wolne miejsce: około 3,7 MB na sekundę nagrania.

## Sceny i benchmark bez okna

```powershell
./StrefaPhysics.exe --scene all --ticks 180 --output recordings --record
./StrefaPhysics.exe --scene arena --ticks 600 --output benchmark
```

Otwórz `recordings/index.html`. `summary.csv` zawiera percentyle czasu fizyki, bilanse i zbieżność solvera. Brak zbieżności PCG jest raportowany również w laboratorium. Laboratorium oblicza fizykę osobno od obsługi okna. Panel pokazuje rzeczywiste kroki/s; ciężkie sceny mogą działać wolniej niż 60 kroków fizyki/s, przy płynnie działających kontrolkach.

Windows x64, OpenGL 3.3. C++23 / SDL 3.4.18 / Dear ImGui 1.93.0. Licencje bibliotek i czcionek: `licenses/`. Kod i dokumentacja modeli są w `StrefaZero-source-v0.4.1.zip`. To prototyp fizyki i poligonu; nie zawiera botów ani multiplayera.

