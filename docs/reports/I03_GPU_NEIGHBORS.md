# I03 — uporządkowane dane i kompletne sąsiedztwo

Data: 2026-10-28. Poprzedni etap: [I02](I02_GPU_EXECUTION.md). [Plan iteracji](../plans/PLAN_ROZWOJU_GPU.md).

Status: **wdrożone; kontrolowane A/B i CTest odebrane, odbiór wydajności okna otwarty**.

## Zakres i kontrakt

Zastąpiono listy `head/link` ciągłymi zakresami aktywnych ID posortowanych według jednokomórkowej siatki. GPU wykonuje counting sort: wyzerowanie liczników, histogram, lokalny prefix scan w 240 grupach po 256 wątków, scan sum bloków w jednej grupie i scatter. Przebiegi rozdzielają bariery SSBO; `barrier()` służy wyłącznie skanowi wewnątrz jednej grupy. Nie ma oczekiwania CPU na zakończenie budowy.

Sortowanie porządkuje **widok ID**, a nie numerację obiektów. Stan fizyczny nadal ma stały adres według ID. `order[index]` daje stabilne ID, `sortedIndex[id]` jego miejsce w indeksie przestrzennym. Pula wolnych ID i rezerwacja miejsc zachowują dotychczasowy kontrakt. Zakres całego wiersza zapytania jest jednym odcinkiem między prefixami pierwszej i ostatniej komórki, z przycięciem na brzegach świata. Ostatni prefix jest liczbą aktywnych ID. Każdy kandydat nadal przechodzi filtr rzeczywistego dystansu i, dla cieczy, widoczności.

Stan cząstek ma domyślnie **SoA z czterema strumieniami `vec4`**: położenie, ruch/właściwości, kontakt/historia renderowania i reszty kompensacji. Obie kopie ping-pong, renderer, spawn, gruz i walidacja używają tego samego wariantu ABI. Publiczne `Grain` nadal ma 64 B; konwersje są na granicy importu i synchronicznego odczytu testowego. Nie dodano kopii cząstek do pętli klatki. Zachowano AoS do pomiaru A/B. Nie przenoszono jeszcze całego stanu do fizycznie zagęszczonych adresów po sortowaniu: zachowanie stałych adresów ogranicza zmianę kontraktów edycji i rozgrywki.

Nie zmieniono kroku 1/120 s, dwóch podkroków, liczby iteracji, promieni ani współczynników sił. Inna kolejność dodawania składników może dawać różnice zaokrągleń; odbiór opiera się na testach zachowania, nie identyczności bitowej. Globalne redukcje reakcji postaci pozostają zakresem I04.

## Ponowne używanie indeksu i dowód kompletności

Domyślnie indeks powstaje po predykcji **raz na podkrok**, czyli dwa razy na tick. Kolejne przebiegi korzystają z anchorów zapisanych przy scatter. Każdy zapis położenia aktualizuje na GPU monotoniczne górne ograniczenie `Dmax`, obejmujące ruch, korekty PBF/PBD, teren, kapsułę i kompensację. Wartość zaokrąglamy w górę do 1/16 komórki, z zapasem numerycznym. Resetujemy ją dopiero przy kolejnej budowie indeksu.

Dla dowolnej pary znajdującej się teraz w promieniu `R`:

`|anchor(q) - current(p)| <= R + Dmax`.

Zapytanie jest więc centrowane na **bieżącym** położeniu `p` i rozszerzone o `Dmax` drugiego końca. Uwzględnia również resztę kompensacji własnego końca i mały zapas. Pozycje zmienione w poprzednim dispatchu i ich ograniczenie są widoczne po tej samej barierze SSBO. Zwiększanie ograniczenia przez inne wątki tego samego przebiegu może tylko rozszerzyć zbiór kandydatów.

