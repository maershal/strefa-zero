# I01 — pierścień odczytów GPU–CPU

Data: 2026-10-28. Poprzedni etap: [pomiary I00](I00_GPU_PROFILING.md). Plan: [PLAN_ROZWOJU_GPU.md](../plans/PLAN_ROZWOJU_GPU.md).

## Zmiana

`ReadbackRing` zastępuje pojedynczy bufor staging. Ma trzy sloty z osobnym buforem, fence, generacją świata, rewizją edycji i tickiem próbki. Slot pozostaje zajęty aż do odczytania lub odrzucenia gotowego wyniku. Pełna kolejka nie czeka ani nie nadpisuje trwającej kopii: pomija nową próbkę i zachowuje żądanie odświeżenia najnowszego stanu.

Domyślnie HUD jest próbkowany co dwa ticki. Wyniki zbierane są w istniejącym `poll()` na początku klatki UI. Produkcyjny `step()` odpytuje wyłącznie timery GPU — fizyka nie potrzebuje kopii HUD, bo korzysta z własnego stanu na GPU. Sposób rozwiązania kontaktów, liczba iteracji i krok symulacji pozostają bez zmian.

Odczyt jest FIFO: po pierwszym niegotowym fence kończymy sprawdzanie. Nie ma pętli oczekującej, dodatnich timeoutów ani `glFinish()` w produkcyjnym transporcie. Osiągnięcie niskiego kosztu konkretnego wywołania sterownika nadal oceniamy pomiarem; semantyka timeoutu zero sama tego nie gwarantuje.

## Ścieżki wykonania

| Tryb | Sloty | Pamięć / odczyt | Zastosowanie |
| --- | ---: | --- | --- |
| `auto` | 3 | Trwałe mapowanie, gdy dostępne; inaczej staging | Domyślny runtime |
| `persistent` | 3 | BufferStorage READ / PERSISTENT / COHERENT, wskazówka CLIENT_STORAGE; memcpy gotowego pakietu | Preferowana ścieżka |
| `staging` | 3 | BufferData STREAM_READ; GetBufferSubData gotowego pakietu | GL 4.3 bez buffer_storage, także wymuszenie do testów |
| `legacy` | 1 | BufferData DYNAMIC_COPY; odpytywanie również w tickach | Kontrola A/B zachowująca poprzedni rytm |
| `stream` | 1 | Jak legacy, ale STREAM_READ | Kontrola wpływu samego hintu |

Obsługę BufferStorage sprawdzamy przez wersję GL / rozszerzenie i adres funkcji. Sama obecność kontekstu 4.3 nie przesądza o braku rozszerzenia. Jawnie wymuszone `persistent` na niewspieranym urządzeniu zgłasza błąd; `auto` wybiera dostępny fallback.

Pakiet to **304 B**: 32 B liczników i 272 B `MissionSnapshot`; trzy sloty mają łącznie 912 B danych, poza narzutem obiektów sterownika. Przed kopią jest bariera BUFFER_UPDATE dla zapisów shaderów; po kopii bariera SSBO chroni źródło przed kolejnymi zapisami. Koherentna mapa jest czytana dopiero po sygnale fence. Podstawy kontraktu: [ARB_buffer_storage](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_buffer_storage.txt), [ARB_sync](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sync.txt).

## Reset, edycja i przeciążenie

- Reset / nowa scena zmieniają generację. Pending sloty pozostają własnością GPU do zakończenia kopii; nie są natychmiast używane ponownie. Gotowy wynik innej generacji jest odrzucany przed kopiowaniem danych do CPU.
- Pędzel zmienia rewizję edycji, dzięki czemu opóźniony licznik sprzed edycji nie nadpisuje nowszego stanu materiałów. Edycja może zażądać odczytu także przy pauzie.
- Przepełnienie zwiększa licznik `dropped`; po zwolnieniu slotu zlecana jest najnowsza próbka. To scalanie odświeżeń prezentacji, a nie pomijanie kroków fizyki.
- Snapshot misji dodatkowo sprawdza generację i niemalejący tick. Metryki pokazują pending, maksimum zajętości, submitted/completed/dropped/stale i opóźnienie próbki.
- Zmiana trybu odczytu jest operacją konfiguracji, poza mierzonym krokiem. Zwalnianie zasobów z kopiami w locie korzysta z cyklu życia obiektów GL; żaden stary wskaźnik mapy nie jest później odczytywany.

