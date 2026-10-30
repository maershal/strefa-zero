# I04.5-A/F — pomiar całej klatki i kolor gruzu

Data: 2026-10-29. Baza: `v0.7.0-i04`, commit `6f318ea`, z lokalnymi zmianami opisanymi poniżej.

**Wdrożono instrumentację A oraz poprawkę F. Cel całego I04.5 pozostaje otwarty:** fizyka misji nadal przekracza 1,50 ms/tick, a historia okresowych spowolnień nie jest zamknięta. Kolejny zakres: I04.5-C — pomiar i ograniczenie pustej pracy solvera. I05 nie został rozpoczęty.

[Plan rozwoju](PLAN_ROZWOJU_GPU.md), [rozstrzygnięcia review](REVIEW_I04_ROZSTRZYGNIECIA.md), [poprzedni raport I04](I04_GPU_COUPLING.md).

## Zmiana

- `frame-timings.csv` opisuje bieżącą klatkę po prezentacji i limiterze: obsługę zdarzeń, metadane, UI, upload/poll, aktualizację świata, zlecenie renderu, odczyty obrazu, kontrolę GL, `present`, działania UI, sen i pozostały koszt. Koszt renderu jest wydzielony z aktualizacji, bez podwójnego sumowania.
- Dotychczasowe `frame_ms` w `mission-frames.csv` nadal oznacza poprzedni odstęp start→start. Zachowano pierwsze dziesięć kolumn; dodano dostępność i liczbę pomiarów GPU. Nowy plik nie przypisuje kosztu `present()` do poprzedniej klatki.
- Zapisywane są rzeczywiste: fokus, minimalizacja, flagi/rozmiar okna, tryb i odświeżanie monitora, swap interval, ticki na klatkę i zaległość zegara. Dodano wyłącznie odczyt `FixedStepClock::pending_seconds()`; parametry 120 Hz / 4 / 24 i polityka utraty czasu pozostają bez zmian.
- `--gpu-timer-mode elapsed|timestamp|off` porównuje dotychczasowe zapytania z parami timestampów lub wyłącza wszystkie timery, także grupowe. Domyślny pozostaje `elapsed`. Zmiana trybu odtwarza obiekty zapytań o właściwym targetcie, odrzuca stare wyniki i zachowuje nieblokujące sprawdzanie gotowości. Reset zeruje również wskaźnik dostępności pomiaru poprzedniej sceny.
- Osobny `--gpu-stabilization-profile` stosuje faktyczne opcje z CLI do misji, 16k i 32k. Nie korzysta z historycznych profilerów, które tworzą własne warianty. Brak timerów oznacza brak wyniku GPU, a nie pomiar 0 ms.
- KHR_debug otrzymał fazę odbioru, czas SDL i liczniki `id=131186` niezależne od limitu pełnych wpisów. Faza odbioru asynchronicznego callbacku nie jest dowodem dokładnego momentu wykonania wcześniejszej operacji. Benchmark ma osobny `phase-timeline.csv` ze znacznikami SDL/UTC dla inicjalizacji, rozgrzewki, pomiaru, renderu i walidacji.
- `--test-vsync` i `--test-frame-limit` działają wyłącznie w testach okna, w osobnym profilu. Misja wymaga minimum 900 klatek i 1700 ticków; przy domyślnych 60 FPS nadal kończy się po 900 klatkach. Dzięki temu próba przy 144 FPS lub VSync nie kończy trasy przed jej wykonaniem. Nie zmniejszono wymaganego wyniku ani tolerancji utraty czasu.
- Typ gruzu `motion.w == 4` ma teraz ceglany kolor przegrody, zamiast wpadać do gałęzi metalu. Fizyczny gruz i solver pozostają te same. Obrazy kontrolne przed/po zachowano w danych.

Szczegółowe pomiary są opt-in przez `--gpu-counters`. Kolekcja klatek ma limit 10000 wierszy i jawny licznik odrzuceń; eksport następuje po pętli. Zrzuty obrazu mają znacznik `capture_performed`; rutynowy `glGetError` pozostaje częścią kosztu testu. Pełny końcowy snapshot jest poza pomiarem klatek.

## Metoda i środowisko

Windows x64, GTX 1650, NVIDIA 610.47, OpenGL 4.3, MSVC 19.51 Release, `/W4 /WX`, IPO. Monitor raportuje około 143,99 Hz. Nie zmieniano taktowań, ustawień sterownika ani polityki zegara gry.

