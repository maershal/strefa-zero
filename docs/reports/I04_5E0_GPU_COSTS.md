# I04.5-E0 — koszty przebiegów i Dmax w czasie

Prace: 2026-10-30. Baza: lokalne I04.5-A/F/C/B/D na `6f318ea`. [Raport D](I04_5D_GPU_NEIGHBOR_READS.md) pozostaje historyczny. E0 jest diagnostyką przed decyzją o lokalnym Dmax, nie wdrożeniem lokalnego indeksu.

**16/16 procesów porównawczych zakończyło się poprawnie.** W zwykłej misji największe koszty wskazują gęstość i korekta PBF; przy 16k/32k dominują kontakty ziaren. Zaobserwowany globalny Dmax sięgnął 0,125 komórki w misji, 0,25 w scenach ziaren i 0,4375 po eksplozjach. To uzasadnia dalszy eksperyment wewnątrz przebiegów PBF. Lokalny Dmax pozostaje warunkowy. Cel GPU <1,50 ms nadal jest otwarty.

## Co dodano

`--gpu-cost-profile --gpu-cost-mode off|passes|bounds|snapshots` uruchamia stały protokół diagnostyczny. Zwykła gra nie uruchamia tego rejestratora. Nie zmieniono shaderów, równań solvera, liczby iteracji, globalnego zakresu sąsiedztwa ani domyślnych ustawień full/direct i obu opcji C OFF.

| Tryb | Rejestrowane dane | Dodatkowe zasoby w każdej scenie |
| --- | --- | --- |
| off | dotychczasowe timery kroku i liczniki | brak zasobów sondy |
| passes | jedna para timestampów wybranego dispatchu na tick, pozostałe wywołania liczone | pula 512 par zapytań; wykorzystano po 240 |
| bounds | kopia 8 B: Dmax i numer budowy indeksu po każdym dispatchu z ważnym indeksem | 512 000 B stagingu |
| snapshots | bounds oraz stan cząstek, prefixy i istniejący indeks w wybranych punktach | 87 460 352 B stagingu, około 83,41 MiB |

Nie dodano wiązania SSBO ani przebiegu compute. Timestampy obejmują dispatch i następującą barierę, po ustawieniu programu/uniformów, przed późniejszymi kopiami sondy. Wybór pojedynczego dispatchu rotuje po pozycjach harmonogramu; nie jest sprzętowym pomiarem ruchu pamięci ani pełnym profilem wszystkich wywołań. Pojemności są jawne i ograniczone; przepełnienie kończy diagnozę błędem, zamiast po cichu usuwać rekordy.

Kopie używają osobnych buforów `GL_COPY_*`; nie przebudowują indeksu, nie zapisują autorytatywnych cząstek i nie zajmują kolejnego slotu SSBO. Odczyt wyników CPU odbywa się po jawnym `glFinish()` poza zmierzoną partią. Poszczególne wywołania GL mogą czekać wewnątrz sterownika; narzut mierzymy, nie zakładamy jego braku. Rozmiary opisują magazyny GL, nie fizyczną rezydencję VRAM.

Snapshoty powstają przy budowie indeksu oraz po pierwszym i ostatnim kontakcie ziaren w obu podkrokach, w tickach 121, 122, 185 i 360. Daje to 24 snapshoty na scenę. Analiza na CPU korzysta z przechwyconego stanu i istniejących anchorów/prefixów, bez naprawiania indeksu. Sprawdza ID, mapę odwrotną, członkostwo, zgodność histogramu/prefixów i konserwatywną granicę ruchu.

## Kontrole poprawności

`--gpu-cost-check` porównuje wszystkie cztery tryby z referencyjnym stanem po czterech krokach: pozycje, motion, kontakt, kompensacja i materiały. Scena ma rzadkie ID, mieszaninę cieczy/ziaren, małą kompensację i wymuszone przesunięcie cząstki o 96,2 komórki. Sonda zarejestrowała granicę ponad 96, a niezależne all-pairs nie wykazało pominiętych par w przechwyconych snapshotach.

Kontrola sprawdza też brak alokacji/wywołań GL przy wyłączonej sondzie, wyłączenie po odebraniu wyników, tagowanie generacji po resecie i jawny błąd przepełnienia. Wstępne SoA oraz końcowe auto/immutable przeszły: maksymalny błąd stanu **0** we wszystkich trybach. [Walidacja](../measurements/gpu-i04-5e0/check-final-auto/cost-tests.txt), [snapshot wymuszonego outliera](../measurements/gpu-i04-5e0/check-final-auto/snapshots/snapshots.csv).