**Ten pierścień przenosi wyłącznie telemetrię i HUD.** Obecna autorytatywna rozgrywka nadal działa na GPU, a kolejka poleceń wybuchu pozostaje oddzielna. Przy migracji reguł gry do CPU w I06 potrzebny będzie osobny kanał zdarzeń z ID i potwierdzeniami; nie wolno konsumować zdarzeń krytycznych z gubiących próbki snapshotów.

## Uruchomienie i pomiary

```powershell
# Test wszystkich dostępnych backendów i bezpiecznego przepełnienia.
& '<gra>\StrefaZero.exe' --gpu-readback-check --profile '<wyniki>\tests'

# 3 powtórzenia każdego wariantu, zawsze świeży świat / bufory.
& '<gra>\StrefaZero.exe' --gpu-readback-profile --profile '<wyniki>\benchmark'

# Cała seria, w tym 3 powtórzenia testu okna legacy / auto / staging.
.\scripts\profile-readback.ps1 -Executable '<gra>\StrefaZero.exe' -Output '<wyniki>' -IncludeWindows

# Wymuszenie zgodnej z GL 4.3 ścieżki w normalnej grze.
& '<gra>\StrefaZero.exe' --mission --gpu-readback-mode staging
```

`--gpu-readback-period 0|1|2|4` pozwala badać częstotliwość; zero jest wyłącznie ablacją odczytów. `--gpu-readback-mode auto|legacy|stream|staging|persistent` wybiera transport. Skrypt pozwala pokazać testy przez `-ShowWindows`; brak opcji zachowuje standardowe ukryte okno testowe. Timeout procesu 120 s dotyczy tylko narzędzia pomiarowego, nie oczekiwania na fence w grze.

`-SkipThroughput` umożliwia wykonanie samych testów transportu i serii okien. W zwykłej grze koniec klatki zapewnia wysłanie poleceń GL; sam `poll()` nie jest poleceniem opróżniania kolejki. Harness bez okna musi zapewnić postęp kolejki przez własny flush / punkt walidacji.

Odczyty `read_mission()`, `readback()` i eksport obrazu nadal mogą synchronizować GPU, ale są granicą walidacji / eksportu, poza normalną pętlą. GL 3.3 zachowuje dotychczasowe tryby CPU; test GPU zwraca kod pominięcia 77.

### Jak interpretować wyniki

- `summary.csv`: fazy CPU, GPU, średnia czasu całej partii oraz **osobno koszt zewnętrznego poll**. W wariantach pierścienia CPU submit nie zawiera odczytu HUD; w kontrolach legacy / stream nadal go zawiera. Sam spadek tej liczby nie dowodzi spadku łącznego kosztu CPU.
- `steps.csv`: wszystkie kroki, ich fazy i transfery. `read_bytes` opisuje GL GetBufferSubData, a `mapped_read_bytes` kopiowanie z gotowej mapy — zero w pierwszej kolumnie nie oznacza braku HUD.
- `transport.csv`: resolved mode, okres, pojemność, przyrosty liczników i pending przed / po partii. Completed może być większe od submitted w danej partii, jeżeli zostały odebrane próbki rozpoczęte podczas rozgrzewki. Maksimum pending dotyczy czasu życia kolejki.
- `mission-frames.csv`: oprócz starego ostatniego CPU submit zawiera sumę wszystkich ticków w klatce oraz zewnętrzne `ui_poll_ms`. Porównanie `frame_step_submit_ms + ui_poll_ms` sprawdza, czy koszt nie został tylko przesunięty poza tick.
- Benchmark przepustowości ma rozgrzewkę 120 ticków, 240 ticków pomiarowych, trzy powtórzenia i odwróconą kolejność co drugą serię. `glFinish()` co 16 ticków jest wyłącznie operacją harnessu poza czasem ticka, identyczną dla kontrolowanych wariantów; nie występuje w produkcyjnym pierścieniu.

