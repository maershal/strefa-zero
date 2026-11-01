# Port Linux / WSL2 — weryfikacja 2026-11-01

## Środowisko i zakres

Ubuntu 24.04.4 x86_64 w WSL2, kernel `6.18.33.2-microsoft-standard-WSL2`,
WSLg 1.0.73.2, GCC 13.3.0, CMake 3.28.3 i Ninja. Release z C++23,
`-Wall -Wextra -Wpedantic -Werror` oraz LTO. SDL 3.4.18 i ImGui 1.93.0
pochodzą z dotychczasowych archiwów zweryfikowanych przez SHA-256.

Grafika sprzętowa: Mesa 25.2.8 D3D12 / NVIDIA GeForce GTX 1650, OpenGL 4.6.
Renderer programowy: llvmpipe LLVM 20.1.2, OpenGL 4.5. W tej dystrybucji
automatyczny wybór Mesa wskazywał llvmpipe mimo działającego D3D12. Launcher
wybiera zainstalowany D3D12 przy dostępnym `/dev/dxg`, respektując jawne zmienne
użytkownika i opcję `--software`.

Lokalne SDL zbudowano dla X11/WSLg. Przy brakujących nagłówkach opcjonalnych
rozszerzeń użyto `-DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XTEST=OFF`; nie instalowano
pakietów systemowych. Wayland nie był lokalnie walidowany. Instrukcja i CI
instalują pełny zestaw zależności X11/Wayland przez `apt`.

## Poprawki wymagane przez port

- Rozdzielono instrukcje zgłaszane przez GCC jako misleading indentation,
  usunięto nieużywaną funkcję i zbędne kopie w pętlach, dodano jawne inicjalizatory.
  Ostrzeżenia pozostają błędami; nie dodano wyciszeń kompilatora.
- WSLg/X11 zwracało `That operation is not supported` dla obu wartości
  `SDL_GL_SetSwapInterval`. Ponowne ustawienie już wyłączonego VSync blokowało
  zmianę motywu i rozmiaru okna. `Window::apply_video` zmienia interwał tylko,
  gdy użytkownik zmienia żądanie i faktyczny stan VSync. Zachowuje fallback
  wyłączonego VSync także przy ponownym zastosowaniu niezmienionego żądania.
  Jawne nieudane włączenie nadal zwraca błąd.
- Na Mesa D3D12 zoptymalizowany warunkowy dispatch podmuchu nie nadawał impulsu
  odkrytej cząstce: `vx=0`. Ten sam pojedynczy tick dawał `vx=27.6806` na
  llvmpipe oraz po wyłączeniu `conditional_work` na D3D12. Wyłączenie cache
  wiązań lub przejście na AoS nie usuwało problemu. To uzasadnia obejście
  wyboru ścieżki, bez przypisywania ostatecznej przyczyny sterownikowi.
- Dla renderera `D3D12 (...)` świat wybiera istniejące bezpośrednie przebiegi
  opcjonalnych obliczeń. Pośrednie uruchamianie aktywnych cząstek, SoA,
  sąsiedztwo i równania fizyki pozostają dostępne. Inne renderery zachowują
  warunkowe przebiegi. Raporty okna ujawniają żądane i faktyczne ustawienie.
- Nowy `gpu.platform_dispatch` sprawdza fizyczny impuls, osłonę terenową,
  liczbę eksplozji i błędy OpenGL na rzeczywistym kontekście 4.3+. Pominięcie
  z kodem 77 pozostaje odrębne od zaliczenia.
- Paczka Linux instaluje runtime SDL z dowiązaniami i `$ORIGIN`, oba programy,
  zasoby, licencje oraz launcher. Oddzielny komponent instalacji pomija SDK SDL.

## Wyniki

| Próba | Wynik |
|---|---|
| Pełna kompilacja Release, GCC, warnings as errors | PASS |
| Kompilacja `--core-only` bez SDL/OpenGL | PASS |
| Testy CPU (`ctest -LE graphics`) | 11/11 PASS |
| Okno i zapisywanie ustawień (`app.opengl_smoke`) | PASS |
| Żądany kontekst 3.3 (`app.opengl33_smoke`) | PASS |
| Rzeczywisty kontekst Mesa OpenGL 3.3 / GLSL 330 | PASS, 4,93 s |
| Okno na programowym Mesa (`--smoke-test`) | PASS |
| Laboratorium GPU (`gpu.laboratory_ui`) | PASS |
| Cache shaderów (`gpu.shader_cache`) | PASS |
| Protokół GPU w przeniesionej paczce (`--gameplay-check`) | PASS, 3 próby kontraktu misji; automatyczny D3D12 |
| Podmuch i osłona po obejściu (`gpu.platform_dispatch`) | PASS: `vx=27.6806` / `vx=0` |
| Podmuch, osłona i fallback VSync na llvmpipe | PASS, 0,42 s |
| Wspólna scena po obejściu (`app.shared_session`) | PASS, 74,07 s |
| Pełna fizyka misji po obejściu (`gpu.mission`) | PASS, 568,02 s; ukończone trasy 30/60/144 FPS |
| Długi test okna misji po obejściu (`gpu.mission_ui`) | TIMEOUT, 60,45 s; wynik pozostaje otwarty |
| Narzędzie fizyki bez DISPLAY i WAYLAND_DISPLAY | PASS, scena arena, 5 ticków |
| Paczka przeniesiona do katalogu ze spacją, SDL przez `$ORIGIN` | PASS |

Końcowe uzupełniające wyniki oraz raporty znajdują się w `linux-wsl/`.
Nie deklarujemy zaliczenia całego CTest ani odbioru wydajności I04.5–I11.
Końcowe krótkie próby D3D12 po poprawkach okna: 3/3 PASS (19,53 s);
obejmują test platformy wraz z VSync, pełny smoke aplikacji i laboratorium GPU.

Pierwsze próby przed poprawkami nie przeszły: zapis ustawień, podmuch w
`gpu.mission`, `app.shared_session` (90 s) i `gpu.mission_ui` (60 s). Pierwsze
trzy problemy po poprawkach przeszły ponowną weryfikację. Długi test okna misji
zachowuje wcześniejsze wymagania 900 klatek, 1700 ticków i braku utraty czasu;
timeout występował także przed portem w Windows (raport I05). Nie podniesiono
limitów ani nie zmniejszono progów. Krótki zestaw budowania używa odrębnych
prób funkcjonalnych; pełny zestaw nadal zawiera test akceptacji wydajności.

## Odtworzenie

```bash
./scripts/build.sh --build-dir "$HOME/.cache/strefa-zero/release"
./scripts/run.sh --build-dir "$HOME/.cache/strefa-zero/release" --mission
./scripts/package-linux.sh "$HOME/.cache/strefa-zero/release"
```

Głębsze próby, wykonywane szeregowo:

```bash
GALLIUM_DRIVER=d3d12 ctest --test-dir "$HOME/.cache/strefa-zero/release" \
  --output-on-failure --parallel 1 \
  -R '^(gpu.platform_dispatch|gpu.gameplay|app.shared_session|gpu.mission|gpu.mission_ui)$'
```

Ścieżka programowa jest przydatna do sprawdzania zgodności i CI; jej wyniki
nie stanowią pomiarów wydajności GTX 1650. Paczka Ubuntu 24.04 wymaga zgodnych
glibc/libstdc++ i systemowego OpenGL; nie jest uniwersalnym AppImage.
