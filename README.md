# Strefa Zero 0.4.2

## Repozytorium

Prywatne repozytorium: https://github.com/maershal/strefa-zero. Do pobrania i wysyłania zmian wymagany jest dostęp do repozytorium na GitHubie.

```powershell
git clone https://github.com/maershal/strefa-zero.git
cd strefa-zero
./scripts/build.ps1 -Configuration Release
```

Repozytorium zawiera źródła, zasoby, testy i dokumentację. Wyniki kompilacji powstają lokalnie w `build/` i są pomijane przez Git.

Prototyp offline w C++23, SDL 3.4.18, Dear ImGui 1.93.0 i OpenGL 3.3. Laboratorium i poligon używają wspólnego modelu materiałów na siatce 320 × 192, ze stałym krokiem czasu 1/60 s. Faktyczne tempo symulacji zależy od kosztu sceny.

## Wydajność i matematyka 0.4.2

Stała pula CPU, MIC(0), ograniczenie skanowania cieczy, pomijanie pustych kanałów i ponowne używanie buforów. Poprawione CFL, wymiana ciepła wielu sąsiadów, szczelne składowe ciśnienia i aktywacja gorącego paliwa. Testy porównują bitowo 1/2/4 wątki. Pomiary fizyki, źródła metod i ocena GPU: [PERFORMANCE_AUDIT.md](docs/PERFORMANCE_AUDIT.md).

Fizyka laboratorium pracuje w osobnym wątku, a okno dostaje niezmienne obrazy świata. Nie ma nadrabiania ośmiu kroków przed kolejną obsługą wejścia. Panel pokazuje faktyczne kroki fizyki/s osobno od FPS okna. Ciężka scena może spowolnić czas symulacji, ale nie zatrzymuje menu i kontrolek. Szczegóły i wyniki: [LAB_PERFORMANCE.md](docs/LAB_PERFORMANCE.md).

## Uruchomienie

Otwórz `StrefaZero.exe`. Zachowaj obok `SDL3.dll`, katalogi `assets/` i `licenses/`. Windows x64, sterownik OpenGL 3.3. Opcje startu: `--arena`, `--laboratory`, `--profile <katalog>`.

`StrefaZero.exe --laboratory --physics-threads 4` uruchamia laboratorium z czterema wątkami fizyki. Dostępne wartości 1–4; bez parametru aplikacja dobiera najwyżej dwa, pozostawiając zapas dla okna i innych programów. W panelu „Koszt obliczeń” widać faktyczną liczbę wątków. Skalowanie zależy od sceny — pomiary znajdują się w audycie.

Poligon: A/D lub strzałki — ruch, Spacja — skok, PPM — plecak, LPM — karabin, R — przeładuj, G — granat, F5 — reset, Esc — menu. Trzy tarcze, gracz niewrażliwy na obrażenia. Celownik musi znajdować się nad areną. Utrata fokusu zatrzymuje grę. Podmuch może przesunąć gracza; celowanie i ręce są niezależne od animacji nóg.

## Laboratorium

19 materiałów oraz gumka, w tym lód, stopiony metal i popiół. Panel po prawej można przewijać.

- Wybierz jedną z 14 scen regresyjnych albo demonstrację.
- LPM rysuje, PPM usuwa, kółko zmienia promień. Ogień ogrzewa istniejący materiał; narzędzia ogrzewania/chłodzenia dodają/odbierają po 100 jednostek energii wskazanej komórce przy aktualizacji interfejsu.
- Spacja — pauza; F — jeden krok. Temperatura, ciśnienie, prędkość, tlen i aktywność mają osobne mapy.
- Najedź kursorem na świat, aby odczytać temperaturę w °C, ciśnienia, prędkości, masę, udziały cieczy, paliwo i tlen.
- Górna granica może przepuszczać gazy albo być szczelna. Boki i dół pozostają zamknięte.
- „Nagrywaj co 3 kroki” zapisuje BMP i `steps.csv` w `profil/recordings/<identyfikator>`. Po zatrzymaniu powstaje `index.html` z podglądem 20 FPS. Czas fizyki nie obejmuje eksportu.

Świat zachowuje masę i entalpię z jawnymi źródłami, edycjami, promieniowaniem i ujściami. Para skrapla się od chłodzenia. Spalanie zużywa paliwo i tlen. Kwas pozostawia rozpuszczone produkty. Eksplozja emituje zdarzenie, ogrzewa i wprawia otoczenie w ruch; ściany osłabiają działanie.

## Budowanie i testy

Wymagane Visual Studio C++ Build Tools, Windows SDK, CMake >= 3.28, Ninja i PowerShell 7. Skrypt wykrywa istniejące narzędzia; nie instaluje ich. Pierwsza konfiguracja pobiera przypięte źródła bibliotek z weryfikacją SHA-256.

```powershell
./scripts/build.ps1 -Configuration Release
# Bez SDL, GPU ani okna (model, narzędzie i testy):
./scripts/build.ps1 -CoreOnly
```

MSVC używa `/std:c++23preview`, `/W4`, `/WX` i statycznego CRT. Release włącza IPO/LTO, jeśli obsługuje je kompilator. Opcjonalna próba refleksji C++26 jest odizolowana; gra jej nie wymaga.

Narzędzie `StrefaPhysics.exe` uruchamia sceny bez okna:

```powershell
./StrefaPhysics.exe --scene all --ticks 180 --output recordings --record
./StrefaPhysics.exe --scene connected-vessels --ticks 600 --output vessels --record
./StrefaPhysics.exe --scene arena --ticks 600 --output benchmark
```

`--threads 1|2|3|4` wybiera liczbę wątków fizyki (łącznie z wywołującym); domyślnie najwyżej 2. `--ic0` włącza poprzedni preconditioner, `--pressure-reference` niezależną implementację PCG na pełnej siatce. `--reference` włącza pełne skanowanie proszków. Wynik zawiera medianę, p95, p99, maksimum, błędy bilansów, prawdziwe residuum PCG i liczbę kroków bez zbieżności. Pliki `*-profile.csv` zawierają czasy systemów dla każdego kroku. Pomiar poligonu obejmuje też logikę gry. `all` obejmuje 14 scen i poligon; demonstrację uruchom przez `--scene demo`.

## Modele i ograniczenia

[Model fizyki](docs/PARTICLES.md), [architektura](docs/ARCHITECTURE.md), [animacja i gra](docs/GAMEPLAY.md), [źródła metod](docs/RESEARCH.md), [wyniki weryfikacji](docs/QA.json).

To przybliżenia do gry: transport udziałów cieczy jest dyfuzyjny, interfejsy mają grubość komórki, a niewidoczne udziały pozostają w bilansie. Ciśnienie i prędkość dźwięku mają skalę gry. Bilans energii obejmuje entalpię i źródła ciepła, a energia mechaniczna jest tłumiona — nie jest to pełna termodynamika przepływu ściśliwego. Luźne piksele metalu/drewna mają bezwładność; sztywne bryły, ragdolle, boty, multiplayer i zapis stanu świata pozostają poza tym wydaniem.

