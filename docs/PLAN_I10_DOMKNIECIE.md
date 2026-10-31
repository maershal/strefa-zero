# I10 — plan domknięcia termiki i gazów GPU

Data: **2026-10-31**. Status: **plan, bez wdrożenia E0–E4 i bez nowych wyników pomiarów**.

I10 ma już funkcjonalne podetapy A–D: entalpię i wymianę ciepła, gazy MAC, spalanie i fazy oraz wspólne narzędzia i obraz. Ten plan dotyczy pozostałego odbioru I10. Punktem odniesienia są [raport I10](I10_GPU_THERMAL.md), zachowane dane `docs/i10/` i [główny plan](PLAN_ROZWOJU_GPU.md#i10--termika-i-gazy-na-gpu-ogień-połączony-ze-światem).

## Cel i punkt wyjścia

Ograniczyć koszt włączonej termiki, zachowując działający model i jego bilanse. Odbiór wymaga spełnienia wszystkich trzech warunków: poprawności, budżetu modułu oraz czasu całej klatki w aktywnym oknie.

| Obszar | Stan udokumentowany po A–D | Cel domknięcia |
| --- | --- | --- |
| Zimna scena piasek/woda, termika włączona | Do +3,382032 ms/tick względem `off` | <0,05 ms/tick, gdy nie ma aktywnej pracy termicznej |
| Gorące sceny: drewno, olej, gaszenie | Do +9,716552 ms/tick | ≤0,70 ms/tick w każdej scenie |
| Masa oraz H + energia chemiczna | Testy przechodzą bramkę <0,1% | Zachować próg przez 600 ticków i dłuższą próbę 2400 ticków |
| Pełna klatka okna | Brak odbioru; timestamp GPU pomija prezentację | p95 ≤16,67 ms przy 120 Hz symulacji i 60 FPS, bez utraty czasu symulacji |

Limity czasu modułu dotyczą przyrostu median segmentów ABBA według dotychczasowego protokołu. P95/p99 są raportowane osobno. Zysk wymagany do tych limitów jest duży; plan nie zakłada, że samo pomijanie pustej pracy wystarczy dla sceny gaszenia.

### Obserwacje ze źródeł

- `src/gpu/thermal_system.cpp`, `ThermalSystem::tick`: A/C uruchamiają przebiegi po całej siatce lub puli 32768 ID. B wykonuje m.in. 32 iteracje ciśnienia po 320 × 192 komórkach.
- `src/gpu/world.cpp`, `ParticleWorld::step`: blok termiki bezwarunkowo wykonuje `rebuild_active()`, `condense()`, ponownie `rebuild_active()` i przebudowę obrazu terenu. Ustawia też obie kategorie fizyczne na aktywne.
- `ThermalSystem::condense` pomija pracę przy wyłączonych reakcjach, ale otaczające przebudowy w `world.cpp` nadal są zlecane.
- Zawężenie barier i specjalizacja programów zostały już sprawdzone. Raport nie wykazał zysku wystarczającego do odbioru. Priorytetem jest ograniczenie faktycznie wykonywanej pracy.

To przesłanki do pomiarów E0, a nie ustalone udziały kosztu poszczególnych operacji.

## Kolejność prac

`E0: baza i rozbicie kosztu → E1: integracja puli → E2: aktywność A/C → E3: transport B → E4: odbiór`

Każdy etap ma osobne porównanie i kontrolę poprawności. Nie kumulować kilku niezweryfikowanych zmian przed pierwszym pomiarem.

### E0 — utrwalić bazę i wskazać dominujący koszt

- [ ] Zachować kopię roboczych źródeł I10, manifest i hashe EXE/shaderów. Baza obejmuje istniejące zmiany niezacommitowane; sam `git diff HEAD` nie odtwarza tego stanu ani plików nieśledzonych.
- [ ] W jawnym trybie profilu rozdzielić A, B, C, konsumenta zdarzeń, obie przebudowy list, kondensację, aktualizację terenu, telemetrię i render D. Zewnętrzny pomiar całego ticka obejmuje wszystkie te koszty.
- [ ] Zapisać liczbę dispatchy, obsłużonych cząstek/komórek, faktycznych narodzin/usunięć/zmian rodzaju, zmian terenu i oczekujących skroplin. Rozdzielić koszt GPU, submit CPU i backpressure.
- [ ] Odtworzyć cztery historyczne sceny i ich etapy `off / A / A+B / A+B+C`. Dodać mały lokalny ogień, świat w równowadze oraz scenę z pracą na całej siatce.
- [ ] Zmierzyć narzut sond; zwykła gra zachowuje asynchroniczne odczyty i brak timerów każdego przebiegu.

Pliki: `src/gpu/thermal_benchmark.inl`, `src/gpu/thermal_system.cpp`, `src/gpu/world.cpp`, `scripts/profile-thermal.ps1`, `scripts/analyze-thermal.py`.

**Wyjście:** tabela kosztów z surowymi próbkami, stanami puli i identyfikacją artefaktu. Wynik ustala kolejność szczegółowych eksperymentów E1–E3; czasy z różnych przedziałów nie są sumowane jako jedna próbka.

### E1 — wykonywać integrację fizycznej puli tylko po zmianie

- [ ] Wprowadzić znaczniki GPU osobno dla zmiany członkostwa puli, rodzaju/masy nośnika, terenu i dostępnego kondensatu. Sama zmiana H nie musi wymagać przebudowy członkostwa.
- [ ] Powiązać przebudowy list, wolne sloty, kondensację i odświeżanie terenu z ich rzeczywistymi zależnościami. Wykorzystać istniejące komendy pośrednie/warunkowe, bez odczytywania flag na CPU co tick.
- [ ] Najpierw pominąć pracę przy braku zmian. Scalenie dwóch przebudów dopuścić dopiero po dowodzie, że kondensacja otrzymuje aktualną listę wolnych ID; druga przebudowa może nadal być potrzebna po narodzinach.
- [ ] Zachować spójność obu fizycznych banków, kategorię ziaren/cieczy, generację i unieważnianie indeksu sąsiadów. Nie zastępować bezwarunkowego włączenia kategorii zgadywaniem ich zawartości.
- [ ] Sprawdzić wrzenie, zmianę fazy, pełną pulę bez wolnych ID, ponowne użycie ID, rozbicie terenu i wypalenie drewnianej przeszkody. Masa kondensatu czeka na miejsce, gdy pula jest pełna.

Pliki: `src/gpu/world.cpp`, `src/gpu/thermal_system.*`, `assets/shaders/thermal.comp`, `assets/shaders/thermal_condense.comp` i istniejące bufory komend.

**Wyjście:** zimny tick bez zmian nie wykonuje pełnych przebudów z powodu samego włączenia termiki. Sceny zmieniające pulę zachowują poprawność. Osobno porównać wariant A z wyłączonymi reakcjami, gdzie kondensacja już obecnie nie działa.

### E2 — wykrywać aktywność i pomijać nieaktywną pracę A/C

- [ ] Zachować gęstą ścieżkę I10 jako referencję diagnostyczną. Dodać listy aktywnych obszarów i nośników budowane na GPU; porównać kafle 8 × 8 i 16 × 16 pod względem całego kosztu, łącznie z klasyfikacją.
- [ ] Oznaczać obszary podczas źródeł, potwierdzonych zdarzeń, edycji, zmian fazy i przemieszczania nośników. Uwzględnić całe sąsiedztwo kontaktu cieplnego, granice kafli i poprzednie oraz nowe położenie cząstki.
- [ ] Rozdzielić aktywność cieplną od reakcyjnej. Obecność zimnego paliwa nie oznacza spalania; gorąca cząstka wymaga wymiany nawet bez paliwa. Promieniowanie jest osobnym powodem aktywności.
- [ ] Pierwszą ścieżkę uśpienia dopuścić wyłącznie dla stanu o wykazanej równowadze i bez możliwej wymiany. Małe gradienty, śladowe gazy i wolne reakcje pozostają aktywne. Nie usuwać H ani masy, żeby uzyskać pustą listę.
- [ ] Wybudzać przed pierwszym przebiegiem korzystającym z nowego źródła. Nie czekać na okresowy pełny skan. W razie niepewności uruchomić gęstą ścieżkę.
- [ ] Zaprojektować spójność ping-pong: pominięte komórki i ID muszą mieć aktualne dane w banku, który stanie się wejściem. Samo ograniczenie dispatcha połączone z globalną zamianą banków jest niewystarczające. Koszt niezbędnej synchronizacji banków wliczyć do pomiaru.
- [ ] Sprawdzić mały gradient przez 2400 ticków, ogień na granicy kafli, gorącą cząstkę wchodzącą w zimny obszar, odległy impuls ciepła i powtórne zapalenie po wygaszeniu.

Pliki: `src/gpu/thermal_system.*`, `src/gpu/thermal.hpp`, `assets/shaders/thermal.comp`, `assets/shaders/thermal_common.glsl`, testy termiki i sceny walidacyjne.

**Wyjście:** brak utraconych wymian lub opóźnionych źródeł względem gęstej referencji; zmierzony koszt list i wybudzania. Bufory/wiązania pozostają zgodne z kontraktem projektu GL 4.3 i SSBO 0–7.

### E3 — ograniczyć koszt gazu z zachowaniem zależności ciśnienia

- [ ] Najpierw dodać bezpieczne pominięcie transportu w całym świecie, gdy stan gazu ma wykazaną równowagę: brak ruchu, wyporności, gradientów temperatury/składu i nowych źródeł. Sam brak ognia nie wystarcza.
- [ ] Dla aktywnego gazu oddzielić lokalne zasięgi adwekcji/dyfuzji od zasięgu iteracyjnej projekcji ciśnienia. Granica gorącego kafla nie może stać się sztuczną ścianą.
- [ ] Zacząć od pełnej otwartej składowej połączonej z obszarem aktywnym, a przy niepewności od całej siatki. W otwartym świecie taka składowa może obejmować całą domenę; zmierzyć również ten przypadek.
- [ ] Lokalny obszar projekcji dopuścić dopiero po określeniu warunków brzegowych, zasięgu wszystkich 32 iteracji i przejściu kontroli dywergencji oraz pól względem gęstej referencji. Jednokomórkowe halo nie stanowi takiego dowodu.
- [ ] Zachować wspólny strumień krawędzi, bilans H i każdego składnika, zerowy przepływ przez zamkniętą krawędź oraz gaz uwięziony po edycji. Aktualizacja topologii musi poprzedzać transport.
- [ ] Zmierzyć warianty pamięci lub łączenia przebiegów tylko dla dominującego kosztu wskazanego przez E0. Zachować 320 × 192 i 32 iteracje; odrzucona redukcja do 160 × 96 nie jest skrótem do odbioru.
- [ ] Sprawdzić dym przy temperaturze otoczenia, sam gradient tlenu, dwie odległe kieszenie gazu, cienką ścianę, wąski kanał i otwarcie drzwi podczas pożaru.

**Wyjście:** przejście testów topologii, transportu i dywergencji oraz pomiar sceny lokalnej i pełnej domeny. Jeżeli projekcja nadal dominuje, etap kończy się jawnym brakiem odbioru i wskazaniem kosztu wymagającego dalszego projektu.

### E4 — pełny odbiór i odtwarzalne wydanie

- [ ] Rozszerzyć istniejące `gpu.thermal`, `gpu.thermal_edges`, `gpu.thermal_ui` i testy sesji o uśpienie/wybudzenie, pracę na granicach obszarów oraz warunkową integrację. Testy mają wykrywać brak transportu, utratę H/masy i przestarzałe banki.
- [ ] Porównać gęstą referencję i kandydata na tych samych stanach wejściowych. Oprócz globalnych bilansów sprawdzić przepływy, temperatury, paliwo/tlen, fazy i dywergencję. Progi porównań lokalnych ustalić i zapisać przed oceną kandydata; sam bilans nie wykrywa zatrzymanej symulacji.
- [ ] Zachować <0,1% błędu masy i H + energii chemicznej przez 600 ticków; wykonać także 2400 ticków i raport dryfu. Źródła/ujścia, promieniowanie i energia chemiczna pozostają jawne.
- [ ] Sprawdzić 30/60/144 FPS, pauzę, pojedynczy krok, utratę fokusu, krótkie polecenie przy backpressure, reset i deduplikację zdarzeń. Replay v1/v2 zachowuje stary model; v3 musi mieć jednoznaczną rewizję i odtwarzać nowe ustawienia, jeżeli zmienią wynik. Zmiany formatu wymagają jawnej wersji.
- [ ] Powtórzyć pełny CTest Windows na końcowym artefakcie i zestaw CPU GCC. Pominięte testy GPU nie są wynikiem PASS.
- [ ] Wykonać dwie niezależne pełne macierze: 4 sceny × 4 etapy × 3 bloki ABBA, 120 ticków rozgrzewki i 240 próbek na segment, czyli 192 segmenty / 46 080 próbek na macierz. Każda kwalifikowana mediana pełnego wariantu spełnia limit swojej sceny; nie uśredniać przekroczeń między scenami/blokami.
- [ ] Zmierzyć zimny **włączony** moduł, gęstą referencję i kandydata. Raportować A/B/C/D, integrację puli, narzut list, med./p95/p99, CPU submit, pamięć i całe pary ticków z renderem. Odróżnić zmianę trajektorii/liczby cząstek od przyspieszenia tego samego stanu.
- [ ] Dodać próby aktywnego okna misji i laboratorium z docelowymi `legacy/basic/low`, obrazem 960 × 576 i termiką włączoną. Zapisać start–start klatek z prezentacją, wykonane ticki, utratę czasu, widoczność i fokus. Utrata fokusu unieważnia segment odbioru okna; nie wycinać jej po cichu z danych. Historyczny `-Suite window` jest smoke testem, a nie gotowym protokołem wydajności.
- [ ] Zachować CSV/JSON, kontrole kompletności analizatora, obrazy w tych samych tickach, logi GL, hashe i manifest. Sprawdzić skopiowaną paczkę Windows oraz fallback CPU/GL 3.3.

**Wyjście:** osobne decyzje PASS/FAIL dla poprawności, kosztu modułu i całej klatki. Dopiero ich łączne spełnienie pozwala oznaczyć I10 jako odebrane. Zmianę domyślnego `off` ocenić i udokumentować osobno po odbiorze.

## Zasady decyzji i zakres

Obecne ustawienia `legacy/basic/low` i termika `off` obowiązują podczas prac. Każda optymalizacja ma zachowaną referencję oraz wynik porównania. Brak zysku lub regresja poprawności oznacza odrzucenie wariantu; nie podwyższamy limitów i nie zmniejszamy liczby iteracji, żeby zmienić wynik odbioru.

I04.5 pozostaje odłożone. I10-E0–E4 nie rozszerza chemii o kwas, węgiel, proch, termit ani nitroglicerynę; nie dodaje akustyki, porowatości ani pełnego mechanicznego sprzężenia gaz–ciecz. Oświetlenie/HDR/bloom pozostają I11. Nie są zależnościami tego planu.

Wyniki nowych etapów zapisywać pod `docs/i10-e/` z osobną identyfikacją prób. Istniejące raporty i paczka I10 pozostają historyczną bazą. Po każdym etapie uzupełnić ten plan i rejestr postępu; planowane pozycje nie są wynikami wykonanych prac.