## Weryfikacja

Test `gpu.readback_ring` używa własnego producenta compute zapisującego 76 rozpoznawalnych słów do rzeczywistego bufora GPU. Sprawdza pełny pakiet, pełną kolejkę bez dodatkowych wywołań GL, wielokrotne i częściowe obiegi slotów, stare generacje i edycje, zmianę backendu oraz zniszczenie z kopią w locie. Osobne sceny świata sprawdzają reset HUD, przełączenie sceny i liczniki po edycji. Testuje się również wymuszone wyłączenie opcjonalnej możliwości BufferStorage, bez udawania testu na innej fizycznej karcie.

### Testy końcowe

- Release MSVC `/W4 /WX`: kompilacja poprawna.
- **11/11 wybranych regresji przeszło**: zegar i wejście, ustawienia, pełna misja GPU (w tym trasy 30/60/144 FPS), instrumentacja, nowy transport, ziarna, ciecze, zachowanie przepływu, okno misji, okno laboratorium GPU i tryb CPU/GL 3.3. To wybrany zestaw, nie deklaracja ponownego uruchomienia całych 18 testów.
- Producent GPU i konsument przeszły testy legacy, stream, staging i persistent. Wymuszona utrata opcjonalnej możliwości poprawnie wybrała staging i odrzuciła jawne persistent.
- KHR_debug nie zgłosił błędów GL w testach transportu; ostrzeżenia dotyczące pełnych odczytów walidacyjnych zachowano w danych.
- Test transportu przeszedł także z osobno zainstalowanej paczki Windows I01. Wymuszone GL 3.3 poprawnie pominęło test GPU kodem 77. Pliki i zasoby paczki sprawdzono sumami SHA-256.
- Oddzielna seria dziewięciu okien: pierwsze sześć przebiegów bez utraty czasu. W trzecim powtórzeniu wszystkie trzy backendy trafiły na wolniejszy reżim GPU i test zegara się nie powiódł. Nie zmieniono jego kryteriów ani limitu zaległości.

### Pomiar kontrolowany: kroki i zewnętrzny odczyt

GTX 1650 / NVIDIA 610.47, GL 4.3 z ARB_buffer_storage. Poniżej p95 w trzech oddzielnych powtórzeniach, po 240 ticków pomiarowych. Nie sumujemy percentyli z niezależnych rozkładów.

| Wariant | p95 CPU submit 1 / 2 / 3 [ms] | p95 zewnętrznego poll 1 / 2 / 3 [ms] |
| --- | --- | --- |
| legacy, odczyt także w ticku | 1,039 / 1,525 / 1,518 | 0,103 / 0,075 / 0,136 |
| staging, pierścień, co 2 ticki | 0,076 / 0,069 / 0,068 | 0,153 / 0,160 / 0,215 |
| persistent, pierścień, co 2 ticki | 0,093 / 0,091 / 0,083 | 0,039 / 0,033 / 0,043 |
| off, wyłącznie kontrola | 0,033 / 0,034 / 0,036 | 0,014 / 0,014 / 0,015 |

Mediana CPU submit domyślnego persistent wynosiła około **0,030 ms**. Mediany fizyki GPU wszystkich wariantów pozostały w podobnym zakresie około **3,6–3,8 ms**. Cel 1,5 ms fizyki GPU nie jest zakresem ani wynikiem I01.

Fallback staging osiąga mały koszt ticka, ale odczyt zewnętrzny jest droższy niż memcpy z trwałej mapy. Dlatego domyślnie wybieramy persistent, gdy jest rzeczywiście dostępne. Niskiego CPU submit nie przedstawiamy jako całego kosztu aplikacji.