**Pełny CTest: 37/38, kod 8, 330,27 s.** Wszystkie trzy nowe testy diagnostyki (AoS, SoA i auto/immutable) przeszły, podobnie dotychczasowe testy B/C/D, misji, sąsiedztwa, wykonania, odczytów, fizyki, UI, CPU i GL 3.3. Nie przeszedł wcześniejszy `gpu.coupling`, w porównaniu geometrycznym wariantu local z referencją. [Pełny log](../measurements/gpu-i04-5e0/ctest.log), [JUnit](../measurements/gpu-i04-5e0/ctest.xml), [podsumowanie](../measurements/gpu-i04-5e0/ctest-summary.json).

W nieudanej próbie E0 przesunięcie środka wyniosło 0,0501863 przy limicie 0,0467122, wyliczonym z powtórki referencji. Osobna powtórka E0 przeszła. Niezmieniona paczka D odtworzyła ten sam błąd: 0,0213728 przy limicie 0,0205292. Hash kontrolnego D odpowiada zachowanemu buildowi `cca9aba9…7f7ee7a`. [Pierwszy błąd](../measurements/gpu-i04-5e0/coupling-first-failure/coupling-tests.txt), [powtórka E0](../measurements/gpu-i04-5e0/coupling-recheck-E0/coupling-tests.txt), [kontrola D](../measurements/gpu-i04-5e0/coupling-control-D/coupling-tests.txt), [liczby i wyniki procesów](../measurements/gpu-i04-5e0/coupling-controls.json).

To dowód występowania błędu także w bazie, nie wyjaśnienie jego przyczyny ani zamknięcie QA. Nie zmieniono testu, limitów ani tolerancji; nie zastąpiono pełnego wyniku 37/38 zieloną powtórką. Baza QA sprzężenia wymaga wyjaśnienia przed kolejną optymalizacją. Kolekcja zachowuje świeże pliki, w tym nieudaną próbę, i pomija starsze artefakty. [Proweniencja kolekcji](../measurements/gpu-i04-5e0/qa/collection-provenance.json).

## Protokół i narzut sond

```powershell
.\StrefaZero.exe --gpu-cost-check --profile .\qa-cost
.\scripts\profile-cost.ps1 -Executable '<gra>\StrefaZero.exe' -Output '<nowy katalog>' -Repetitions 4
python .\scripts\analyze-stabilization.py --input '<wyniki>'
python .\scripts\analyze-cost.py --input '<wyniki>'
```

GTX 1650, NVIDIA 610.47, OpenGL 4.3, Release MSVC `/W4 /WX`, IPO. EXE SHA256 `adebd3b3ecdf42ef44df623953c3d909eb42f84e680d120091c56ef437402cf7`. Każdy proces mierzy świeżą misję, 16k, 32k i misję z dwiema kontrolowanymi eksplozjami. Cztery warianty × cztery powtórzenia, pełny rotowany Latin; bez równoczesnych testów GPU.

Każda scena: 120 ticków rozgrzewki i 240 pomiarowych, elapsed, liczniki/diagnostyka/sprzężenie ON, SoA/reused, combined, full/direct, HUD auto/2, opcje C OFF. Drain i poll co 16 ticków są poza CPU submit, ale w czasie ściennym partii. Eksplozje mają ID 100 i 164, pozycję (128,147), promień 22 i impuls 70; następują w tickach 121 i 185. Misja z eksplozjami kończy się 3114 cząstkami wobec 3096 przed destrukcją. Surowe wyniki zapisują rzeczywiste członkostwo.

**16/16 kod 0, bez timeoutów; analyzer schema 6: 192 wiersze, zero ostrzeżeń.** Sprawdzana jest zgodność żądanego i efektywnego trybu diagnostyki, także konflikt etykiety CSV z zapisanymi argumentami natywnego procesu. Składnia Windows PowerShell 5.1 oraz cztery kontrole analyzera przeszły. [Procesy i komendy](../measurements/gpu-i04-5e0/cost-matrix/runs.csv), [analiza podstawowa](../measurements/gpu-i04-5e0/cost-matrix/analysis.csv), [składnia](../measurements/gpu-i04-5e0/script-syntax.json), [kontrole](../measurements/gpu-i04-5e0/analyzer-fixture-checks.json).

Poniżej mediana czterech median, ms/tick. p95 jest medianą czterech percentyli, nie percentylem połączonej próbki. **Off jest referencją wydajności; pozostałe warianty mierzą także własny narzut.**

