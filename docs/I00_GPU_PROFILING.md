# I00 — pomiary GPU i komunikacji z CPU

Data: 2026-10-28. Etap planu: [I00](PLAN_ROZWOJU_GPU.md#i00--odtworzenie-pomiarów-i-wskazanie-rzeczywistych-kosztów).

## Zakres

Dodano opcjonalne liczniki i próbki czasu do istniejącego silnika, test kompletności pomiarów oraz powtarzalny zestaw eksperymentów. Domyślne ustawienia zachowują dotychczasowe podkroki, liczbę iteracji, częstotliwość HUD i sprzężenie postaci. Eksperymenty wyłączające sprzężenie nie są wariantem fizyki przeznaczonym do wydania.

`--gpu-counters` zbiera dla każdego kroku: dispatchy, bariery, przebudowy siatki, żądania przebudowy aktywnych ID, redukcje kapsuły, wiązania, wybory programów, odpytywanie timerów/fence i liczbę transferowanych bajtów. Pomiar CPU rozdziela `poll`, wysyłanie zdarzeń wybuchów, przygotowanie misji, wysłanie solvera, diagnostykę i staging. Wewnątrz `poll` osobno mierzone są timery, sprawdzenie fence i gotowy odczyt.

Liczniki nie czytają nowego stanu GPU. `api_calls` obejmuje funkcje ładowane przez warstwę `gpu::Api`; bezpośrednie wywołania GL 1.x nie są pełnym śladem wszystkich poleceń. `render_state_queries` osobno liczy 18 dotychczasowych odpytań stanu na render. Przebudowa aktywnych ID jest liczona jako zlecenie także wtedy, gdy shader warunkowo nie wykonuje zmian. CPU nie zna liczby rzeczywiście pracujących grup indirect bez dodatkowego odczytu.

`--gpu-group-timers` dodaje asynchroniczne timestampy dla przygotowania misji, dwóch podkroków, diagnostyki i staging. Nie zagnieżdża zapytań GL_TIME_ELAPSED. Historia próbek ma limit 10 000; w zwykłej grze nowe liczniki i historia faz są domyślnie wyłączone.

## Uruchomienie

W PowerShell, z katalogu źródeł:

```powershell
# Krótki test poprawności liczników i wariantów.
& '<katalog gry>\StrefaZero.exe' --gpu-profile-check --profile '<wyniki>\checks'

# Pełna macierz przepustowości: 3 powtórzenia, 120 ticków rozgrzewki i 240 pomiarowych.
& '<katalog gry>\StrefaZero.exe' --gpu-profile --profile '<wyniki>\throughput'

# Macierz przepustowości i normalne okno gry: 3 powtórzenia każdego wariantu.
.\scripts\profile-gpu.ps1 -Executable '<katalog gry>\StrefaZero.exe' -Output '<wyniki>'

# Osobny odczyt ostrzeżeń sterownika; nie używać kontekstu debug do pomiaru Release.
& '<katalog gry>\StrefaZero.exe' --gpu-profile-check --gl-debug --profile '<wyniki>\debug'
```

Skrypt przyjmuje `-SkipWindows`, `-SkipThroughput`, `-WindowRepetitions 1..5` i `-ContinueOnFailure`. Ostatnia opcja zachowuje nieudane pomiary i kontynuuje serię; skrypt nadal zgłasza błąd na końcu i zapisuje `failed-runs.txt`. Nie instaluje narzędzi. Przebiegi są sekwencyjne, a kolejność wariantów odwracana w co drugim powtórzeniu. GPU wymaga GL 4.3; kod 77 to pominięcie, nie pozytywny pomiar.

Testy standardowo zachowują dotychczasowe ukryte okno. Opcja `--show-test-window` pokazuje je do osobnego sprawdzenia wpływu prezentacji na ekranie. Oba warunki trzeba oznaczyć w porównaniach; samo pokazanie okna nie zmienia ustawień fizyki.

| Opcja / wariant | Znaczenie |
| --- | --- |
| baseline | Liczniki i fazy CPU włączone; fizyka, timery i odczyty bez zmian. |
| no_readback / `--gpu-readback-period 0` | Wyłączony staging HUD/liczników; fizyka pozostaje na GPU. |
| readback_4 / `--gpu-readback-period 4` | HUD próbkowany co 4 ticki. Dostępne też okresy 1 i 2. |
| no_coupling / `--gpu-no-coupling` | Wyłączone oddziaływanie kapsuła–cząstki i jego redukcje; ruch postaci względem terenu nadal działa. |
| no_diagnostics / `--gpu-no-diagnostics` | Wyłączone timery GPU i okresowe sprawdzanie ściśnięcia; liczniki CPU nadal dostępne. |
| no_timers | W macierzy przepustowości wyłącza tylko timery GPU, pozostawiając diagnostykę ściśnięcia. |
| no_counters | Domyślna ścieżka aplikacji, bez nowych próbek faz CPU i liczników; dawne zbiorcze timery nadal działają. |
| group_timers / `--gpu-group-timers` | Baseline z dodatkowymi timestampami grup. |

Kontekst debug i ograniczony bufor KHR_debug są automatyczne w Debug, opcjonalne przez `--gl-debug` w Release. Callback tylko kopiuje ograniczoną liczbę komunikatów do pamięci; zapis `gl-debug.txt` następuje przy zamykaniu. Bufory i programy mają etykiety, a tick grupę debug. Obsługa KHR_debug jest sprawdzana również dla kontekstu 3.3.

## Format i granice pomiarów

- `environment.txt`: GPU, sterownik/wersja GL i GLSL, flagi kontekstu, tryb kompilacji, tick i rozdzielczość renderowania 960 × 576.
- `summary.csv`: scena, wariant, powtórzenie, rodzaj pomiaru, liczba próbek, mediana/p95/p99/maksimum. Jednopróbkowe `batch_mean_step_wall_ms` jest średnim czasem ticka z całej partii, wraz z opróżnianiem kolejki, nie percentylem rozkładu ticków.
- `steps.csv`: surowe fazy CPU i delty liczników na tick. Odczyt w `poll` może dotyczyć wcześniejszego ticka, więc nie przypisujemy go kosztowi fizyki tego samego ticka na GPU.
- `gpu-groups.csv`: wyniki grup z tickiem i generacją; dane starej generacji są odrzucane. Dodatkowe timestampy mają własny koszt — porównanie z baseline jest częścią eksperymentu.
- `mission-frames.csv` i `mission-ui.txt`: dotychczasowa pętla okna 60 FPS, pełna trasa i obraz po 900 klatkach. `cpu_submit_ms` w wierszu klatki to ostatni krok, nie suma kroków tej klatki.
- `window-steps.csv`, `window-gpu-groups.csv`, `window-counters.txt`: nowe próbki wszystkich kroków i sumy wywołań w oknie. Sumy obejmują również inicjalizację/końcową walidację, delty w CSV kroków obejmują wyłącznie `step()`.
- `readback_age_ticks`: wiek ostatniego zaakceptowanego stanu HUD w chwili końca kroku, a nie czas samego transferu. Przy wyłączonym odczycie rośnie od ostatniego snapshotu.

Macierz przepustowości jest nieograniczona FPS i celowo zachowuje opróżnienie kolejki co 16 ticków poza czasem `step()`, aby porównać ją ze starym benchmarkiem. Ostateczna wersja tworzy **świeży świat i bufory na każdy wariant/powtórzenie**. Wcześniejsza seria współdzieląca świat po walidacji została zachowana jako pomiar wstępny, bez traktowania jej jako czystej bazy. Powodem jest zaobserwowana w KHR_debug migracja buforów po pełnym odczycie walidacyjnym.

Pomiar normalnego okna jest osobny. Synchroniczne odczyty walidacyjne i końcowe sprawdzenie wyniku misji są poza pomiarem klatek. Warianty bez timerów nie mają wyników czasu GPU; porównujemy ich koszt CPU i czas całej partii. Nowe kolumny `ui_poll_ms`, `ui_timer_poll_ms`, `ui_fence_poll_ms`, `ui_read_ms` mierzą również zbieranie wyników poza `step()`, a `frame_step_submit_ms` sumuje wszystkie kroki w klatce. Pierwotne kolumny CSV pozostawiono na początku w tej samej kolejności.

Mała scena benchmarku to nieruchoma misja z 3096 początkowymi cząstkami; normalne okno wykonuje trasę ze strzelaniem i destrukcją. Sceny 16k/32k to ziarna z kapsułą, zgodnie z dawnym testem przeciążenia, a nie gęstsza kopia mieszanej misji. Nie interpretujemy ich jako izolowanego pomiaru skalowania liczby cząstek w identycznej geometrii.

## Weryfikacja i wyniki

Test `gpu.instrumentation` sprawdza 204 zwykłe / 208 diagnostycznych dispatchy, 42 / 43 przebudowy siatki, 42 redukcje, rozmiar staging 304 B, brak transferów w wariancie no_readback, kompletność timestampów, skończony stan i zachowanie 3096 cząstek. Sprawdza też odrzucanie starego HUD po resecie. Przy wyłączonym sprzężeniu oczekuje 160 / 164 dispatchy i zera redukcji postaci.

### Testy funkcjonalne

- Release z MSVC `/W4 /WX` zbudowany poprawnie.
- Pełny zestaw: **16/17 przeszło**, test `gpu.mission_ui` wykazał utratę czasu po klatce 220,616 ms. Osobna powtórka tego testu przeszła bez utraty czasu, ale dalsze próby ponownie wykazały duże spowolnienie. Nie uznajemy tego testu za niezawodnie zaliczony.
- Testy fizyki CPU, ziaren, cieczy, przepływu, całej misji 30/60/144 FPS, narzędzi i kompatybilności GL 3.3 przeszły w pełnym przebiegu. Zmiany nie modyfikują domyślnych parametrów modelu.
- Ostateczny `--gpu-profile-check --gl-debug`: **8/8 wariantów pomiaru przeszło**, bez utraty cząstek i błędów GL.
- Wymuszony GL 3.3 z nowym testem GPU: poprawny kod **77**, zapisany powód pominięcia.
- Callback KHR_debug działa. Ostrzeżenie sterownika potwierdziło migrację staging z VIDEO do DMA CACHED; ostrzeżenia o migracji cząstek występowały również przy celowych pełnych odczytach walidacyjnych. Sam komunikat nie dowodzi kosztu każdej kopii ani miejsca wszystkich późniejszych alokacji.

### Wyniki porównawcze i ograniczenie bazy

Sprzęt: GTX 1650, OpenGL 4.3 / NVIDIA 610.47, Release, obraz świata 960 × 576. Użytkownik korzystał normalnie z komputera. Temperatura w zapisanej serii wynosiła około 63–66°C, taktowanie grafiki często 1395 MHz; pojedyncza taka próbka nie dowodzi stabilności całego systemu.

W trakcie pomiarów nastąpiła duża zmiana osiągów. Pierwsze dwa pomiary wstępne dawały medianę GPU około 3,75–3,83 ms, a trzeci około 20 ms także bez nowych liczników. Późniejsza seria na świeżych buforach była powtarzalna w wolniejszym reżimie około 17 ms. **Nie mieszamy obu reżimów w jeden wynik i nie przypisujemy tej różnicy instrumentacji.**

Poniżej trzy oddzielne powtórzenia ostatecznej macierzy na świeżych buforach. To pomiary w wolniejszym reżimie, nie potwierdzenie osiągnięcia docelowych budżetów.

| Wariant | Mediana GPU/tick, powtórzenia 1 / 2 / 3 [ms] | p95 CPU submit, powtórzenia 1 / 2 / 3 [ms] |
| --- | --- | --- |
| baseline | 17,052 / 17,115 / 17,482 | 3,893 / 4,615 / 4,751 |
| no_readback | 17,091 / 17,054 / 17,358 | 0,115 / 0,139 / 0,176 |
| readback_4 | 17,334 / 17,398 / 17,353 | 12,089 / 13,539 / 9,510 |
| no_coupling | 15,566 / 15,667 / 15,542 | 3,624 / 3,213 / 3,493 |
| no_counters | 17,521 / 17,371 / 17,209 | 4,982 / 4,505 / 4,371 |

Wnioski do następnej iteracji:

1. **Ścieżka odczytu powoduje mierzalny problem z opóźnieniami CPU.** Wyłączenie jej zmniejsza p95 z kilku ms do około 0,1–0,18 ms, przy praktycznie niezmienionym koszcie fizyki GPU. To przemawia za I01, ale nie oznacza, że usunięcie HUD jest rozwiązaniem produkcyjnym.
2. **Rzadsze wywołanie staging nie wystarcza.** Single-buffer/fence już scala żądania przy zapełnionej kolejce. W samych krokach zapisano podobną liczbę kopii dla okresu 1 i 4; sprawdzanie fence może nadal kosztować kilka–kilkanaście ms mimo timeoutu zero. Nie mylić semantyki „bez oczekiwania na sygnał” z zerowym kosztem wywołania sterownika. CSV kroków nie obejmuje dodatkowych wywołań `poll()` podczas zewnętrznego opróżniania kolejki.
3. **Nie cały koszt przypada na gotowy `glGetBufferSubData`.** W ostatecznej bazie p95 sprawdzania fence wynosiło około 3,48 ms dla baseline i 11,97 ms dla readback_4; gotowy odczyt zwykle był krótki, ale miał także pojedyncze długie skoki. Raport wskazuje fazę problemu, a nie nieudowodnioną pojedynczą przyczynę w sterowniku.
4. **Sama kapsuła nie wyjaśnia większości czasu GPU.** Wyłączenie sprzężenia usuwa 44 dispatchy i daje około 9% różnicy median w wolniejszym reżimie. Większym celem pozostaje harmonogram solvera i sąsiedztwo I02–I04.
5. **W pomiarze grup dominują podkroki.** Dla wolniejszego reżimu mediany grup to około 0,206 ms przygotowania, 8,576 ms pierwszego podkroku, 8,410 ms drugiego oraz 0,0024 ms staging. To grupy z dodatkową instrumentacją, więc nie sumujemy ich median jako dokładnego czasu baseline.

### Kontrola starej wersji, widoczności okna i monitora

Przed zmianą warunków zachowana, niezmieniona wersja 0.7.0 przeszła test okna z medianą 16,669 ms i bez utraty czasu. Ta sama kopia później osiągnęła medianę 95,534 ms i zgubiła 54,042 s symulacji w teście 900 klatek. Nowa wersja również zwolniła z wyłączonymi licznikami. To wyklucza tezę, że sam nowy kod liczników wyjaśnia całe spowolnienie; **przyczyna zmiany warunków wykonania pozostaje niewyjaśniona**.

Osobna próba nowej wersji z widocznym oknem i licznikami nadal była wolna: mediana klatki 89,093 ms, p95 93,111 ms, utracony czas 45,967 s. Pokazanie okna samo nie przywróciło wcześniejszych osiągów.

Po informacji użytkownika o drugim monitorze sprawdzono konfigurację: system widział już jeden aktywny ekran wewnętrzny. Powtórzony test starej wersji nadal był wolny: mediana 92,254 ms, p95 98,080 ms, utracony czas 49,383 s. **Nie ma zatem potwierdzenia, że odłączenie drugiego monitora rozwiązuje problem.** Nowa wersja z licznikami osiągnęła medianę 92,431 ms i p95 99,940 ms; również zgubiła czas. Surowa telemetria i oba raporty znajdują się w katalogu danych.

Długą serię okien przerwano po wykryciu spowolnienia. Zachowano nieudane wyniki, timeout i powód przerwania. Nie obniżano kryterium poprawności zegara ani nie usuwano nieudanych próbek, żeby uzyskać pozytywny raport.

### Stan odbioru

**Instrumentacja I00 jest wdrożona i sprawdzona funkcjonalnie. Baza wydajności okna pozostaje nieodebrana.** Nie deklarujemy 60 FPS ani osiągnięcia celu 1,5 ms. I01 ma już konkretny punkt odniesienia dla ścieżki odczytów; porównania FPS wymagają ponownego sprawdzenia warunków wykonania i kontroli starej wersji.

Surowe dane: [gpu-i00](gpu-i00/). Są tam raporty testów, pierwsza seria, ostateczna macierz na świeżych buforach, kontrole starszej wersji, próba widocznego okna i konfiguracja z jednym monitorem. Pliki BMP pominięto w archiwum pomiarów, żeby nie powielać obrazów bez zmian wizualnych.