Główna seria: **18 poprawnie zakończonych procesów — 9 prób przepustowości i 9 prób okna**. Każdy tryb ma trzy powtórzenia. Dla trzech wariantów użyto jawnej kolejności Latin: elapsed/timestamp/off, timestamp/off/elapsed, off/elapsed/timestamp. Harness dla dwóch wariantów obsługuje przeplot ABBA. Zapisano argumenty, PID, UTC, status procesu, sumę EXE/shaderów i telemetrię NVIDIA co 500 ms. Procesy GPU uruchamiano kolejno.

Przepustowość: świeży świat na scenę, 120 ticków rozgrzewki, 240 pomiarowych, następnie osobny render 120 klatek. `glFinish` i odbiór HUD co 16 ticków są poza CPU submit, ale w koszcie przepustowości wall-clock. Pełna walidacja liczby i skończoności cząstek jest po pomiarze.

Okno: widoczne, VSync off, limiter 60 FPS. Analiza runtime pomija pierwsze 30 klatek i ostatnią klatkę ze zrzutami; każdy przebieg zawiera 869 takich próbek. Wszystkie 900 wierszy pozostają w CSV. Suma faz odpowiada czasowi klatki, maksymalny błąd sumowania około 1,42e-14 ms; brak utraconych wierszy.

**Fokus nie był wymuszany i zmieniał się między przebiegami.** Liczba skupionych próbek runtime w kolejnych próbach elapsed wyniosła 0/0/0, timestamp 683/0/227, off 0/0/378. Dlatego wyników nie traktujemy jako izolowanego dowodu wpływu samego timera. Metadane pozwalają filtrować fokus; nie zatajono tej różnicy warunków.

## Wyniki przepustowości

Wszystkie wartości to ms/tick, osobne wyniki trzech powtórzeń. Nie uśredniamy percentyli różnych przebiegów.

| Scena | Mediany GPU elapsed | Mediany GPU timestamp |
| --- | --- | --- |
| mission | 2,785 / 2,884 / 2,908 | 2,831 / 2,867 / 3,094 |
| stress_16k | 1,805 / 1,810 / 1,859 | 1,796 / 1,797 / 1,804 |
| stress_32k | 3,214 / 3,267 / 3,287 | 3,240 / 3,246 / 3,248 |

P95 GPU misji: elapsed 3,289 / 3,379 / 3,709 ms, timestamp 3,322 / 3,318 / 3,866 ms. Cel <1,50 ms nie został osiągnięty.

| Tryb | Średni wall-clock na tick misji, trzy osobne partie |
| --- | --- |
| elapsed | 2,908 / 2,971 / 3,156 ms |
| timestamp | 2,939 / 2,961 / 3,349 ms |
| off | 2,997 / 3,005 / 2,998 ms |

Wyłączenie timerów nie dało w tej serii przełomu w przepustowości. Jeden przebieg timestamp miał medianę CPU submit 0,560 ms, pozostałe zwykle około 0,010–0,013 ms; tej obserwacji nie usuwamy z raportu. Nie wybieramy timestampów jako naprawy wydajności i nie zmieniamy domyślnego trybu.

## Wyniki okna

Każda z dziewięciu misji ukończyła trzy cele i wyjście, **bez utraty czasu symulacji**. Mediany runtime wszystkich prób mieszczą się w 16,668–16,669 ms.

| Tryb | P95 runtime klatki, trzy próby | Mediana CPU present, trzy próby |
| --- | --- | --- |
| elapsed | 17,078 / 16,697 / 16,670 ms | 0,445 / 0,309 / 0,075 ms |
| timestamp | 16,707 / 17,160 / 17,062 ms | 0,321 / 0,473 / 0,080 ms |
| off | 18,893 / 17,922 / 16,669 ms | 0,525 / 0,403 / 0,064 ms |

Mediana jawnego oczekiwania limitera wynosi około 13,557–15,070 ms. Zdarzają się skoki całej klatki także bez timerów; nie zaliczamy twardego celu p95 ≤16,67 ms na podstawie samej mediany. W tej serii nie wystąpił stały reżim 31,6–33,3 ms ani dawne stałe 13–19,5 ms na tick.

