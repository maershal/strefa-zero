# I04.5-C — ograniczenie pustej pracy solvera

Data: 2026-10-29. Baza: lokalny zakres I04.5-A/F, zachowany w osobnej paczce i [raporcie A/F](I04_5_GPU_STABILIZATION.md).

Warianty C zaimplementowano i porównano jako eksperyment. **Nie potwierdzono przyspieszenia; `phase_gating=false` i `skip_zero_projection=false` pozostają ustawieniami domyślnymi.** Poprawiono także stosowanie polityki profilowania od momentu utworzenia świata. Cel całego I04.5 <1,50 ms/tick pozostaje otwarty. Wyniki końcowego QA są poniżej.

## Maska obecności cieczy

Po włączeniu maski do istniejącego bufora sortowanego indeksu dochodzi 240 liczb uint32, czyli **960 B**, bez zwiększenia ośmiu wiązań SSBO. Domyślny wariant i linked-list nie alokują tego dodatku. Kafelki mają 16 × 16 komórek, świat 320 × 192. Zerowanie i histogram powstają w istniejących przebiegach, bez nowego dispatchu. Wartość 1 (bit 0) oznacza ciecz, wartość 2 (bit 1) ziarno.

Maska opisuje pozycje anchorów tego samego indeksu co `order[]`. Dla aktualnej pozycji p sprawdzane są wszystkie kafelki przecinające pełny konserwatywny zakres istniejącego `neighborWindow(p,2)`: promień 2 + globalny Dmax + reszta kompensacji + margines. Jeżeli q rzeczywiście leży w zasięgu p, to jego anchor mieści się w tym zakresie na mocy ograniczenia przemieszczenia. Brak bitu cieczy w całym zakresie wyklucza więc sąsiada cieczy. Odległy outlier może jedynie poszerzyć zakres i ograniczyć zysk; nie jest obcinany stałym halo 3 × 3.

Wtedy suche ziarno może pominąć skan sąsiadów PBF oraz próbki ścian przeznaczone wyłącznie dla cieczy. Nadal wykonuje zapis reakcji (także zerowej), ruch z kompensacją, wymagane projekcje i zapis ping-pong. Zwykłe kontakty ziaren, kapsuły i cieczy pozostają w dotychczasowych przebiegach. Wariant linked-list zachowuje pełną ścieżkę; maska działa tylko przy sortowanym indeksie. Spawn, usuwanie, zmiana terenu i reset odświeżają/unieważniają indeks przed następnym wykorzystaniem.

## Dlaczego odrzucono bezwarunkowy skrót correction == 0

Zerowa korekta PBF nie oznacza spełnionej kolizji terenu. Projekcja ziaren ma limit trzech iteracji Newtona po najwyżej 0,4 komórki; może skończyć wywołanie z pozostałą penetracją. Następne wywołanie nadal zmienia pozycję i sumę normalnych kontaktu.

Kontrprzykład objęty testem GPU: ściana zajmuje y ≥180, ziarno wczytano przy y=181,9, a korekta PBF wynosi zero. Pełne dwa wywołania doprowadzają je do y≈179,54 i contact_y=-6. Pominięcie projekcji na podstawie samego zera pozostawiłoby materiał wewnątrz ściany.

Alternatywa ma wyłącznie lokalną ważność pomiędzy dwoma kolejnymi wywołaniami w jednym wątku. Pierwsza projekcja może potwierdzić stan tylko po jawnym teście depth≤0 lub braku pobliskiej ściany, jeżeli końcowy clamp nie zmienił geometrii. Wyczerpanie iteracji, awaryjna projekcja AABB i wszystkie ciecze pozostają niepotwierdzone. Kolejny `move()` jest zawsze wykonywany, także dla zera; dopiero bitowa zgodność pozycji i reszty kompensacji z potwierdzonym stanem pozwala pominąć drugi no-op. Stan potwierdzenia nie jest przechowywany między dispatchami ani zmianami terenu.

Opcje porównawcze: `--gpu-phase-gating on|off` i `--gpu-zero-projection on|off`. Druga nazwa oznacza powyższą ograniczoną optymalizację, nie bezwarunkowe pominięcie zera.

## Walidacja i odtwarzanie

