# Strefa Zero 0.7.0

Offline'owy prototyp gry 2D w C++23 z symulacją materiałów: ciecze PBF i ziarna PBD na GPU
(OpenGL 4.3 compute), referencyjny model termochemiczny na CPU, misja, poligon i laboratoria.
SDL 3.4.18, Dear ImGui 1.93.0. Bez CUDA i Vulkana.

Aplikacja tworzy kontekst OpenGL 4.3 i wraca do 3.3, gdy sterownik go nie udostępnia
(tryby GPU są wtedy niedostępne; laboratorium CPU i poligon działają).

## Szybki start

**Linux / WSL2 (Ubuntu 24.04)** — zależności, WSLg i grafika: [docs/LINUX_WSL.md](docs/LINUX_WSL.md).

```bash
./scripts/build.sh
./scripts/run.sh --mission
```

**Windows** — Visual Studio C++ Build Tools, Windows SDK, CMake >= 3.28, Ninja, PowerShell 7.

```powershell
./scripts/build.ps1 -Configuration Release
./build/release/StrefaZero.exe --mission
```

Pierwsza konfiguracja pobiera przypięte źródła SDL i ImGui z weryfikacją SHA-256.
Wyniki kompilacji trafiają do `build/` (ignorowane przez Git).

## Tryby

| Tryb | Uruchomienie | Opis |
| --- | --- | --- |
| Misja GPU | `--mission` | Zniszcz trzy tarcze i dotrzyj do wyjścia; zapora i silos uwalniają wodę i piasek. [Opis](docs/reports/GPU_MISSION_070.md) |
| Laboratorium GPU | `--gpu-laboratory` | Woda i olej (PBF), piasek, popiół i drobiny metalu (PBD), limit 32 768 cząstek. |
| Laboratorium CPU | `--laboratory` | 19 materiałów, 14 scen regresyjnych, temperatura, ciśnienie, tlen, reakcje. |
| Poligon | `--arena` | Trening na fizyce CPU; trzy tarcze, gracz niewrażliwy na obrażenia. |

**Sterowanie (misja, poligon):** A/D lub strzałki — ruch, Spacja — skok, PPM — plecak,
LPM — karabin, R — przeładuj, G — granat, F5 — reset, Esc — menu.
**Laboratoria:** LPM rysuje, PPM usuwa, kółko zmienia pędzel, Spacja — pauza, F — jeden krok.

## Opcje uruchomienia

`StrefaZero` (gra) przyjmuje tylko opcje gracza; `StrefaZero --help` je wypisuje.

| Opcja | Znaczenie |
| --- | --- |
| `--mission`, `--gpu-laboratory`, `--laboratory`, `--arena` | Start bezpośrednio w trybie (zob. tabela wyżej). |
| `--profile <katalog>` | Katalog ustawień, nagrań i raportów (domyślnie profil użytkownika). |
| `--opengl33` | Wymusza kontekst 3.3 (kontrola zgodności; tryby GPU niedostępne). |
| `--gl-debug` | Zapisuje komunikaty sterownika OpenGL do `gl-debug.txt` w profilu. |
| `--smoke-test`, `--mission-smoke-test` | Test dymny menu / misji GPU tego pliku wykonywalnego (wymaga `--profile`). |
| `--help` | Lista opcji. |

Model cieczy, jakość obrazu, efekty, termikę GPU ([I07](docs/reports/I07_FLUID_QUALITY.md)–[I10](docs/reports/I10_GPU_THERMAL.md))
wybiera się w narzędziach świata GPU, a oświetlenie ([I11](docs/reports/I11_LIGHTING.md)) w ustawieniach.
Termika i oświetlenie są domyślnie wyłączone, bo nie spełniają jeszcze budżetów czasu klatki.

### StrefaQA

`StrefaQA` powstaje w tym samym katalogu kompilacji (nie trafia do paczki gracza). Przyjmuje opcje gracza
oraz wszystkie walidacje, benchmarki, profile i pokrętła eksperymentów, z tą samą semantyką co wcześniej
`StrefaZero`. Pełna lista z wartościami domyślnymi: [docs/maintenance/INVENTORY.md](docs/maintenance/INVENTORY.md)
i `src/app/launch_options.cpp`. Przykłady pokręteł startowych:

