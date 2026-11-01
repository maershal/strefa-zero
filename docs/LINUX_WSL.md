# Strefa Zero — Linux i WSL2

Gra i narzędzie fizyki są natywnymi programami Linux ELF. WSL2 wyświetla okno
przez WSLg (X11 lub Wayland). Nie potrzeba Wine, plików EXE ani bibliotek DLL.
Podstawowy system docelowy: Ubuntu 24.04 x86_64, GCC 13, CMake 3.28 i Ninja.

## Gotowa paczka

Rozpakuj `StrefaZero-Linux-x86_64.tar.gz` w terminalu Ubuntu/WSL:

```bash
tar -xzf StrefaZero-Linux-x86_64.tar.gz
cd StrefaZero-Linux-x86_64
./run.sh --mission
```

Zachowaj obok programu `libSDL3.so*`, `assets/` i `licenses/`. Biblioteka SDL
jest dołączona i wyszukiwana względem programu przez `$ORIGIN`; nie trzeba
ustawiać `LD_LIBRARY_PATH`. Paczka korzysta z systemowych glibc, libstdc++,
OpenGL/Mesa i bibliotek pulpitu. Jest budowana na Ubuntu 24.04; starsze wydania
mogą wymagać kompilacji ze źródeł. `sha256sum -c SHA256SUMS.txt` sprawdza pliki.

Wymagania uruchomieniowe Ubuntu:

```bash
sudo apt update
sudo apt install libgl1 libopengl0 libgl1-mesa-dri libegl1 libx11-6 libxext6 libxcursor1 \
  libxi6 libxfixes3 libxrandr2 libxss1 libxtst6 libwayland-client0 \
  libwayland-cursor0 libwayland-egl1 libxkbcommon0 libdecor-0-0
```

## WSLg i grafika

Potrzebny jest WSL2 z WSLg oraz sterownik karty graficznej zainstalowany
w Windows. Konfigurację opisuje [dokumentacja Microsoft](https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps).
WSLg ustawia `DISPLAY` i `WAYLAND_DISPLAY`; zachowaj ich automatyczne wartości.
Launcher `run.sh` w WSL wybiera zainstalowany sterownik Mesa D3D12, jeśli
dostępne jest `/dev/dxg`. Pozwala to uniknąć niezamierzonego użycia llvmpipe.
Jawne `GALLIUM_DRIVER` i `LIBGL_ALWAYS_SOFTWARE` mają pierwszeństwo;
`./run.sh --software --mission` wybiera renderer programowy.
W WSL wersję tego sterownika sprawdzisz przez `GALLIUM_DRIVER=d3d12 glxinfo -B`.
Na Mesa D3D12 świat używa bezpośredniej ścieżki opcjonalnych obliczeń compute:
test podmuchu wykazał pomijanie impulsu przy warunkowym dispatchu. Zachowano
pośrednie uruchamianie aktywnych cząstek i dotychczasowe równania fizyki.
`gpu.platform_dispatch` sprawdza rzeczywisty impuls oraz osłanianie przez ścianę.
Raport okna podaje żądaną i faktyczną wartość `conditional_work`.
Nie uruchamiaj gry przez `sudo`. Przy problemach z Waylandem można wybrać X11:

```bash
SDL_VIDEO_DRIVER=x11 ./StrefaZero --mission
```

Poligon CPU i laboratorium CPU wymagają OpenGL 3.3. Misja, laboratorium GPU
i efekty compute wymagają OpenGL 4.3. Dostępna wersja zależy od sterownika
udostępnianego w WSLg, a nie od samej karty w Windows. Sprawdź ją poleceniem
`glxinfo -B` (pakiet `mesa-utils`). Jeśli sterownik nie udostępnia 4.3, można
uruchomić poligon CPU albo diagnostycznie użyć programowego Mesa llvmpipe:

```bash
./StrefaZero --arena --opengl33
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe ./StrefaZero --mission
```

llvmpipe wykonuje obliczenia na CPU i może być znacznie wolniejsze. Nie wymuszaj
wersji przez `MESA_GL_VERSION_OVERRIDE`: nie dodaje to brakujących funkcji.

## Budowanie ze źródeł

W terminalu Ubuntu 24.04 / WSL2:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config \
  libgl-dev libegl-dev libx11-dev libxext-dev libxcursor-dev libxi-dev \
  libxfixes-dev libxrandr-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libdecor-0-dev libasound2-dev \
  libpulse-dev libudev-dev libdbus-1-dev libdrm-dev libgbm-dev