Nowy `--gpu-work-check` porównuje pełny stan pozycji, prędkości, kontaktów, kompensacji i identyfikatorów materiałów z pełną ścieżką dla wybranego układu danych i indeksu; domyślnie SoA/reused. Cztery testy CTest wykonują macierz AoS/SoA × sorted/reused. Każdy sprawdza trzy kombinacje optymalizacji, granice kafelków, skoki przez wiele kafelków, narożniki i cienkie ściany, reszty kompensacji, penetrację, spawn/erase, reset i pełne 32768 slotów. Niezależnie rekonstruuje maskę z anchorów i sprawdza rzeczywiste pary odległością all-pairs dla małych scen. Linked-list ma własną kontrolę fallbacku. Dodatkowy `gpu.work_mission` uruchamia pełny scenariusz misji z obiema opcjami włączonymi, obejmując sprzężenie kapsuły.

```powershell
.\StrefaZero.exe --gpu-work-check --profile .\work-check
ctest --test-dir '<build>' -R '^gpu.work_gating' --output-on-failure
.\scripts\profile-work.ps1 -Executable '<gra>\StrefaZero.exe' -Output '<nowy katalog>' -Variants reference,phase,projection,combined -Repetitions 4 -SkipWindows
python .\scripts\analyze-stabilization.py --input '<wyniki>'
```

Przezroczysty protokół pomiaru i nadzór natywnego procesu pochodzą z I04.5-A. Wariant `reference` oznacza wyłączone optymalizacje C w tym samym buildzie; nie jest binarnie identyczną dawną paczką. Pierwsza kompilacja nowych wariantów nie wchodzi do czasu fizyki.

## Porównanie wydajności

GTX 1650, sterownik NVIDIA 610.47, OpenGL 4.3, Release MSVC 19.51, `/W4 /WX`, IPO, render 960 × 576. W każdym procesie: 120 ticków rozgrzewki, 240 pomiarowych na scenę i 120 próbek renderu; timer elapsed, liczniki włączone, diagnostyka i sprzężenie aktywne. Świeży świat dla każdej sceny. `glFinish` i poll HUD co 16 ticków są poza CPU submit, lecz w czasie przepustowości. Nie utożsamiamy tego testu z płynnym oknem.

Wykonano **16/16 procesów bez błędu i timeoutu**, cztery powtórzenia każdego wariantu w rotowanym porządku Latin square. Analyzer schema 3 zwrócił 288 wierszy i zero ostrzeżeń. Tabela pokazuje medianę czterech median GPU; procent oznacza zmianę czasu względem reference. Surowe percentyle każdego procesu pozostają w CSV.

| Wariant | Misja, ms | Zmiana | 16k, ms | Zmiana | 32k, ms | Zmiana |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| reference | 11,3754 | — | 4,7658 | — | 8,7471 | — |
| phase | 11,2757 | −0,88% | 4,8166 | +1,07% | 8,8381 | +1,04% |
| projection | 11,2907 | −0,74% | 4,7651 | −0,01% | 8,7636 | +0,19% |
| combined | 11,3657 | −0,08% | 4,8133 | +1,00% | 8,8692 | +1,40% |

Zakres median misji reference wyniósł 11,1641–11,4203 ms, a combined 11,1959–11,5830 ms. Różnice nie uzasadniają włączenia optymalizacji. Nie wykonano testu istotności ani nie przyjmujemy różnicy poniżej 1% za ustalone przyspieszenie.

Dla misji, mediana wartości z czterech procesów:

| Wariant | GPU p95 / p99, ms | CPU submit mediana / p95, ms | Średni czas ścienny ticka, ms |
| --- | ---: | ---: | ---: |
| reference | 11,6306 / 11,7808 | 2,4002 / 3,2458 | 11,4537 |
| phase | 11,5238 / 11,6539 | 2,3868 / 3,2878 | 11,3503 |
| projection | 11,5874 / 11,7304 | 2,4053 / 3,2941 | 11,3688 |
| combined | 11,7393 / 12,1469 | 2,3818 / 3,1703 | 11,4785 |

To mediany osobno policzonych statystyk procesów, nie percentyle połączonej próbki. Pełne zestawienie trzech scen jest w [comparison-summary.json](../measurements/gpu-i04-5c/comparison-summary.json), dane i kolejność w [throughput](../measurements/gpu-i04-5c/throughput/runs.csv).

Liczba przebiegów i budów indeksu nie maleje: maska jest zintegrowana z dotychczasowymi przebiegami i ogranicza pracę ich wątków. Przykładowe liczniki misji reference z serii przed poprawką inicjalizacji, obejmujące 360 ticków i obsługę sceny: 50430 dispatchy, 720 budów indeksu, 9360 redukcji postaci; nie są to wartości samego okna 240 pomiarowych ticków. Certyfikat nie dodaje bufora ani odczytu CPU; maska dodaje opisane 960 B. Dane odczytów, opóźnień i wywołań pozostają w `window-counters.txt` osobno dla każdej sceny.