To świadome doprecyzowanie planu: nie przechowujemy listy par z niezmiennym promieniem `R + skin`. Dla roboczego `skin = 1` warunek `2*Dmax <= skin` opisuje zwykły zakres ważności. **Po jego przekroczeniu korzystamy z szerszych zakresów komórek**, zamiast używać niekompletnej listy. Cała struktura zostaje przebudowana na początku następnego podkroku. Test przesuwa końce o ponad cztery komórki bez przebudowy i sprawdza wszystkie pary. Nie ma stałej czapki sąsiadów, przepełnienia listy par ani cichego pomijania kontaktów. Koszt patologicznego skupienia może być kwadratowy; jawny zakres może w skrajnym przypadku objąć wszystkie aktywne ID.

Po pędzlu i zmianie członkostwa indeks zostaje unieważniony. Automatyczna destrukcja i spawn gruzu poprzedzają budowę pierwszego indeksu bieżącego podkroku. Reset unieważnia stan i zeruje nagłówek diagnostyczny. Nie dodano cache widoczności par: dotychczasowy cache otoczenia terenu i DDA pracują na bieżących końcach oraz bieżącym terenie.

## Pomiary

GTX 1650, NVIDIA 610.47, OpenGL 4.3, Release MSVC `/W4 /WX`. Świeży świat dla wariantu, 120 ticków rozgrzewki, 240 pomiarowych, trzy powtórzenia i odwrócona kolejność w drugim. Timery grup są włączone we wszystkich wariantach. `glFinish()` i odbiór HUD co 16 ticków należą do harnessu, poza licznikiem CPU ticka. Koszty pełnej partii i zewnętrznego poll są w CSV. Audyt indeksu i rozkładu sąsiadów wykonuje się po pomiarze.

Najpierw uruchomiono niezmieniony build I02. Mediany GPU: misja **3,365 / 3,477 / 3,475 ms**, 16k **2,909 / 2,930 / 2,947 ms**, 32k **5,773 / 5,747 / 5,756 ms**. Dane: `gpu-i03/baseline/`. Następnie wykonano kontrolowane porównanie wariantów w jednym buildzie. Wariant `i02_linked_aos` zachowuje harmonogram I02, lecz korzysta ze wspólnych helperów zmienionego kodu; nie jest binarnie identycznym wydaniem I02.

Mediany GPU na tick [ms], trzy powtórzenia:

| Wariant | Misja 3096 | 16k ziaren z postacią | 32768 ziaren z postacią |
| --- | --- | --- | --- |
| linked AoS, harmonogram I02 | 3,337 / 3,435 / 3,429 | 2,918 / 2,919 / 2,945 | 5,720 / 5,758 / 5,761 |
| sorted AoS, świeży indeks przy każdym zapytaniu | 3,805 / 3,899 / 3,817 | 3,680 / 3,719 / 3,703 | 6,807 / 6,849 / 6,870 |
| reused AoS | 2,915 / 2,945 / 2,934 | 2,705 / 2,689 / 2,737 | 5,307 / 5,343 / 5,362 |
| **reused SoA, domyślny** | **2,907 / 2,888 / 2,943** | **2,550 / 2,540 / 2,560** | **4,870 / 4,898 / 4,934** |
| linked SoA | 3,374 / 3,329 / 3,332 | 2,636 / 2,634 / 2,637 | 5,165 / 5,168 / 5,185 |

Dla median z trzech powtórzeń domyślny wariant jest szybszy od `linked AoS` o około **15,2% / 12,6% / 14,9%**. Samo sortowanie bez ponownego używania jest wolniejsze, więc nie zostało ustawieniem domyślnym. SoA względem reused AoS daje małą różnicę w misji, około 6% dla 16k i 8% dla pełnej puli. Docelowe **1,5 ms GPU/tick nie zostało osiągnięte**; lokalne sprzężenie kapsuły należy do I04.

P95/p99 GPU dla domyślnego wariantu [ms]:

| Scena | p95, trzy powtórzenia | p99, trzy powtórzenia |
| --- | --- | --- |
| Misja | 2,959 / 2,931 / 3,003 | 2,988 / 2,978 / 3,039 |
| 16k | 2,632 / 2,631 / 2,650 | 2,647 / 2,660 / 2,664 |
| 32k | 4,998 / 5,039 / 5,068 | 5,042 / 5,077 / 5,096 |