| Tryb | GPU misja | Misja p95 | GPU 16k | GPU 32k | GPU misja z eksplozjami | CPU submit misji |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| off | 2,84254 | 3,22542 | 1,79085 | 3,18470 | 3,02174 | 0,00780 |
| passes | 2,80638 | 3,20658 | 1,79320 | 3,18886 | 3,02358 | 0,69215 |
| bounds | 3,22898 | 3,64675 | 2,01760 | 3,40994 | 3,42078 | 0,01895 |
| snapshots | 3,21294 | 3,73440 | 2,02662 | 3,41501 | 3,41754 | 0,02050 |

W misji bounds podnosi medianę GPU o około 13,6%. Passes nie wykazuje wyraźnego wzrostu mediany GPU, ale ma duży koszt CPU: około 0,692 ms wobec 0,0078 ms. Nie traktujemy tego trybu jako pomiaru opóźnień zwykłej gry. Różnica GPU off/passes mieści się w nakładających się zakresach prób; nie jest przyspieszeniem solvera. Czas ścienny na tick misji: off 2,91544 ms, passes 2,88312 ms, bounds 3,31266 ms, snapshots 3,42103 ms. CPU i GPU nakładają się, więc tych czasów nie sumujemy. [Wszystkie zakresy, p99 i maksima](../measurements/gpu-i04-5e0/cost-matrix/cost-analysis.json).

Dodatkowo porównano niezmienione D i E0 z wyłączoną nową sondą w identycznym dotychczasowym protokole `--gpu-stabilization-profile`: po trzy procesy, naprzemienny porządek tworzący ABBA. **6/6 kod 0, analyzer 108 wierszy, zero ostrzeżeń.** Oba EXE są identyfikowane hashami, a shadery mają identyczną zawartość.

| Build / sonda wyłączona | GPU misja | GPU 16k | GPU 32k | CPU submit misji |
| --- | ---: | ---: | ---: | ---: |
| D | 2,80704 | 1,78102 | 3,13203 | 0,00700 |
| E0 | 2,82010 | 1,78189 | 3,16816 | 0,00780 |

E0 ma w tej małej serii medianę GPU 32k dłuższą o około 1,15%, a misji o 0,47%; zakresy prób nakładają się. Nie wykonano testu istotności i nie deklarujemy zerowego kosztu wyłączonych haków CPU. CPU submit pozostaje poniżej celu 0,1 ms. [Wyniki kontroli](../measurements/gpu-i04-5e0/disabled-control-summary.json), [procesy](../measurements/gpu-i04-5e0/disabled-control/runs.csv), [hashe źródeł i EXE](../measurements/gpu-i04-5e0/source-state.json). Nie zmieniano ustawień systemu ani sterownika; telemetria dotyczy całych procesów, także inicjalizacji i walidacji.

## Gdzie trafia koszt

Rzadkie timestampy dały po 240 par na scenę, łącznie cztery próby na kategorię sceny. Poniższe liczby są oszacowaniem: średnia zmierzona długość przebiegu × rzeczywista liczba wywołań/tick. Znaczniki wpływają na kolejkę, a mało częste przebiegi mogą nie mieć próbki. Nie jest to marginalny koszt, który można bezpośrednio odjąć od całego kroku.

| Scena | Największe oszacowania, ms/tick | Liczba próbek tych przebiegów |
| --- | --- | --- |
| misja | korekta PBF 0,92592; gęstość PBF 0,85733; kontakt ziaren 0,64466 | 112 / 108 / 160 |
| 16k | kontakt ziaren 1,23892; redukcja reakcji 0,15294; finalizacja redukcji 0,13749 | 240 / 232 / 224 |
| 32k | kontakt ziaren 2,55460; odtworzenie prędkości 0,18835; predykcja 0,16071 | 240 / 20 / 20 |
| misja z eksplozjami | korekta PBF 1,01043; gęstość PBF 0,87857; kontakt ziaren 0,65658 | 112 / 108 / 160 |

Suma dostępnych oszacowań misji wynosi 3,20401 ms, a mediana całego kroku wariantu passes 2,80638 ms. Ten brak addytywności pokazuje ograniczenie metody. Nie zmierzono osobno rzadkich operacji zerowania i diagnostyki maksymalnej kompresji cieczy; w wynikach mają brak danych, a nie 0 ms. Ranking głównych producentów jest przesłanką do kolejnego A/B, nie obietnicą przyspieszenia. [Próbki i oszacowania osobno dla każdej próby](../measurements/gpu-i04-5e0/cost-matrix/pass-estimates.csv).

## Dmax i nadmiar kandydatów

