# Kontrola GPU na Windows: benchmark scen i soak

Procedura do wykonania ręcznie na laptopie z kartą NVIDIA (GTX 1650). Daje dwa wyniki: koszt każdej sceny w oknie
(podstawa decyzji o treści i optymalizacjach) oraz odpowiedź, czy i kiedy pojawia się wolny reżim z
[G3-B](../reports/I04_5G3B_GPU_REPLAY.md) (fizyka GPU ×2,7–4,1) i które powody ograniczenia zegara są wtedy aktywne.
Zasady pomiaru: sekcja „Protokół pomiaru” w `CLAUDE.md`.

## 1. Przygotowanie

1. Zasilacz podłączony, plan zasilania zrównoważony albo wysoka wydajność (raport go zapisze).
2. Zamknij przeglądarki, komunikatory i gry; wyłącz nagrywanie ekranu i nakładki (GeForce Experience, Discord).
3. Zbuduj wersję Release w PowerShell 7.2 lub nowszym z katalogu repozytorium:

   ```powershell
   pwsh ./scripts/build.ps1 -Configuration Release -SkipTests
   ```

4. Zostaw laptop bezczynny przez 5 minut, żeby GPU ostygło.
5. Benchmark otwiera widoczne okno. Nie minimalizuj go, nie zasłaniaj innym oknem, nie przełączaj pulpitu
   wirtualnego i nie używaj komputera do końca pomiaru. Scena, w której okno choć przez jedną klatkę było ukryte,
   zminimalizowane, zasłonięte albo ukryte przez DWM, dostaje status `invalid`. Klatki z zminimalizowanym oknem
   (symulacja wtedy stoi) liczą się do okna pomiaru sceny, więc minimalizacja nie skraca ani nie omija pomiaru.
   Rozgrzewka nie ma wierszy klatek, ale okno poza ekranem w jej trakcie liczy kolumna `warmup_away_frames`
   i ono też unieważnia scenę.
   Na Windows SDL 3.4 nie zgłasza zasłonięcia (`SDL_EVENT_WINDOW_OCCLUDED`), więc benchmark sprawdza tam co klatkę
   także `IsIconic` (liczone jako `minimized`) i `DwmGetWindowAttribute(DWMWA_CLOAKED)` (kolumna `cloaked`). Okna
   zasłoniętego innym oknem na Windows nie da się w ten sposób wykryć: licznik `unfocused_frames` (wypisywany też
   w wierszu podsumowania jako „bez fokusu”) pokazuje, że ktoś używał komputera, ale nie unieważnia pomiaru.
6. Zamknięcie okna (albo Ctrl+C w konsoli, czyli `SDL_EVENT_QUIT`) przerywa cały benchmark, a nie tylko bieżącą
   scenę: przerwana scena dostaje status `failed` z powodem „przerwany zamknięciem okna (QUIT)”, kolejne sceny nie
   startują, a kod wyjścia to 1.

## 2. Benchmark wszystkich scen (z VSync i bez)

Każde polecenie trwa ok. 32 × 70 s ≈ 37 min (60 s pomiaru + 10 s rozgrzewki na scenę). Telemetria zapisuje się
obok wyników:

```powershell
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false) # polskie znaki w summary.txt
Import-Module ./scripts/profiling/GpuTelemetry.psm1
foreach ($vsync in 'on', 'off') {
    $out = "gpu-check/benchmark-vsync-$vsync"
    New-Item -ItemType Directory $out -Force | Out-Null
    $telemetry = Start-GpuTelemetry -Path "$out/telemetry.csv"
    ./build/release/StrefaQA.exe --scene-benchmark all --seconds 60 --vsync $vsync --profile $out |
        Tee-Object "$out/summary.txt"
    Stop-GpuTelemetry $telemetry
    Start-Sleep -Seconds 300 # ostygnięcie przed drugim przebiegiem
}
```

- Kod wyjścia 0: wszystkie sceny mają status `ok`.
- Kod 1: co najmniej jedna scena ma status `invalid` (okno niewidoczne) albo `failed` (błąd trybu). Pozostałe sceny są
  zmierzone, a powód jest w kolumnie `status_reason`.
- Kod 77: brak OpenGL 4.3 (w profilu jest wtedy `gpu-skipped.txt`, a CTest `gpu.scene_benchmark_content` też
  jest pomijany).