P95 CPU submit dla misji: **0,086 / 0,064 / 0,079 ms**; mediana **0,013 / 0,013 / 0,012 ms**. P99 wynosi **0,175 / 0,090 / 0,109 ms**, maksimum w tych seriach 0,416 ms. Liczby nie obejmują zewnętrznego odbioru HUD. W porównywanym zwykłym ticku liczba dispatchy zmalała **204 → 130**, a w diagnostycznym **208 → 132**. Liczba budów indeksu zmalała **42/43 → 2**; zachowane są 42 redukcje reakcji postaci.

Końcowy audyt misji: mediana **27** kandydatów, p95 **39**, maksimum **45**; rzeczywistych sąsiadów w promieniu 2 odpowiednio **12 / 18 / 18**. W scenach ziaren: **25 / 35 / 35** kandydatów oraz **10 / 10 / 10** sąsiadów. Końcowe `Dmax = 0,0625` dotyczy ostatniego podkroku po rozgrzewce, nie maksymalnego przemieszczenia całej trasy. Pełny rozkład i liczniki są w CSV.

## Pamięć

Alokacje mają stałą pojemność 32768 ID. Dlatego rzeczywista pamięć jest taka sama przy 3096, 16000 i 32768 aktywnych cząstkach, także dla wszystkich ID w jednej komórce:

| Zasób | Bajty |
| --- | ---: |
| Dwie kopie czterech strumieni cząstek | 4 194 304 |
| Nagłówek, counts, prefixes, cursors i pomocniczy scan | 739 264 |
| Posortowane ID, mapa odwrotna i anchory | 786 432 |
| Wszystkie osiem SSBO świata razem | 8 018 560 |

Nowy indeks zajmuje około **1,46 MiB**, względem 0,36 MiB `head/link`. Wszystkie SSBO: około **7,65 MiB**, przyrost około **1,10 MiB**. Dwie istniejące tekstury świata dodają około 4,22 MiB. Nie obejmuje to tekstur postaci, programów, query ani prywatnych alokacji sterownika. Nie alokujemy bufora na każdą parę, więc pamięć nie rośnie kwadratowo w gęstych kontaktach. Dla pełnej komórki test sprawdza dokładnie 32768 ID; dla małych gęstych scen sprawdza każdą parę.

## Weryfikacja i odtwarzanie

- Release MSVC `/W4 /WX`: kompilacja poprawna.
- **20/20 wszystkich testów CTest przeszło**: CPU, zegar, ustawienia, poligon, okna oraz pełna fizyka GPU. W misji sprawdzono dotychczasowe trasy 30/60/144 FPS.
- Nowy test GPU sprawdza rzeczywiste wyniki zapytań GLSL względem niezależnego wyszukiwania wszystkich par na CPU i niezależnego przecięcia odcinka z prostokątami ścian. Obejmuje brzegi, narożnik, jednopikselową ścianę, granice bloków scan, zbieżne pozycje, nieciągłe ID, duże korekty bez przebudowy, kolejne projekcje, spawn/erase/reset i pełne 32768 ID w jednej komórce.
- Sprawdzono oba układy pamięci oraz indeks świeży i używany ponownie. Nie zgubiono ani nie odtworzono usuniętych ID; zakresy i mapa odwrotna zgadzają się z aktywnym stanem.
- Osobno przeszły pełna misja i instrumentacja reference oraz audyt sąsiedztwa z KHR_debug. `glGetError()` w audycie zwrócił brak błędu. KHR_debug zapisał 256 ostrzeżeń o migracji buforów po synchronicznych odczytach walidacji, z dalszymi 68 pominiętymi komunikatami z powodu limitu logu. Nie jest to dowód takiej migracji w produkcyjnej pętli; benchmark używa świeżych buforów i nie odczytuje synchronicznie indeksu w pomiarze.
- Wymuszony GL 3.3 poprawnie zwrócił kod **77**, czyli pominięcie nowego testu GPU; normalne okno CPU/GL 3.3 przeszło.