| Opcja | Domyślnie | Znaczenie |
| --- | --- | --- |
| `--physics-threads 1-4` | najwyżej 2 | Wątki fizyki CPU. |
| `--gpu-fluid-profile legacy\|balanced` | `legacy` | Model cieczy ([I07](docs/reports/I07_FLUID_QUALITY.md)). |
| `--gpu-render-quality basic\|standard\|high` | `basic` | Jakość obrazu wody ([I08](docs/reports/I08_SCENE_RENDERER.md)). |
| `--gpu-vfx-quality off\|low\|high` | `low` | Efekty GPU ([I09](docs/reports/I09_GPU_VFX.md)). |
| `--gpu-thermal on`, `--gpu-thermal-scene fire\|oil\|quench\|phases` | wyłączone | Termika, gazy i ogień GPU ([I10](docs/reports/I10_GPU_THERMAL.md)). |
| `--gpu-lighting-quality off\|low\|high` | z ustawień | Światło 2D, HDR i bloom ([I11](docs/reports/I11_LIGHTING.md)). |
| `--shader-cache on` | wyłączony | Cache skompilowanych shaderów. |

## Budowanie i testy

```bash
./scripts/build.sh --test-suite cpu     # testy CPU
./scripts/build.sh                      # + smoke testy okna i GPU (domyślnie)
./scripts/build.sh --test-suite all     # pełne QA, wymaga GPU z OpenGL 4.3
./scripts/build.sh --core-only          # bez SDL/OpenGL: model, narzędzie i testy
./scripts/format.sh --check             # styl C++ (.clang-format)
./scripts/lint.sh                       # clang-tidy (.clang-tidy)
```

Ostrzeżenia są błędami (`-Werror`, `/WX`). Testy GPU bez kontekstu OpenGL 4.3 kończą się
kodem 77, który CTest oznacza jako pominięty. Tryby kontrolne `StrefaQA` (`--gpu-*-check`,
`--mission-test`, `--lab-smoke-test` …) i smoke testy `StrefaZero` zapisują raporty w `--profile`
i kończą proces. Skrypty `scripts/profiling/*.ps1` przyjmują `-Executable StrefaQA.exe`.

`StrefaPhysics` uruchamia sceny CPU bez okna:

```bash
./build/linux-release/StrefaPhysics --scene all --ticks 180 --output recordings --record
./build/linux-release/StrefaPhysics --scene arena --ticks 600 --output benchmark
```

## Struktura

```
src/core      ustawienia i schemat metadanych
src/sim       model CPU: świat, termika, ciecze MAC/PCG, powietrze, reakcje, sceny
src/game      sesja gry, protokół świata, animacja i IK
src/gpu       świat GPU: PBF/PBD, renderer, VFX, termika, oświetlenie; walidacje (gpu/validation.hpp)
src/platform  okno SDL, kontekst GL, tekstury, zapis ustawień
src/ui        menu, laboratoria, poligon (ImGui)
src/app       aplikacja, opcje uruchomienia, scenariusze diagnostyczne (te ostatnie tylko w StrefaQA)
src/tools     StrefaPhysics (sceny CPU bez okna) i StrefaQA (walidacje, benchmarki, profile)
assets/       shadery GLSL, sprite'y, tła, czcionki
tests/        testy CTest
scripts/      build, run, pakowanie, format i lint; profiling/ — narzędzia pomiarowe
docs/         dokumentacja — zob. docs/README.md
```

## Dokumentacja

[Indeks dokumentacji](docs/README.md) · [Architektura](docs/ARCHITECTURE.md) ·
[Model fizyki](docs/PARTICLES.md) · [Historia zmian](docs/CHANGELOG.md) ·
[Plan rozwoju GPU](docs/plans/PLAN_ROZWOJU_GPU.md)

To przybliżenia do gry: interfejsy cieczy mają grubość komórki, ciśnienie i prędkość dźwięku
mają skalę gry, a energia mechaniczna jest tłumiona. Sztywne bryły, ragdolle, boty,
multiplayer i zapis stanu świata pozostają poza tym wydaniem.

Gotowy program Windows i surowe obrazy QA: [wydanie I11 Preview](https://github.com/maershal/strefa-zero/releases/tag/v0.7.0-i11-preview.1).
Licencje zależności i zasobów: [licenses/](licenses/).