### Kontrola względem niezmienionej paczki A

Wcześniejsza seria A/F miała mediany misji około 2,79–3,09 ms. Aby sprawdzić zmianę warunków, uruchomiono zachowane EXE A i C/reference w porządku **A–C–C–A**, pod tym samym tymczasowym adresem EXE, kopiując właściwe assety przed procesem. Starego wydania nie nadpisano. Wszystkie cztery procesy zakończyły się kodem 0.

| Próba | Wariant | Misja GPU, ms | 16k GPU, ms | 32k GPU, ms |
| --- | --- | ---: | ---: | ---: |
| 1 | A | 11,3485 | 4,8748 | 8,7921 |
| 2 | C/reference | 11,2555 | 4,8365 | 8,7282 |
| 3 | C/reference | 11,6347 | 4,8476 | 8,7798 |
| 4 | A | 11,3052 | 4,8104 | 8,8050 |

Spowolnienie wystąpiło również w niezmienionym A. Nie jest dowodem regresji C ani dowodem konkretnej przyczyny w sterowniku, VRAM czy DWM. Telemetria bieżącej serii rejestrowała często około 1395 MHz i 19–23 W, wobec wyższych taktowań w dawnej serii A; sama korelacja nie ustala przyczyny. Hash EXE, czasy i PID są w [immutable-control/runs.json](../measurements/gpu-i04-5c/immutable-control/runs.json). Nie zmieniano globalnych ustawień zasilania ani sterownika.

## Polityka profilowania od utworzenia świata

Pierwsza kontrola `--gpu-readback-period 0` nadal zleciła **trzy początkowe kopie do pierścienia odczytów** (`readback_submitted=3`): świat powstawał z ustawieniami domyślnymi, a dopiero później dostawał opcje CLI. Kopie zostały unieważnione przez reset; `readback_completed=0` i `mapped_read_bytes=0`, więc nie były to trzy odczyty CPU. Nie pozwalało to wykluczyć wpływu początkowych kopii. Dowód zachowano w [readback-off-probe](../measurements/gpu-i04-5c/readback-off-probe/mission/window-counters.txt).

Fabryka `ParticleWorld::create` przyjmuje teraz `ProfileOptions` i ustawia politykę transportu, liczników i timerów przed pierwszym resetem. Aplikacja, nowy profiler stabilizacji oraz świeże światy historycznego profilera przekazują ją od początku. Test `initial_profile_policy` sprawdza zero odczytów i zapytań od tworzenia, przez reset misji, po kroki, render i opróżnienie kolejki. Ustawienie `collect=true` obejmuje teraz w licznikach totals także inicjalizację; różnice per-step pozostają porównywalne. Nie należy porównywać totals z A bez uwzględnienia tej zmiany.

Seria 16 wariantów i ABBA powyżej poprzedzają tę poprawkę; używały normalnej polityki odczytów, takiej samej dla wszystkich wariantów. Kontrola bez odczytów po poprawce i końcowe testy dotyczą finalnego builda. Brak wyniku timera w trybie off jest zapisywany jako niedostępny, nigdy jako czas zero.

Finalna kontrola potwierdziła `initial_readbacks_submitted=0` i `readback_submitted=0` dla każdej z trzech scen: brak kopii HUD od tworzenia świata przez pomiar. Pełny odczyt walidacyjny nadal odbywa się po pomiarze. Mediany GPU misji/16k/32k wyniosły 12,4807 / 4,9562 / 9,3353 ms, a średni czas ścienny ticka 13,3698 / 5,2938 / 9,5903 ms. Pojedyncza próba nie pokazała poprawy i miała większy rozrzut; nie wyznacza efektu przyczynowego odczytów. Poprawka naprawia warunki eksperymentu, nie rozwiązuje spowolnienia. [Dane finalnej kontroli](../measurements/gpu-i04-5c/readback-off-from-create/summary.csv).

## Eksperymenty odrzucone i ograniczenia

