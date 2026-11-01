# Dane I04.5-A/F — 2026-10-29

[Raport](../I04_5_GPU_STABILIZATION.md) wyjaśnia zakres, metodę, wyniki i ograniczenia.

- `measurements/`: główna seria 18 procesów; 3 tryby × 3 powtórzenia × przepustowość/okno. `analysis.csv` zachowuje status ważności każdego przebiegu; `analysis.json` warunki i ostrzeżenia.
- `controls/`: pojedyncze próby 144 FPS z debug, VSync i bez szczegółowych liczników. Nie tworzą trzech powtórzeń nowego benchmarku. Brak pełnego profilu klatek przy counters off jest zamierzony.
- `original-i04/`: dwie kontrole niezmienionej wydanej paczki, bez nowej instrumentacji. To kontrola bazy, nie identyczny A/B nowych timerów.
- `ctest.log`, `ctest.xml`: pełne 21/21 regresji; `ctest-final-focused.*`: końcowe 4/4 po uzupełnieniu metadanych/długości smoke.
- `tests/`: szczegółowe raporty kontraktów, trasy 30/60/144 FPS.
- `images/`: niezmienione BMP z ukończonej misji starej i nowej wersji; nie są pixel-diffem deterministycznej trajektorii.
- `build-*.log`, `install.log`: natywne logi budowania i przygotowania paczki.

Pełne obrazy pozostałych prób zachowano w katalogu roboczym. W kopii danych pominięto powtarzające się duże BMP; CSV, logi, metadane i informacje o błędach pozostają zachowane. Zmiennego fokusu ani czasu inicjalizacji nie ukryto. `gl-debug.txt` oznacza fazę ODBIORU callbacku, która może różnić się od chwili wcześniejszego wywołania GL.
