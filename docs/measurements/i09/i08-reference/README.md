# Referencja I08 do I09

Archiwum zawiera zachowany początkowy stan źródeł I08, łącznie z zastanymi zmianami. Pomiar używa oryginalnego renderera i solvera. Jedyną zmianą wykonawczą jest `reference-render-validation.inl`: gałąź benchmarku uruchamia te same cztery sceny i zakresy czasu co I09, bez puli VFX. `harness.patch` pokazuje pełną zmianę. Build lokalny pożyczał tę samą bibliotekę SDL3 z builda I09; kompilator, Release/LTO i ustawienia są takie same.

Odtworzenie na Windows (PowerShell, Developer Prompt MSVC):

```powershell
Expand-Archive i08-initial-source.zip i08-source
Copy-Item reference-render-validation.inl i08-source/src/gpu/render_validation.inl -Force
cmake -S i08-source -B i08-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTREFA_WARNINGS_AS_ERRORS=ON
cmake --build i08-build --target strefa_zero --parallel 4
./i08-build/StrefaZero.exe --gpu-render-benchmark --profile i08-wyniki
```

96 segmentów × 240 próbek = 23 040 próbek, z 120 klatkami rozgrzewki na segment. Wszystkie cztery pozycje w każdym bloku są I08 bez VFX. Jest to osobny proces referencyjny: jego różnicy względem I09 nie przedstawiamy jako sparowanego przyrostu VFX. Bramkę 0,25 ms liczy macierz ABBA I09 względem swoich dwóch referencji off.