W bounds/snapshots zebrano 32 serie checkpointów. Percentyle checkpointów ważą granice dispatchy, nie czas GPU; analiza zapisuje osobno maksima na tick. W 384 snapshotach sprawdzono oba zasięgi zapytań (1 i 2), co daje 768 wierszy przestrzennych. **Nie wykryto pominiętych geometrycznych sąsiadów.** Małe fixture korzystają z all-pairs; duże sceny z niezależnego indeksu CPU aktualnych pozycji. Widoczność przez ściany i filtry materiałów są poza tym licznikiem geometrycznym; sprawdzają je wcześniejsze regresje solvera.

| Scena | Największy Dmax w całych nagranych trajektoriach | Największy stosunek kandydatów starego indeksu do świeżego, w wybranych snapshotach |
| --- | ---: | ---: |
| misja | 0,125 | 1,08129 |
| 16k | 0,25 | 1,04390 |
| 32k | 0,25 | 1,04103 |
| misja z eksplozjami | 0,4375 | 1,49974 |

Świeży indeks powstaje wyłącznie w analizie CPU z tego samego przechwyconego stanu i własnym histogramem. Jest punktem odniesienia, nie proponowaną ścieżką solvera. Porównanie uwzględnia zmianę anchorów i granicy ruchu; nie izoluje samego globalnego Dmax ani kosztu ewentualnej przebudowy. Różnica kandydatów nie przekłada się wprost na czas GPU.

Snapshoty nie obejmują każdego checkpointu: największy Dmax może wystąpić między nimi. Wyniki dotyczą tych scen, dwóch eksplozji i jednego GPU; nie wykluczają cięższych outlierów w innych sytuacjach. Duże przesunięcie 96,2 z fixture jest testem kompletności, a nie wynikiem zwykłej gry. Globalny konserwatywny fallback pozostaje zachowany. [Serie, punkty szczytowe i snapshoty](../measurements/gpu-i04-5e0/cost-matrix/cost-analysis.json); histogramy liczebności są obok każdego `snapshots.csv` i zostały uzgodnione z sumami kandydatów i sąsiadów.

## Decyzja i dalsza praca

**E0 zakończono jako badanie; lokalnego Dmax nie wdrożono.** Domyślna diagnostyka OFF, full/direct i obie opcje C OFF. Nie zmniejszono dokładności, liczby iteracji ani limitu zaległości zegara. Cel GPU 1,50 ms i przyczyna historycznej zmienności między sesjami są nadal otwarte.

Najpierw wymaga wyjaśnienia baza QA sprzężenia. Po jej odbiorze następny eksperyment I04.5-G: odrębny rozmiar grupy PBF 64/128/256, przy pozostawieniu ziaren i redukcji na 256. Wcześniejszy I02 badał rozmiar wspólny; teraz profil wskazuje konkretną rodzinę przebiegów. Najpierw poprawność, potem A/B na tych samych scenach i w oknie. Osobną hipotezą są wskazówki braku aliasowania dla rzeczywiście rozdzielonych banków Jacobi; nie wolno przenosić ich na operacje in-place. Żadna z tych hipotez nie jest jeszcze wynikiem wydajności.

Paczka `StrefaZero-Windows-x64-I04_5-E0` przeszła własne `--gpu-cost-check --gpu-storage auto` i `--gpu-profile-check` (kod 0); `--opengl33 --gpu-cost-check` zwróciło oczekiwane 77. Kontrola widocznego okna przeszła: 900 klatek, 1798 ticków, wynik 3 i brak utraty czasu. Mediana całej klatki po 30 klatkach rozgrzewki: 16,66860 ms, p95 16,66940 ms, maksimum 43,41720 ms, z przechwytywaniem obrazu w pętli.

Fokus był aktywny w 0/870 analizowanych klatkach, minimalizacja: 0. To kontrola diagnostyczna, nie odbiór wymaganego okna z fokusem. KHR_debug id131186 według fazy odbioru: runtime 0, validation 1; logger zgubił 0 komunikatów. Asynchroniczny callback nie dowodzi miejsca pamięci ani przyczyny historycznego spowolnienia. [Kontrole paczki](../measurements/gpu-i04-5e0/package-checks.json), [okno i fazy debug](../measurements/gpu-i04-5e0/package-window-summary.json).

EXE i assety odpowiadają finalnemu buildowi/źródłom. Paczka ma manifest SHA256 i kontrolę CRC ZIP. Jest kompilacją diagnostyczną z jawnym otwartym wynikiem QA 37/38, bez nowego tagu wydania. Zweryfikowano niezmienność 2850 plików historycznych raportów/danych oraz sum zewnętrznych paczek A/B/C/D.

Polecenia i deklaracje z dostarczonego review pozostają propozycjami do weryfikacji. Wyniki A/F/C/B/D, ich nieudane próby i paczki pozostają zachowane. I05 nie rozpoczyna się automatycznie.
