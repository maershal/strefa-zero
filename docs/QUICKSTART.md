# Strefa Zero 0.7 — iteracja I11

Uruchom `StrefaZero.exe`. Zachowaj `SDL3.dll`, `assets/` oraz `licenses/` w tym samym folderze.

`StrefaZero.exe --help` wypisuje opcje gry. Pokrętła eksperymentów (`--gpu-*`, `--physics-threads`, `--shader-cache`) oraz walidacje przyjmuje `StrefaQA.exe`, budowany ze źródeł obok gry (`./scripts/build.ps1`) i nie dołączany do paczki gracza. Większość tych ustawień jest też dostępna w Laboratorium GPU i ustawieniach gry.

Menu: 01 Misja GPU, 02 Laboratorium GPU, 03 Poligon CPU, 04 Laboratorium CPU, 05 Podgląd areny, 06 Ustawienia, 07 O projekcie, 08 Zakończ.

## Misja GPU

Wybierz **01 Misja GPU**. Bezpośredni start: `StrefaZero.exe --mission`.

Zniszcz trzy czerwone tarcze i dotrzyj do zielonego wyjścia po prawej. Pomarańczowe przegrody można przestrzelić lub wysadzić. Nad zbiornikiem przeleć plecakiem, otwórz zaporę, a następnie zniszcz dno silosu piasku. Szara konstrukcja pozostaje stała. Postać stoi na usypach, rozpycha ziarna, unosi się w wodzie i opada w lżejszym oleju. Jest niewrażliwa na obrażenia.

A/D lub strzałki — ruch; Spacja — skok; PPM — plecak; LPM — karabin; R — przeładuj; G — granat; F5 — reset; Esc — menu. Celowanie oraz plecak działają również z kursorem poza planszą w aktywnym oknie; kontrolki interfejsu zachowują przechwycenie wejścia. Utrata fokusu zatrzymuje symulację.

Wymagane OpenGL 4.3. Na starszym sterowniku wybierz Poligon CPU. Misja ma 3096 cząstek początkowych, a gruz powstaje z niszczonych przegród. Pełna pula 32768 cząstek jest limitem pojemności, nie gwarancją utrzymania 120 kroków/s na każdym GPU. Pomiary i ograniczenia: `REPORT.md`.

## Laboratorium GPU

W menu wybierz **02 Laboratorium GPU**. Domyślnie uruchamia się klepsydra. Na liście scen są też „Olej pod wodą” i „Kropla oleju”. Bezpośredni start: `StrefaZero.exe --gpu-laboratory`.

Piasek, popiół i metalowe drobiny poruszają się z ciągłymi pozycjami, zderzają i tworzą usypy. LPM dodaje wybrany materiał, PPM usuwa, kółko zmienia pędzel. Spacja pauzuje, F wykonuje pojedynczy krok. Możesz rysować ściany, wybrać scenę lub zacząć od pustej planszy. Woda i olej korzystają z PBF; wybierz zbiornik, przerwaną zaporę, naczynia połączone lub mieszankę z piaskiem. Limit: 32768 cząstek łącznie. Panel można przewijać.

GPU wykonuje fizykę i rysowanie; krok czasu wynosi 1/120 s z podkrokami kontaktów. Panel pokazuje czas GPU i ewentualny pominięty czas. Wymagane OpenGL 4.3. Przy starszym sterowniku nadal działają tryby CPU na OpenGL 3.3; można wymusić je parametrem `--opengl33`.

**Woda i olej działają na GPU. Ogień, gazy, temperatura i reakcje mają referencyjną wersję w Laboratorium CPU, a od I10 także opcjonalną, eksperymentalną termikę GPU, domyślnie wyłączoną: włącz ją w Laboratorium GPU albo parametrem `--gpu-thermal on` programu StrefaQA (opis w sekcji o termice poniżej).** Misja GPU używa nowego modelu; Poligon CPU pozostaje osobnym trybem. Opis i wyniki: REPORT.md w paczce Windows.

## Poligon CPU

A/D — ruch; Spacja — skok; PPM — plecak; LPM — karabin; R — przeładowanie; G — granat; F5 — reset; Esc — menu. Gracz pozostaje niewrażliwy, ale reaguje na podmuchy. Zniszcz trzy tarcze. Ogień i termit ogrzewają materiały; metal może się stopić i ponownie skrzepnąć. Pociski śledzą całą drogę, a osłony ograniczają podmuchy.

## Laboratorium CPU

