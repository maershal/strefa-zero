# Dane pomiarowe I01

Interpretacja i ograniczenia: [raport I01](../I01_GPU_READBACK.md).

- `initial-tick-poll/`: pierwszy eksperyment; pierścień nadal odpytywany w tickach. Odrzucony.
- `rejected-get-synciv/`: eksperyment z GetSynciv; większy koszt odpytywania na badanym sterowniku. Odrzucony.
- `final-frame-poll/`: końcowa implementacja, HUD zbierany poza tickiem. Trzy powtórzenia każdego wariantu, 120 ticków rozgrzewki i 240 pomiarowych.
- `windows/`: wszystkie dziewięć prób okna, surowe klatki, kroki, liczniki oraz `window-summary.csv`. Pierwsze sześć prób przeszło; trzy z ostatniego powtórzenia nie spełniły kryterium zegara przy wolniejszym GPU. Wyniki fizyczne misji pozostały poprawne. Pliki BMP pozostają w katalogu roboczym; tutaj zapisano komplet danych tekstowych.
- `windows.log`: skrypt zakończył serię błędem przy legacy / 3. `windows/remaining.log` zawiera kody wyjścia osobno dokończonych auto / 3 i staging / 3. Nieudanych prób nie usunięto.
- `regression-tests.txt`: końcowe 11/11 wybranych testów. Nie jest to deklaracja uruchomienia całych 18 zarejestrowanych testów.
- `tests/`: bezpośredni test transportu z KHR_debug. Ostrzeżenia o migracji buforów dotyczą odczytów walidacyjnych; brak błędów GL.
- `package-check/`: test transportu po przeniesieniu paczki oraz poprawne pominięcie testu GPU na wymuszonym GL 3.3 (kod 77).
- `gpu-telemetry.csv`: zewnętrzny zapis stanu GPU podczas regresji i okien. Korelacje nie rozstrzygają przyczyny zmiennej wydajności.
- `source-sha256.json`, `metadata.json`: źródła końcowej wersji i zakres weryfikacji. Historyczne, odrzucone eksperymenty mają inne zachowanie niż ta wersja.

`window-summary.csv` pomija tylko pierwsze 30 klatek rozgrzewki. Łączny koszt CPU liczony jest dla każdej klatki jako `frame_step_submit_ms + ui_poll_ms`, a dopiero potem wyznacza się medianę i p95. Nie sumujemy percentyli obu składników. Surowe CSV zawierają także rozgrzewkę i nieudane przebiegi.

Domyślny transport nie gwarantuje małego kosztu każdego wywołania sterownika ani rozwiązania okresowego spowolnienia całego GPU. Niższy CPU submit nie oznacza automatycznie szybszej fizyki GPU.
