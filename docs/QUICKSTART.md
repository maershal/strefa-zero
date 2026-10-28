# Strefa Zero 0.7 — iteracja I04

Uruchom `StrefaZero.exe`. Zachowaj `SDL3.dll`, `assets/` oraz `licenses/` w tym samym folderze.

## Misja GPU

Wybierz **Misja GPU — rozpocznij grę**. Bezpośredni start: `StrefaZero.exe --mission`.

Zniszcz trzy czerwone tarcze i dotrzyj do zielonego wyjścia po prawej. Pomarańczowe przegrody można przestrzelić lub wysadzić. Nad zbiornikiem przeleć plecakiem, otwórz zaporę, a następnie zniszcz dno silosu piasku. Szara konstrukcja pozostaje stała. Postać stoi na usypach, rozpycha ziarna, unosi się w wodzie i opada w lżejszym oleju. Jest niewrażliwa na obrażenia.

A/D lub strzałki — ruch; Spacja — skok; PPM — plecak; LPM — karabin; R — przeładuj; G — granat; F5 — reset; Esc — menu. Kieruj celownik nad planszę. Utrata fokusu zatrzymuje symulację.

Wymagane OpenGL 4.3. Na starszym sterowniku wybierz poligon CPU. Misja ma 3096 cząstek początkowych, a gruz powstaje z niszczonych przegród. Pełna pula 32768 cząstek jest limitem pojemności, nie gwarancją utrzymania 120 kroków/s na każdym GPU. Pomiary i ograniczenia: `REPORT.md`.

## Nowe laboratorium GPU

W menu wybierz **03 Laboratorium GPU — ciecze i ziarna**. Domyślnie uruchamia się klepsydra. Na liście scen są też „Olej pod wodą” i „Kropla oleju”. Bezpośredni start: `StrefaZero.exe --gpu-laboratory`.

Piasek, popiół i metalowe drobiny poruszają się z ciągłymi pozycjami, zderzają i tworzą usypy. LPM dodaje wybrany materiał, PPM usuwa, kółko zmienia pędzel. Spacja pauzuje, F wykonuje pojedynczy krok. Możesz rysować ściany, wybrać scenę lub zacząć od pustej planszy. Woda i olej korzystają z PBF; wybierz zbiornik, przerwaną zaporę, naczynia połączone lub mieszankę z piaskiem. Limit: 32768 cząstek łącznie. Panel można przewijać.

GPU wykonuje fizykę i rysowanie; krok czasu wynosi 1/120 s z podkrokami kontaktów. Panel pokazuje czas GPU i ewentualny pominięty czas. Wymagane OpenGL 4.3. Przy starszym sterowniku nadal działają tryby CPU na OpenGL 3.3; można wymusić je parametrem `--opengl33`.

**Woda i olej działają już na GPU. Ogień, gazy, temperatura i reakcje pozostają w Laboratorium materiałów na CPU.** Misja GPU używa nowego modelu; poligon CPU pozostaje osobnym trybem. Opis i wyniki: REPORT.md w paczce Windows.

## Poligon

A/D — ruch; Spacja — skok; PPM — plecak; LPM — karabin; R — przeładowanie; G — granat; F5 — reset; Esc — menu. Gracz pozostaje niewrażliwy, ale reaguje na podmuchy. Zniszcz trzy tarcze. Ogień i termit ogrzewają materiały; metal może się stopić i ponownie skrzepnąć. Pociski śledzą całą drogę, a osłony ograniczają podmuchy.

## Laboratorium

Wybór scen i map pól znajduje się po prawej. Najedź kursorem na świat, aby obejrzeć temperaturę, ciśnienia, prędkości, masę, paliwo i tlen. Panel można przewijać. LPM rysuje, PPM usuwa, kółko zmienia pędzel, Spacja zatrzymuje czas, F wykonuje pojedynczy krok.

Rozwiń „Koszt obliczeń”, aby sprawdzić czasy cieczy, ciśnienia, gazów i ciepła. Wersja 0.4.2 korzysta domyślnie z maksymalnie dwóch wątków fizyki, zoptymalizowanego PCG oraz poprawionego transportu cieczy. Okno nadal działa niezależnie od symulacji. Ciężka scena może pracować poniżej 60 kroków/s; audyt CPU znajduje się w docs/PERFORMANCE_AUDIT.md paczki źródłowej.

Aby porównać cztery wątki, uruchom `StrefaZero.exe --laboratory --physics-threads 4`. Parametr działa także z `--arena`. Wybór 1–4 nie zmienia szybkości czasu gry ani współczynników materiałów; zmienia podział pracy CPU.

19 materiałów i gumka: kamień, piasek, woda, olej, drewno, ogień, dym, para, kwas, lawa, gaz, węgiel, proch, termit, nitrogliceryna, metal, lód, stopiony metal, popiół. Temperaturę obliczamy w K, pokazujemy w °C; ciśnienie i masa mają jednostki modelu gry.

„Nagrywaj co 3 kroki” zapisuje klatki BMP i CSV w katalogu profilu, pod `recordings/`. Po zatrzymaniu otwórz `index.html` z tego folderu. Nagranie odtwarza się z szybkością 20 FPS. Dla długich sesji zaplanuj wolne miejsce: około 3,7 MB na sekundę nagrania.

## Sceny i benchmark bez okna

```powershell
./StrefaPhysics.exe --scene all --ticks 180 --output recordings --record
./StrefaPhysics.exe --scene arena --ticks 600 --output benchmark
```

Otwórz `recordings/index.html`. `summary.csv` zawiera percentyle czasu fizyki, bilanse i zbieżność solvera. Brak zbieżności PCG jest raportowany również w laboratorium. Laboratorium oblicza fizykę osobno od obsługi okna. Panel pokazuje rzeczywiste kroki/s; ciężkie sceny mogą działać wolniej niż 60 kroków fizyki/s, przy płynnie działających kontrolkach.

Windows x64, OpenGL 3.3. C++23 / SDL 3.4.18 / Dear ImGui 1.93.0. Licencje bibliotek i czcionek: `licenses/`. Kod i dokumentacja modeli są w `StrefaZero-source-I04.zip`. To prototyp fizyki i poligonu; nie zawiera botów ani multiplayera.