`scene-benchmark-info.txt` jest zapisywany na dysk przed pierwszą sceną, więc zostaje także po awarii albo zabiciu
procesu. Na końcu dochodzą do niego `result=completed|quit` i `scenes_measured=<liczba>`, a przy kodzie wyjścia sceny innym
niż 0 (np. 77) `result=exit_code_<kod>`; brak `result=` oznacza przerwany proces. Plik ma `schema=3` (nowe pola
okna i `result=`). Pola `window_iconic` i `window_cloaked` opisują stan okna na starcie.

`--vsync on` pokazuje, jak gra działa u gracza; `--vsync off` (bez ogranicznika klatek) mierzy koszt. Rozmiar okna
można ustalić opcją `--test-window-size 1600 900`; `scene-benchmark-info.txt` zapisuje faktyczny rozmiar, skalę DPI
i jakości z ustawień domyślnych.

Misja odtwarza zapisaną trasę (1340 ticków ≈ 11 s). W dłuższym oknie trasa zaczyna się od nowa. `route_reset=1`
ma klatka po tej, która zażądała restartu (jej `frame_ms` zawiera przeładowanie świata), i nie wchodzi do statystyk
czasu klatki; liczba restartów jest w kolumnie `route_resets`.

## 3. Soak 15 minut

```powershell
pwsh ./scripts/profiling/soak.ps1 -Executable build/release/StrefaQA.exe -Output gpu-check/soak -SingleProcess `
    -Baseline gpu-check/benchmark-vsync-off/scene-benchmark.csv