- Wyłączenie timerów nie przyniosło poprawy przepustowości: średni czas ścienny ticka misji 13,1837 ms; GPU timing niedostępny. To pojedyncza kontrola, nie estymata efektu. [Dane](../measurements/gpu-i04-5c/timers-off-probe/throughput-off-1/summary.csv).
- Tymczasowe `glBufferStorage(GL_DYNAMIC_STORAGE_BIT)` dla ośmiu buforów SSBO dało misję 11,4156 ms GPU i 12,1467 ms czasu ściennego; 16k/32k odpowiednio 5,0445/9,0481 ms GPU. Bez potwierdzonego zysku. Zmianę wycofano; [patch eksperymentu](../measurements/gpu-i04-5c/rejected-storage-probe.patch) i [wyniki](../measurements/gpu-i04-5c/storage-probe/throughput-elapsed-1/summary.csv) są zachowane. Nie jest to dowód stałej rezydencji VRAM ani ocena wszystkich wariantów storage.
- Pierwszy zbiorczy test wszystkich układów przekroczył 600 s podczas zimnej kompilacji shaderów; ukończył 9 kombinacji bez rozbieżności. [Wynik przerwania](../measurements/gpu-i04-5c/work-check/native-result.json) pozostaje w archiwum. Tę samą macierz rozdzielono na cztery testy CTest z limitem 600 s każdy, bez zmiany przypadków i tolerancji. Pierwsza kompilacja trzech wariantów SoA/reused zajęła około 50–53 s na świat; ten koszt nie wchodzi do benchmarku ticka i jest dodatkowym powodem pozostawienia opcji eksperymentalnymi.
- Ta iteracja nie zamyka odbioru wydajności okna całego I04.5 ani celu 1,50 ms. Nie wykonano nowej serii sześciu okien z kryteriami wydajności; finalny CTest sprawdza istniejące scenariusze UI i misji. Dawne 9 okien A/F pozostaje historycznym wynikiem.

## Końcowe QA i decyzja

- Finalny build Release `/W4 /WX` z IPO zakończony poprawnie. [Zapis źródeł i hash EXE](../measurements/gpu-i04-5c/source-state.json) rozróżnia finalny build od wcześniejszej serii porównawczej.
- **5/5 nowych testów C przeszło**: cztery konfiguracje indeksu/układu i pełna misja z obiema opcjami włączonymi. W 72 wierszach porównań małych scen maksymalny błąd stanu wyniósł dokładnie 0 na tym GPU; test zachowuje tolerancję 0,0001 dla innych urządzeń. [Podsumowanie macierzy](../measurements/gpu-i04-5c/qa/work-matrix-summary.json).
- Instrumentacja przeszła wraz z nowym `initial_profile_policy`, trzema trybami timerów, przełączaniem trybu przy oczekujących zapytaniach i resetem. [Log](../measurements/gpu-i04-5c/qa/instrumentation-profile/instrumentation-tests.txt).
- Skrypty przeszły kontrolę składni PowerShell 5.1. Kontrolne dane analyzera potwierdzają obsługę zagnieżdżonych profili oraz odrzucanie procesu z niezerowym kodem i niezgodnej efektywnej konfiguracji. [Wyniki fixture](../measurements/gpu-i04-5c/analyzer-fixture/checks.json).

Pełny CTest finalnego builda: **22/26 PASS, 4 błędy, kod wyjścia 8, 1208,42 s**. To wynik negatywny całego zestawu, pomimo poprawnego przejścia nowych przypadków C. Nie podniesiono limitów istniejących testów ani tolerancji fizyki. [Log](../measurements/gpu-i04-5c/ctest.log), [JUnit](../measurements/gpu-i04-5c/ctest.xml), [podsumowanie](../measurements/gpu-i04-5c/ctest-summary.json).

| Test | Wynik pełnej serii | Co pozostaje otwarte |
| --- | --- | --- |
| app.opengl_smoke | timeout 30,49 s przy limicie 30 s | Czas scenariusza CPU/UI |
| gpu.coupling | błąd po 217,99 s | Różnica średniej pozycji local 0,027835 przy granicy 0,020000; kontrolne powtórzenie reference dało 0,008098 |
| gpu.mission_ui | timeout 60,24 s przy limicie 60 s | Wydajność i ukończenie scenariusza okna |
| app.opengl33_smoke | timeout 30,15 s przy limicie 30 s | Czas scenariusza CPU/UI w GL 3.3 |

Pozostałe testy, w tym domyślna pełna misja, sąsiedztwo, wykonanie, transport, ziarna, ciecze i przepływ, przeszły. Pierwsze utworzenie rzadszych światów w teście coupling trwało 56–59 s na wariant, a cached 0,30 s; koszt kompilacji jest jawny w jego logu. Porównanie 41 hashy źródeł CPU/UI/platform z A wykazało brak zmian, lecz nie dowodzi identycznych binariów ani warunków systemowych. [Audyt źródeł](../measurements/gpu-i04-5c/cpu-source-audit.json).

Kontrole A/C uruchomiono sekwencyjnie pod tym samym tymczasowym adresem EXE, z właściwymi assetami i świeżymi profilami. Użyto diagnostycznego limitu **120 s**, aby poznać wynik procesu po przekroczeniu limitu oryginalnego CTest. Te wyniki nie zastępują pełnego CTest i nie zmieniają jego kryteriów. Tak jak dotychczasowy CTest, poniższe testy UI używają **ukrytego okna**; czas obejmuje cały proces, nie tylko fizykę. [Metadane i wyniki](../measurements/gpu-i04-5c/smoke-control/runs.json).