git clone https://github.com/maershal/strefa-zero.git
cd strefa-zero
./scripts/build.sh
./scripts/run.sh --mission
```

Pierwsza konfiguracja pobiera przypięte SDL 3.4.18 i ImGui 1.93.0 z kontrolą
SHA-256. Można podać `--dependency-cache /ścieżka/do/archiwów` z plikami
`SDL3-3.4.18.tar.gz` oraz `imgui-1.93.0.tar.gz`. Kompilacja używa C++23 oraz
`-Wall -Wextra -Wpedantic -Werror`, a Release włącza LTO.

Linux ma oddzielne katalogi `build/linux-release` i `build/linux-debug`.
Nie używaj katalogu skonfigurowanego wcześniej przez Windows/MSVC. Kod może
znajdować się w `/mnt/c`, ale kompilacja będzie szybsza w systemie plików WSL:

```bash
./scripts/build.sh --build-dir "$HOME/.cache/strefa-zero/release"
./scripts/run.sh --build-dir "$HOME/.cache/strefa-zero/release" --mission
./scripts/run.sh --build-dir "$HOME/.cache/strefa-zero/release" --software --mission
```

`--configuration Debug` wybiera Debug, a `--jobs 2` ogranicza zużycie pamięci.
Dodatkowe opcje CMake można przekazać po `--`. Dla pracy bez pulpitu i OpenGL:

```bash
./scripts/build.sh --core-only
./build/linux-release-core/StrefaPhysics --scene arena --ticks 60 --output /tmp/strefa-physics
```

Dostępne są też presety `linux-release`, `linux-debug` i `linux-core-only`:

```bash
cmake --preset linux-release
cmake --build --preset linux-release --parallel 4
ctest --preset linux-release -LE graphics
```

## Testy i paczka

`build.sh` domyślnie uruchamia testy CPU oraz krótkie testy okna OpenGL 3.3,
podstawowego okna, protokołu misji i laboratorium GPU. `--test-suite cpu` wybiera tylko
testy bez okna, `--test-suite all` pełny CTest, a `--skip-tests` samą kompilację.
Testy GPU wymagają kontekstu 4.3; na starszym sterowniku kod 77 oznacza
**pominięcie**, a nie zaliczenie fizyki GPU. Testy graficzne wykonuj szeregowo.
Pełne QA jest znacznie dłuższe, zwłaszcza na llvmpipe.

`gpu.mission_ui` pozostaje osobnym testem akceptacji wydajności: wymaga co
najmniej 900 klatek, 1700 ticków i braku utraty czasu, z limitem 60 s. Timeout
występował także w Windows przed portem ([raport I05](https://github.com/maershal/strefa-zero/blob/main/docs/I05_SHARED_WORLD.md)).
Test zachowuje próg i pozostaje w pełnym QA (`--test-suite all`, etykieta
`performance`); krótki zestaw sprawdza protokół misji przez `gpu.gameplay`.
Przeniesienie na Linux nie zamyka wcześniejszego odbioru wydajności.

```bash
ctest --test-dir build/linux-release -LE graphics --output-on-failure
ctest --test-dir build/linux-release -R '^app\.opengl' --output-on-failure
ctest --test-dir build/linux-release -R '^gpu\.mission_ui$' --output-on-failure
./scripts/package-linux.sh
# Niestandardowy katalog kompilacji i folder wyjściowy:
./scripts/package-linux.sh "$HOME/.cache/strefa-zero/release" ./dist
```

Paczka i jej suma SHA-256 trafiają do `dist/`. Zawierają oba programy,
bibliotekę SDL z dowiązaniami, launcher `run.sh`, zasoby, licencje i tę instrukcję. Testy instalacji
należy uruchamiać po rozpakowaniu w innym katalogu, bez `LD_LIBRARY_PATH`.
GitHub Actions buduje Linuxa, wykonuje testy CPU i testy okna przez Mesa/Xvfb
oraz udostępnia paczkę jako artefakt.

Środowisko testowe, poprawki zgodności i jawne ograniczenia:
[LINUX_WSL_QA.md](https://github.com/maershal/strefa-zero/blob/main/docs/reports/LINUX_WSL_QA.md).

Sterowanie: A/D — ruch, Spacja — skok, PPM — plecak, LPM — strzał,
R — przeładowanie, G — granat, F5 — reset, Esc — menu. Utrata fokusu pauzuje grę.
Ustawienia trafiają przez SDL do katalogu danych użytkownika (domyślnie
`~/.local/share/StrefaZero/Prototype`); `--profile /katalog` wybiera inny profil.