```

Skrypt powtarza przebiegi po 17 scenach GPU (12 scen laboratorium, misja, 4 sceny termiki, po 10 s pomiaru,
VSync off) przez 15 minut i przez cały czas zbiera telemetrię `nvidia-smi` co 500 ms.

- `-SingleProcess` uruchamia wszystkie przebiegi w jednym procesie StrefaQA (`--loop-minutes`). Bez tej opcji każdy
  przebieg to nowy proces, a restart procesu i przerwa między przebiegami mogą przerwać wolny reżim.
- `-Baseline` to `scene-benchmark.csv` z przebiegu na ostygniętym GPU z tymi samymi ustawieniami. Ten sam `--vsync`
  daje porównywalne mediany fizyki.

Pomiar sceny jest oznaczany według trzech niezależnych kryteriów:

| Kryterium | Warunek |
| --- | --- |
| względne | mediana fizyki GPU ≥ 2× najlepszy przebieg tej sceny w soaku (`-SlowFactor`) |
| bezwzględne | mediana fizyki GPU ≥ 2× mediana tej sceny w `-Baseline` |
| telemetryczne | większość próbek poza P0 albo mediana zegara pamięci poniżej 90% maksimum sesji |

Samo kryterium względne nie wykryje reżimu, który trwa od pierwszego przebiegu. Kryterium telemetryczne oznacza też
lekkie sceny, w których sterownik sam obniża P-stan. Wyniki: `soak-report.md`, `soak-summary.csv`, `telemetry.csv`,
`environment.json` i katalog `run/` (albo `pass-NN/` bez `-SingleProcess`).

## 4. Co odesłać

Wklej do rozmowy albo do opisu PR:

1. Zawartość `gpu-check/benchmark-vsync-on/summary.txt` i `gpu-check/benchmark-vsync-off/summary.txt`
   (po jednym wierszu na scenę).
2. Cały `gpu-check/soak/soak-report.md`: wynik trzech kryteriów, tabelę powodów ograniczenia i tabelę przebiegów
   (≤ 10 wierszy).
3. Z `gpu-check/soak/environment.json`: pola `gpu`, `power_source`, `power_scheme` i `telemetry_reasons_source`.
4. Czy w trakcie coś się działo (aktualizacja Windows, uśpienie ekranu, odłączony zasilacz, inne okno na wierzchu).

Surowe pliki (`scene-benchmark*.csv`, `telemetry.csv`, `run/`, `pass-*/`) trafiają do repozytorium
[strefa-zero-measurements](https://github.com/maershal/strefa-zero-measurements) w nowym katalogu z własnym
`SHA256SUMS.txt`, a nie do tego repozytorium.

## Jak czytać wyniki

Symulacja ma 120 ticków na sekundę niezależnie od FPS. Przy `--vsync on` (60 Hz) każda klatka wykonuje 2 ticki.
Przy `--vsync off` gra rysuje setki klatek na sekundę, a większość z nich nie wykonuje żadnego ticku. **Mediana czasu
klatki bez VSync opisuje więc głównie klatki bez fizyki** i nie mówi, czy scena zmieści się w budżecie. Do tego służą:

- `frames_0_ticks` / `frames_1_tick` / `frames_2plus_ticks` z medianami czasu klatki osobno dla klatek z 0, 1 i 2+
  tickami;
- `gpu_ms_per_sim_second`: milisekundy GPU na sekundę symulacji, czyli (średnia fizyki + średnia prezentacji) × 120;
  nie zależy od FPS ani VSync;
- `gpu_ms_per_wall_second`: milisekundy GPU na sekundę zegara w tym przebiegu, czyli
  (fizyka + prezentacja) × ticki/s + render × FPS;
- `budget_60fps`: udział budżetu czasu GPU przy grze w czasie rzeczywistym w 60 FPS, czyli
  ((fizyka + prezentacja) × 120 + render × 60) / 1000 ms. Wartość ≥ 1 oznacza, że GPU nie nadąży i zegar zacznie
  pomijać czas (`dropped_seconds`). W wierszu podsumowania to „budżet 60 FPS”.

Sceny przejściowe (np. `gpu:dam-break`, `gpu:avalanche`) zmieniają się w czasie, więc wynik jest dodatkowo
podzielony na pierwsze 10 s okna pomiaru (`early_*`) i resztę (`late_*`): mediana klatki, ticki/s i średni czas GPU
ticku.

## Kolumny wyników

`scene-benchmark.csv` ma jeden wiersz na scenę i przebieg (`pass`):

- `status`: `ok`, `invalid` (okno ukryte, zminimalizowane, zasłonięte albo ukryte przez DWM w części klatek) lub
  `failed` (błąd sceny albo przerwanie przez QUIT), z powodem w `status_reason`; liczniki `hidden_frames`,
  `minimized_frames` (SDL albo `IsIconic`), `occluded_frames`, `unfocused_frames` (brak fokusu nie unieważnia
  pomiaru), `cloaked_frames` (`DWMWA_CLOAKED`, tylko Windows) i `warmup_away_frames` (klatki rozgrzewki i klatka
  otwierająca okno pomiaru z oknem poza ekranem; dwie ostatnie kolumny pliku);
- `window_seconds`: faktyczny czas okna pomiaru;
- czas klatki: mediana, p95, p99 i max;
- rozkłady GPU ze wszystkich zapytań zakończonych w oknie pomiaru (zbierane co klatkę, bez limitu):
  - `physics_*`: komendy, solver, diagnostyka i tick termiki;
  - `presentation_*`: prezentacja termiki, tick oświetlenia, aktualizacja VFX i zdarzenia;
  - `render_*`: przebieg renderowania świata;
  - `*_samples`: liczba próbek; `*_truncated=1`: próbki utracone przez pełną kolejkę;
  - `*_mean_ms`, `*_median_ms`, `*_p95_ms`, `*_p99_ms`;
- metryki kosztu z sekcji wyżej;
- `cpu_step_*`: krok wątku Laboratorium CPU (sceny `lab:`, które nie mają czasu GPU);
- `ticks_per_frame_*`, `ticks_per_second`, `frames_per_second`;
- `dropped_seconds`: czas pominięty przez zegar; w scenach `lab:` jest to spóźnienie wątku;
- `backlog_*`: zaległość zegara;
- `route_resets`: restarty trasy misji;
- `start_utc_unix_ns` i `end_utc_unix_ns`: granice okna pomiaru.

`scene-benchmark-frames.csv` ma wiersz na klatkę: liczbę i sumę próbek GPU zakończonych w tej klatce, flagi okna SDL
(`visible`, `focused`, `minimized`, `occluded`), `route_reset` i `cloaked` (ostatnia kolumna).

`telemetry.csv` ma te same znaczniki `utc_unix_ns` (UTC, ns od 1970), więc okno sceny łączy się z próbkami
telemetrii przez zakres (`Get-GpuTelemetryWindow`). Powody ograniczenia zegara są w kolumnach `reason_*` (1 = aktywny).
`gpu_idle` oznacza tylko, że GPU czekało na pracę. `sw_power_cap` to praca na limicie mocy, zwykła dla laptopa pod
obciążeniem: raportuj go i porównuj pomiary przy tym samym limicie. `hw_slowdown`, `hw_thermal_slowdown`,
`hw_power_brake_slowdown` i `sw_thermal_slowdown` dyskwalifikują okno do porównań; `soak.ps1` oznacza je kolumną
`disqualified`.
