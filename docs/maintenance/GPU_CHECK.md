# Kontrola GPU na Windows: benchmark scen i soak

Procedura do wykonania ręcznie na laptopie z kartą NVIDIA (GTX 1650). Daje dwa wyniki: koszt każdej sceny w oknie
(podstawa decyzji o treści i optymalizacjach) oraz odpowiedź, czy i kiedy pojawia się wolny reżim z
[G3-B](../reports/I04_5G3B_GPU_REPLAY.md) (fizyka GPU ×2,7–4,1) i które powody ograniczenia zegara są wtedy aktywne.
Zasady pomiaru: sekcja „Protokół pomiaru” w `CLAUDE.md`.

## 1. Przygotowanie

1. Zasilacz podłączony, plan zasilania zrównoważony albo wysoka wydajność (raport go zapisze).
2. Zamknij przeglądarki, komunikatory i gry; wyłącz nagrywanie ekranu i nakładki (GeForce Experience, Discord).
3. Zbuduj wersję Release w PowerShell 7 z katalogu repozytorium:

   ```powershell
   pwsh ./scripts/build.ps1 -Configuration Release -SkipTests
   ```

4. Zostaw laptop bezczynny przez 5 minut, żeby GPU ostygło. Okno benchmarku nie może być zminimalizowane ani
   zasłonięte przez inne okno pełnoekranowe.

## 2. Benchmark wszystkich scen (z VSync i bez)

Każde polecenie trwa ok. 32 × 70 s ≈ 37 min (60 s pomiaru + 10 s rozgrzewki na scenę). Telemetria zapisuje się
obok wyników:

```powershell
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

Kod wyjścia 0 oznacza komplet wyników, 77 brak OpenGL 4.3, a inne kody to błąd opisany w `smoke-error.txt`.
`--vsync on` pokazuje, jak gra działa u gracza; `--vsync off` (bez ogranicznika klatek) mierzy koszt.

## 3. Soak 15 minut

```powershell
pwsh ./scripts/profiling/soak.ps1 -Executable build/release/StrefaQA.exe -Output gpu-check/soak
```

Skrypt powtarza przebiegi po 17 scenach GPU (12 scen laboratorium, misja, 4 sceny termiki, po 10 s pomiaru,
VSync off) przez 15 minut i przez cały czas zbiera telemetrię `nvidia-smi` co 500 ms. Pomiar sceny jest „wolny”, gdy
mediana fizyki GPU jest co najmniej 2× większa niż w najlepszym przebiegu tej sceny (`-SlowFactor`). Wynik:
`soak-report.md`, `soak-summary.csv`, `telemetry.csv`, `environment.json` i katalogi `pass-NN/`.

## 4. Co odesłać

Wklej do rozmowy albo do opisu PR:

1. Zawartość `gpu-check/benchmark-vsync-on/summary.txt` i `gpu-check/benchmark-vsync-off/summary.txt`
   (po jednym wierszu na scenę).
2. Cały `gpu-check/soak/soak-report.md`: wynik, tabelę powodów ograniczenia i tabelę przebiegów (≤ 10 wierszy).
3. Z `gpu-check/soak/environment.json`: pola `gpu`, `power_source`, `power_scheme` i `telemetry_reasons_source`.
4. Czy w trakcie coś się działo (aktualizacja Windows, uśpienie ekranu, odłączony zasilacz, inne okno na wierzchu).

Surowe pliki (`scene-benchmark*.csv`, `telemetry.csv`, `pass-*/`) trafiają do repozytorium
[strefa-zero-measurements](https://github.com/maershal/strefa-zero-measurements) w nowym katalogu z własnym
`SHA256SUMS.txt`, a nie do tego repozytorium.

## Kolumny wyników

`scene-benchmark.csv` ma jeden wiersz na scenę:

- czas klatki: mediana, p95, p99 i max;
- `gpu_samples` i rozkłady GPU z każdego zakończonego zapytania w oknie pomiaru:
  - `physics_*`: komendy, solver, diagnostyka i tick termiki;
  - `presentation_*`: prezentacja termiki, tick oświetlenia, aktualizacja VFX i zdarzenia;
  - `render_*`: przebieg renderowania świata;
- `cpu_step_*`: krok wątku Laboratorium CPU (sceny `lab:`, które nie mają czasu GPU);
- `ticks_per_frame_*`;
- `dropped_seconds`: czas pominięty przez zegar; w scenach `lab:` jest to spóźnienie wątku;
- `backlog_*`: zaległość zegara;
- `start_utc_unix_ns` i `end_utc_unix_ns`: granice okna pomiaru.

`telemetry.csv` ma te same znaczniki `utc_unix_ns` (UTC, ns od 1970), więc okno sceny łączy się z próbkami
telemetrii przez zakres (`Get-GpuTelemetryWindow`). Powody ograniczenia zegara są w kolumnach `reason_*` (1 = aktywny).
`gpu_idle` oznacza tylko, że GPU czekało na pracę; pozostałe powody (`sw_power_cap`, `hw_slowdown`,
`hw_thermal_slowdown`, `hw_power_brake_slowdown`, `sw_thermal_slowdown`) dyskwalifikują pomiar do porównań.
