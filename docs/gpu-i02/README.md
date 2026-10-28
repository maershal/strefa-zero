# Dane I02

Interpretacja: [I02_GPU_EXECUTION.md](../I02_GPU_EXECUTION.md).

- `benchmark/`: wszystkie 51 partii pomiarowych, każdy wariant we wszystkich trzech powtórzeniach. Po 120 ticków rozgrzewki i 240 pomiarowych, świeży świat na partię. Sceny 16k/32k składają się z ziaren z postacią; 3096 to mieszana misja. `progress.txt` pokazuje faktyczną kolejność, odwróconą w drugim powtórzeniu.
- `benchmark/summary.csv`: CPU, GPU, fazy, zewnętrzny odczyt oraz średnia całej partii. `steps.csv` zachowuje każdy zmierzony tick i liczniki. Pomiary czasu grup są włączone we wszystkich wariantach.
- `benchmark/runtime-source-sha256.json`: źródła świata, API GL i shaderów użytych do benchmarku, zgodne z końcową wersją. Osobno zapisano hash zmierzonego EXE; końcowy rebuild dodaje weryfikację wykonania każdego aktywnego ID, porównanie impulsu w miejscu i logowanie ustawień harmonogramu. Kod świata i shaderów pozostał ten sam.
- `tests/`: 12/12 regresji, wyniki osobnej misji i instrumentacji reference, test execution z debugiem oraz poprawne pominięcie GPU na GL 3.3 (77). `ctest-output/` zawiera tekstowe wyniki testów z katalogu builda. Nie deklarujemy uruchomienia wszystkich 19 zarejestrowanych testów.
- `windows/`: wszystkie sześć prób okna po 900 klatek oraz kody wyjścia `runs.csv`. Każda przeszła, wynik misji 3, zgubiony czas 0. `window-summary.csv` pomija tylko pierwsze 30 klatek; surowe CSV zachowuje je. BMP pozostają w katalogu roboczym.
- `package-check/`: test z relokowanej paczki I02 i sprawdzenie kodu 77 na wymuszonym GL 3.3.
- `gpu-telemetry.csv` i `windows/gpu-telemetry.csv`: niezależny zapis stanu GPU podczas benchmarku i okien. Nie identyfikuje przyczyny historycznych spowolnień.
- `source-sha256.json`, `metadata.json`, `SHA256SUMS.txt`: identyfikacja źródeł końcowych, zakres testów i sumy danych.

Łączny koszt CPU okna liczymy dla każdej klatki jako `frame_step_submit_ms + ui_poll_ms`, a następnie wyznaczamy percentyle. Nie sumujemy percentyli składników. P95 używa indeksu `min(n-1, floor(0.95*n))` w posortowanych danych. Tabela raportu z procentami GPU używa median trzech median powtórzeń.

`dispatches` to liczba wywołań API; dispatch z zerem grup też jest liczony. `indirect_dispatches` to jej podzbiór. `timer_submissions` obejmuje pomiary fizyki, renderu i grup; `timer_skips` dotyczy pominiętych pomiarów fizyki. Test pełnej kolejki wyłącza pomiary grup, aby niezależnie porównać przyjęte próbki fizyki z ich odczytanymi wynikami.

Benchmark jest bez limitu klatek. Jego `glFinish()` co 16 ticków jest poza czasem CPU kroku i pozostaje częścią średniej całej partii. Gra korzysta z normalnego odświeżania okna i asynchronicznego HUD z I01. Pomyślne próby tej serii nie dowodzą usunięcia okresowych spowolnień GPU obserwowanych wcześniej.
