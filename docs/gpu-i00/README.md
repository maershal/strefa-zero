# Dane I00

Raport główny: [I00_GPU_PROFILING.md](../I00_GPU_PROFILING.md).

- `preliminary/`: seria współdzieląca bufory po walidacji; pokazuje zmianę reżimu osiągów.
- `final/`: świeże bufory na pomiar, telemetria GPU, testy, próba widocznego okna i porównanie jednego monitora.
- `original-early-window/`, `original-late-control/`: zachowana kopia 0.7.0 przed instrumentacją.
- `tests-full.txt`: 16/17, nieudany test zegara okna.
- `tests-window-retry.txt`: udana osobna powtórka, dalsze próby znów nieudane.
- `source-sha256.json`: skróty źródeł do odtworzenia implementacji.

Nie łączyć obu reżimów GPU ani prób ukrytego/widocznego okna w jedną medianę. Nieudane próby i przerwanie serii są zachowane. Eksporty BMP pominięto.