Wybór scen i map pól znajduje się po prawej. Najedź kursorem na świat, aby obejrzeć temperaturę, ciśnienia, prędkości, masę, paliwo i tlen. Panel można przewijać. LPM rysuje, PPM usuwa, kółko zmienia pędzel, Spacja zatrzymuje czas, F wykonuje pojedynczy krok.

Rozwiń „Diagnostyka”, aby sprawdzić zbieżność solvera ciśnienia (PCG), bilanse oraz czasy cieczy, ciśnienia, gazów i ciepła. Wersja 0.4.2 korzysta domyślnie z maksymalnie dwóch wątków fizyki, zoptymalizowanego PCG oraz poprawionego transportu cieczy. Okno nadal działa niezależnie od symulacji. Ciężka scena może pracować poniżej 60 kroków/s; audyt CPU znajduje się w docs/reports/PERFORMANCE_AUDIT.md paczki źródłowej.

Aby porównać cztery wątki, uruchom `StrefaQA.exe --laboratory --physics-threads 4`. Parametr działa także z `--arena`. Wybór 1–4 nie zmienia szybkości czasu gry ani współczynników materiałów; zmienia podział pracy CPU.

19 materiałów i gumka: kamień, piasek, woda, olej, drewno, ogień, dym, para, kwas, lawa, gaz, węgiel, proch, termit, nitrogliceryna, metal, lód, stopiony metal, popiół. Temperaturę obliczamy w K, pokazujemy w °C; ciśnienie i masa mają jednostki modelu gry.

„Nagrywaj co 3 kroki” zapisuje klatki BMP i CSV w katalogu profilu, pod `recordings/`. Po zatrzymaniu otwórz `index.html` z tego folderu. Nagranie odtwarza się z szybkością 20 FPS. Dla długich sesji zaplanuj wolne miejsce: około 3,7 MB na sekundę nagrania.

## Sceny i benchmark bez okna

```powershell
./StrefaPhysics.exe --scene all --ticks 180 --output recordings --record
./StrefaPhysics.exe --scene arena --ticks 600 --output benchmark
```

Otwórz `recordings/index.html`. `summary.csv` zawiera percentyle czasu fizyki, bilanse i zbieżność solvera. Brak zbieżności PCG jest raportowany również w laboratorium. Laboratorium oblicza fizykę osobno od obsługi okna. Panel pokazuje rzeczywiste kroki/s; ciężkie sceny mogą działać wolniej niż 60 kroków fizyki/s, przy płynnie działających kontrolkach.

Windows x64, OpenGL 3.3. C++23 / SDL 3.4.18 / Dear ImGui 1.93.0. Licencje bibliotek i czcionek: `licenses/`. Kod i dokumentacja modeli są w `StrefaZero-source-I07.zip`. To prototyp fizyki i poligonu; nie zawiera botów ani multiplayera.


## Wspólna scena gry i narzędzi (I05)

„Misja GPU” oraz „Laboratorium GPU” otwierają ten sam świat. Na pierwszym wejściu wybierana jest misja lub klepsydra; późniejszy powrót z menu zachowuje stan. W misji użyj przycisku „Laboratorium GPU”, a w laboratorium „Widok gry (Misja GPU)”. Pauza jest wspólna. Reset, wybór innej sceny i rozpoczęcie nowej misji są jawnymi działaniami.

W Laboratorium GPU można malować również na misji, zatrzymać ją i wykonać jeden krok. Opcjonalna termika i gazy działają w tym samym świecie GPU. Poligon CPU jest osobnym narzędziem referencyjnym, tworzonym dopiero po otwarciu.

„Nowe nagranie (reset sceny)” rozpoczyna zapis poleceń. „Zapisz nagranie + obraz” eksportuje `session.txt`, `metrics.csv` i `preview.bmp` do `recordings/gpu-…/` w profilu. „Odtwórz ostatnie nagranie” wykonuje zapis wsadowo i pozostawia scenę zatrzymaną. Limit to 65536 poleceń.

Opcjonalny cache shaderów: `StrefaQA.exe --mission --shader-cache on`. Domyślnie wyłączony; uszkodzone/niezgodne wpisy są odrzucane, po czym programy kompilują się ze źródeł.

## Sterowanie i reguły I06

Amunicja, granaty, przeładowanie i wynik mają wspólny stan dla gry i narzędzi. Krótkie naciśnięcia zachowują się także między krokami symulacji. Skok ma tolerancję około 83 ms po utracie podparcia i bufor 100 ms przed lądowaniem; pauza lub utrata fokusu kasują oczekujący skok. Granaty mają wspólny odstęp 250 ms.