Niezmieniona paczka I04 przeszła osobne kontrole: ukryte okno mediana/p95 16,668/16,669 ms, widoczne 16,668/16,671 ms, oba bez utraty czasu. To punkt odniesienia z tego dnia, a nie dowód przyspieszenia względem danych historycznych.

Kontrole dodatkowe:

- Limit 144 FPS z KHR_debug: 2039 klatek / 1700 ticków, mediana 6,946 ms, p95 6,954 ms, wynik 3/3 i brak utraty czasu. To pojedyncza próba, nie gwarancja takiej wydajności we wszystkich warunkach.
- VSync on na monitorze około 144 Hz: 2042 klatki / 1700 ticków, mediana 6,945 ms, p95 7,083 ms, wynik 3/3 i brak utraty czasu.
- Kolekcja liczników off: 900 klatek / 1801 ticków, mediana/p95 16,668/16,674 ms, wynik 3/3 i brak utraty czasu. Brak `frame-timings.csv` jest zamierzony i analizator go jawnie zgłasza; to jedna kontrola narzutu, nie trzy powtórzenia pozwalające wyznaczyć przyspieszenie.

W kontrolnym logu debug było 0 komunikatów `id=131186` odebranych w fazie runtime i 1 podczas końcowej walidacji: przeniesienie `mission state` VIDEO→HOST. Log miał trzy wpisy i zero odrzuconych. Opóźnione dostarczanie komunikatów pozostaje ograniczeniem, ale wynik nie potwierdza tezy o migracji jako ustalonej przyczynie spowolnienia zwykłych klatek.

## Weryfikacja i dalsza decyzja

Pełny CTest: **21/21 PASS**, 197,34 s, w tym fizyka, sprzężenie, sąsiedztwo, odczyty, trasy 30/60/144 FPS, oba laboratoria i OpenGL 3.3. Rozszerzony `gpu.instrumentation` sprawdza elapsed/timestamp/off, tick/render/grupy, przełączenie przy oczekujących wynikach, wyłączenie oraz reset. Po końcowym uzupełnieniu metadanych i warunku długości smoke powtórzono właściwe regresje na końcowym buildzie: **4/4 PASS** (`game.clock`, `gpu.instrumentation`, `gpu.mission_ui`, `app.opengl33_smoke`, 25,12 s). Główne 18 prób i dodatkowe kontrole również wykonano na tym końcowym buildzie.

Przenośna paczka przeszła własny `--gpu-profile-check` z kodem 0; wymuszony GL 3.3 dla nowego profilera zwrócił oczekiwane 77 (pominięcie GPU, bez fałszywego PASS). EXE i wszystkie assets porównano bajtowo z końcowym buildem/źródłami. [QA](gpu-i04-5/QA.json).

Nie przeniesiono jeszcze buforów stanu na inny model alokacji, nie dodano masek faz, `sortedHot` ani lokalnego `Dmax`. Dalszy krok to **I04.5-C**: osobne A/B kosztu pracy cieczy/ziaren i projekcji terenu, przy zachowaniu pełnych kontaktów i kompensacji. I04.5-B pozostaje eksperymentem pamięciowym, jeśli dalsze dane pokażą jego potrzebę. D/E są nadal warunkowe. Cel 1,5 ms i historyczna przyczyna niestabilności pozostają jawnie otwarte.

## Odtwarzanie i dane

```powershell
.\StrefaZero.exe --mission-smoke-test --gpu-counters --gpu-timer-mode elapsed --profile .\profile-elapsed
.\StrefaZero.exe --gpu-stabilization-profile --gpu-counters --gpu-timer-mode timestamp --profile .\profile-throughput
.\scripts\profile-stabilization.ps1 -Executable '<gra>\StrefaZero.exe' -Output '<nowy katalog>' -Repetitions 3
python .\scripts\analyze-stabilization.py --input '<wyniki>'
```

Surowe wyniki i protokół: [gpu-i04-5](gpu-i04-5/), [analiza 18 przebiegów](gpu-i04-5/measurements/analysis.csv), [statusy i ograniczenia](gpu-i04-5/measurements/analysis.json), [regresje CTest](gpu-i04-5/ctest.log), [kontrola debug](gpu-i04-5/controls/high-fps-debug/window-elapsed-1/gl-debug.txt). `frame-timings-info.txt`, `phase-timeline.csv`, `run.json` i `environment.json` opisują semantykę oraz warunki każdego pomiaru.