### Kontrola pełnych klatek

Tabela zawiera wszystkie powtórzenia i oba składniki CPU z tej samej klatki: **suma czasów ticków + zewnętrzny poll**. Pominięto tylko zaplanowane pierwsze 30 klatek rozgrzewki; surowe CSV zachowuje również je. W każdym przebiegu misja osiągnęła wynik 3 i zakończenie; nie wszystkie przebiegi spełniły wymaganie braku zgubionego czasu.

| Wariant / seria | Mediana całej klatki [ms] | Mediana łącznego CPU [ms] | p95 łącznego CPU [ms] | Zgubiony czas [s] |
| --- | ---: | ---: | ---: | ---: |
| legacy / 1 | 16,669 | 2,329 | 8,349 | 0 |
| auto / 1 | 16,669 | 2,353 | 14,394 | 0 |
| staging / 1 | 16,669 | 0,124 | 0,170 | 0 |
| legacy / 2 | 16,668 | 0,235 | 1,610 | 0 |
| auto / 2 | 16,669 | 0,096 | 0,126 | 0 |
| staging / 2 | 16,668 | 0,118 | 0,170 | 0 |
| legacy / 3 | 90,908 | 80,126 | 88,450 | 35,683 |
| auto / 3 | 92,737 | 12,883 | 17,703 | 50,000 |
| staging / 3 | 93,140 | 12,191 | 15,558 | 49,483 |

Druga seria potwierdza spadek łącznego kosztu CPU, a nie wyłącznie przeniesienie pracy poza licznik kroku. Pierwsza seria była mniej stabilna i nie potwierdza poprawy każdego percentyla dla auto — zachowujemy ten wynik. Trzecia seria ponownie wykazała duże spowolnienie GPU do około 22 ms na tick we wszystkich backendach. Mniejszy koszt CPU nie przywrócił w niej płynności ani poprawnego tempa zegara.

**I01 poprawia i porządkuje transport, ale nie rozwiązuje okresowego spowolnienia całego GPU.** Baza FPS pozostaje warunkowa tak jak w I00. Nie porównujemy późnego wyniku 17–22 ms z I00 ze stabilnymi 3,7 ms z tej serii jako rzekomego przyspieszenia fizyki przez pierścień.

### Odrzucone podejścia

1. Sam STREAM_READ bez zmiany rytmu nie dał trwałej poprawy opóźnień. Hint nie jest gwarancją zachowania sterownika.
2. Pierścień z trwałym mapowaniem nadal odpytywany w każdym ticku skrócił samo czytanie pakietu, ale p95 CPU pozostawało około 1–2 ms. Nie wybrano tego wariantu.
3. GetSynciv zamiast ClientWaitSync pogorszył na tym sterowniku koszt odpytywania; wariant cofnięto. Ostateczny transport używa ClientWaitSync z flagami i timeoutem zero, na poziomie klatki.

Surowe dane i wszystkie wyjątki: [gpu-i01](../measurements/gpu-i01). `initial-tick-poll/` i `rejected-get-synciv/` są eksperymentami odrzuconymi; `final-frame-poll/` opisuje implementację końcową. `windows/` zawiera wszystkie dziewięć prób, również nieudane i dokończone po przerwaniu skryptu na legacy / 3.

## Odbiór i następny etap

I01 jest wdrożone: trzy sloty, oba backendy, bezpieczne generacje/edycje, pełna kolejka, metryki i odczyt HUD poza tickiem. Cel niskiego CPU jest potwierdzony w kontrolowanej serii oraz części pełnych prób; nie jest gwarancją przy przeciążonym lub spowolnionym GPU.

Następny zakres: **I02 — programy wyspecjalizowane dla przebiegów, mniej wiązań i FIFO timerów**. Liczba przebiegów fizyki nadal wynosi 204 / 208; koszt sąsiedztwa oraz solvera pozostaje do I02–I04.