W serii trzech par **4/6 prób okna przeszło**. Pierwsze cztery próby ukończyły misję bez utraty czasu. Obie próby trzeciej pary ukończyły cele, ale utraciły czas zegara, więc harness poprawnie zwrócił błąd. Kryteriów nie zmieniano. Poniżej wszystkie wyniki; pierwsze 30 klatek pominięto tylko przy obliczaniu percentyli, surowe CSV je zachowuje. Łączny CPU to suma `frame_step_submit_ms + ui_poll_ms` z tej samej klatki.

| Wariant / próba | Mediana klatki [ms] | p95 klatki [ms] | p95 łącznego CPU [ms] | Utracony czas [s] |
| --- | ---: | ---: | ---: | ---: |
| linked / 1 | 16.669 | 16.703 | 2.801 | 0.000 |
| reused / 1 | 16.668 | 16.669 | 0.078 | 0.000 |
| linked / 2 | 16.668 | 16.669 | 0.091 | 0.000 |
| reused / 2 | 16.668 | 16.669 | 0.081 | 0.000 |
| linked / 3 | 16.669 | 82.875 | 56.612 | 17.917 |
| reused / 3 | 75.614 | 77.703 | 9.456 | 33.250 |

Przy limicie 60 FPS mediana poprawnych klatek pozostaje około 16,67 ms. P95 łącznego CPU w poprawnych próbach reused SoA wynosi około 0,078–0,081 ms. Wynik pierwszego linked obejmuje znacznie większy koszt CPU, mimo poprawnego czasu klatki. Trzecia para pokazuje powrót znanego reżimu powolnego GPU; nie uznajemy go za naprawiony. Nie zaliczamy odbioru FPS całej iteracji ani nie traktujemy kontrolowanego benchmarku jako gwarancji wydajności okna. Osobnego testu klatek bez limitu FPS w tym etapie nie wykonywano.

Dodatkowa kontrola **niezmienioną paczką Windows I02** również zwróciła błąd: mediana/p95 klatki **82,737 / 84,763 ms**, utracony czas **40,042 s**, końcowy GPU tick **19,560 ms**. To potwierdza, że podobne spowolnienie występuje także w starym wydaniu; nie ustala jego przyczyny. Dane: `gpu-i03/window-controls/original-i02/`. Dodatkowy zapis I03 z tego katalogu częściowo nakładał się z testem paczki GPU, więc nie jest używany do porównań czasu; właściwe porównanie sześciu okien powyżej wykonywano sekwencyjnie.

Paczka Windows I03 przeszła nowy audyt sąsiedztwa z własnego katalogu. Wymuszony GL 3.3 zwrócił oczekiwane 77. Porównano bajty EXE, DLL i wszystkich shaderów z buildem/źródłami, a sumy SHA-256 zapisano przy paczce i surowych danych. To walidacja poprawności paczki; nie zmienia otwartego odbioru czasu klatki.

```powershell
# Kompletny audyt zakresów i niezależne wyszukiwanie wszystkich par.
.\StrefaZero.exe --gpu-neighbor-check --profile .\neighbor-tests

# Trzy powtórzenia A/B oraz rozkłady kandydatów.
.\StrefaZero.exe --gpu-neighbor-profile --profile .\neighbor-benchmark

# Produkcyjna ścieżka oraz porównawcza ścieżka I02.
.\StrefaZero.exe --mission --gpu-neighbors reused --gpu-layout soa
.\StrefaZero.exe --mission --gpu-neighbors linked --gpu-layout aos

# Harness z trzema parami prób okna.
.\scripts\profile-neighbors.ps1 -Executable '<gra>\StrefaZero.exe' -Output '<wyniki>' -IncludeWindows
```

`--gpu-neighbors sorted` przebudowuje zakresy przed każdym zapytaniem i służy kontroli kosztu sortowania. `linked` zachowuje listy z I02. `--gpu-layout aos|soa` wybiera wspólne ABI obliczeń i renderera. Parametry przełączające całą konfigurację `--gpu-execution` należy podawać przed tymi szczegółowymi przełącznikami.

Surowe dane i logi: [gpu-i03](https://github.com/maershal/strefa-zero-measurements/tree/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i03). Kolejny zakres: **I04 — lokalne sprzężenie postaci**. Przyczyna historycznych okresowych spowolnień GPU z I00–I01 nadal nie została ustalona.