Zniszczona tarcza pozostaje wrakiem, który zatrzymuje pociski. Zachowano efektywne tempo broni poprzedniej wersji: 13 ticków karabinu oraz 181 ticków przeładowania. Bieżące wyniki i ograniczenia wydajności opisuje raport I06. Diagnostyka kontraktu: `StrefaQA.exe --gameplay-check --profile wyniki-i06`.

## I07 — opcjonalna fizyka cieczy

Domyślny profil `legacy` zachowuje ustawienia I06. Eksperymentalny profil `balanced` dodaje ważone granice, napięcie 2D, adhezję i ograniczoną wirowość: `StrefaQA.exe --mission --gpu-fluid-profile balanced`. Profil jest dostępny tylko w StrefaQA (eksperymentalny, poza budżetem). Nagrania v2 zawierają konfigurację fizyki; starsze nagrania v1 wymagają `legacy`.

Walidacja: `StrefaQA.exe --gpu-fluid-check --gpu-fluid-profile balanced --profile wyniki-i07`. Wyniki, koszt i ograniczenia: [I07_FLUID_QUALITY.md](reports/I07_FLUID_QUALITY.md).

## Obraz wody I08

W ustawieniach („Obraz wody”) lub w Laboratorium GPU wybierz jakość obrazu: Podstawowy, Standardowy lub Wysoki. Zmiana nie resetuje sceny i jest zapisywana w `settings.ini`. Przy uruchamianiu StrefaQA można użyć `--gpu-render-quality basic|standard|high`. Podstawowy pozostaje domyślny na podstawie pomiarów GTX 1650. Profile fizyki `legacy`/`balanced` są osobnym ustawieniem. [Raport I08](reports/I08_SCENE_RENDERER.md).

## Efekty I09

Domyślne VFX low mają pulę 16 384. Wybór efektów w ustawieniach, Laboratorium GPU, misji i Poligonie CPU działa bez resetowania fizyki i jest zapisywany w `settings.ini`. Z wiersza poleceń: `StrefaQA.exe --mission --gpu-vfx-quality low`, `high` lub `off`. Jakość wody i fizyki wybiera się osobno. Poligon CPU używa tej samej puli na GL 4.3, a na GL 3.3 zachowuje uproszczone efekty. Szczegóły odbioru: [I09_GPU_VFX.md](reports/I09_GPU_VFX.md).

Odbiór wydajności I09 pozostaje otwarty: końcowy najgorszy segment low dodał 0,678 ms przy limicie 0,25 ms. Szczegóły i warunki pomiarów są w raporcie I09.

## Termika I10

Uruchom `StrefaQA.exe --gpu-thermal-scene fire` (albo `oil`, `quench`, `phases`). W Laboratorium GPU można włączyć „Termika i gazy (eksperymentalne)”, wybrać scenę i nacisnąć „Wczytaj scenę termiki”; „Reset sceny” przywraca wczytaną scenę termiki. Włączenie dotyczy tylko bieżącej sesji. Dostępne są grzanie, chłodzenie, dodawanie drewna/lodu/lawy oraz gazów. Widoki pokazują temperaturę, ciśnienie projekcji, prędkość, tlen i paliwo; inspektor podaje tick ostatniej gotowej próbki. Pauza i pojedynczy krok obejmują też termikę.

W misji użyj `StrefaQA.exe --mission --gpu-thermal on`. Domyślnie jest `off`, ponieważ budżet wydajności nie został spełniony. CPU i fallback GL 3.3 zachowują dotychczasową termikę referencyjną. Zakres materiałów, testy i wyniki: [I10_GPU_THERMAL.md](reports/I10_GPU_THERMAL.md).

## Oświetlenie I11

`StrefaQA.exe --mission --gpu-lighting-quality low` włącza światło, HDR, subtelny bloom i styl piasku. `high` dodaje miękkie cienie; `off` zachowuje dotychczasowy obraz. W Laboratorium GPU wybierz „ŚWIATŁO (eksperymentalne)” (albo „Oświetlenie” w ustawieniach); włączenie zapisuje się na stałe dopiero po „Zapisz na stałe”, inaczej działa tylko w tej sesji; bloom, winieta, wstrząs i aberracja mają osobne suwaki. Wstrząs i aberracja są początkowo wyłączone. Wilgoć piasku jest wizualna, utrzymuje się po wyjściu z wody i wysycha w tickach.

I11 pozostaje domyślnie wyłączone; odbiór wydajności: otwarty. Termika I10 jest oddzielną opcją. [Raport I11](reports/I11_LIGHTING.md) opisuje testy, pomiary i ograniczenia.