| Kontrola | A: czas / kod wyjścia | C: czas / kod wyjścia |
| --- | ---: | ---: |
| smoke GL 4.3 | 34,019 s / 0 | 30,718 s / 0 |
| smoke GL 3.3 | 31,019 s / 0 | 28,839 s / 0 |
| coupling | 7,153 s / 1 | 9,584 s / 0 |
| mission UI | 69,838 s / 1 | 71,141 s / 1 |

W kontroli coupling **A również przekroczyło tę samą tolerancję**: różnica średniej pozycji 0,038122 przy granicy 0,028199; C w powtórzeniu dało 0,012646 i przeszło. Błąd nie jest unikalny dla C, ale pojedyncze udane powtórzenie nie zapewnia stabilności testu ani nie unieważnia pierwotnego błędu.

Oba procesy mission UI ukończyły 900 klatek, 3594 ticki i misję z wynikiem 3, lecz zgłosiły utratę czasu: A **36,625 s**, C **38,250 s**. Mediany klatki wyniosły odpowiednio 76,091 i 80,039 ms; GPU tick 16,284 i 17,330 ms. Procesy poprawnie zwróciły błąd, ponieważ samo zaliczenie celów nie spełnia kryterium zegara.

W tym scenariuszu pętla musi zlecić co najmniej 1700 ticków i wyrenderować co najmniej 900 klatek; limit procesu obejmuje również inicjalizację, kompilacje, zrzuty i końcowy odczyt. Z timeoutu bez końcowego raportu nie można ustalić, w której fazie proces się zatrzymał. To jedna z przyczyn stosowania świeżych profili i zachowania końcowych kontroli po pomiarze.

Jedna dodatkowa para A/C z `--show-test-window` również zakończyła się błędem zegara. A: 65,159 s procesu, 3589 ticków, mediana klatki 71,421 ms, utrata czasu 31,192 s. C: 63,385 s, 3587 ticków, mediana 71,418 ms, utrata czasu 30,275 s. Obie wersje wykonały 900 klatek i ukończyły misję z wynikiem 3. Samo pokazanie okna nie przywróciło poprawnego przebiegu w tej sesji. To dwie kontrole, nie wymagana seria sześciu okien ani dowód konkretnej przyczyny w systemie. [Dane widocznych okien](../measurements/gpu-i04-5c/visible-control/runs.json).

Paczka `StrefaZero-Windows-x64-I04_5-C` przeszła własne `--gpu-profile-check` (kod 0) i wymuszony `--opengl33 --gpu-work-check` (oczekiwany kod 77). EXE i assety porównano z finalnym buildem/źródłami; paczka ma własny manifest SHA256 i kontrolę CRC ZIP. Jest to paczka diagnostyczna z otwartym QA, bez nowego tagu wydania. [Instrumentacja paczki](../measurements/gpu-i04-5c/package-profile-check/instrumentation-tests.txt), [fallback](../measurements/gpu-i04-5c/package-gl33-work-skip/native-result.json), [manifest danych](../measurements/gpu-i04-5c/SHA256SUMS.txt).

Kolekcja QA pomija sześć starszych plików z ponownie używanych katalogów testowych. Raporty pozostałe po dawnych udanych uruchomieniach nie są dowodem sukcesu obecnych timeoutów; [proweniencja kolekcji](../measurements/gpu-i04-5c/qa/collection-provenance.json) wymienia wykluczenia. Kontrole A/C używają świeżych profili.

Domyślna ścieżka solvera zachowuje obie opcje C wyłączone. Warianty są dostępne do dalszych pomiarów wraz z kontrolą stanu i natywnym harness. Korekta inicjalizacji profilowania pozostaje w kodzie jako naprawa poprawności eksperymentów.

Następny zakres to **I04.5-B**: najpierw odtworzyć i wyjaśnić otwartą bazę QA, następnie oddzielnie zmierzyć inicjalizację i transport runtime, z potwierdzoną polityką od `create()`. Warianty rozdzielenia buforów, immutable storage czy mapowania wybierać dopiero z danych. D/E pozostają warunkowe. I05 nadal czeka na ocenę całego I04.5. Instrukcje z dostarczonego review pozostają propozycjami technicznymi ocenianymi w planie; wyniki tej iteracji nie potwierdzają obiecanych w nim przyspieszeń.
