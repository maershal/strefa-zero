# Historia zmian

Od najnowszych. Szczegóły, pomiary i ograniczenia każdego etapu są w [raportach](reports/); surowe dane w repozytorium [strefa-zero-measurements](https://github.com/maershal/strefa-zero-measurements).

## Porządki — podział StrefaZero / StrefaQA

`StrefaZero` zawiera grę i 10 opcji gracza (`--help`); walidacje, benchmarki, profile i pokrętła eksperymentów przeszły do osobnego programu `StrefaQA` (biblioteka `strefa_diagnostics`) z niezmienioną semantyką. CTest uruchamia tryby QA przez `StrefaQA` bez zmiany nazw, etykiet i limitów czasu; `app.opengl_smoke`, `app.opengl33_smoke` i `gpu.mission_ui` nadal sprawdzają `StrefaZero`. `Application` przyjmuje `LaunchConfig` zamiast 21 parametrów. Stan wyjściowy: [INVENTORY.md](maintenance/INVENTORY.md).

## Porządki — osobne repozytorium pomiarów

Surowe dane z `docs/measurements/` (12 312 plików, 679 MB) przeniesiono bajt w bajt do [strefa-zero-measurements](https://github.com/maershal/strefa-zero-measurements/tree/88ba1601a3c6ab19b0e97a3ed8e679a593e09539); katalogi bez manifestu dostały tam własne `SHA256SUMS.txt`. Linki w raportach i planach są permalinkami do tego commita. Historii repozytorium nie przepisywano, więc starsze commity nadal zawierają dane. Usunięto hook Claude Code blokujący odczyt pomiarów wraz z jego testem w CI.

## Wydanie I11 Preview

Gotowy program dla Windows i pełne surowe obrazy QA znajdują się w [wydaniu I11 Preview](https://github.com/maershal/strefa-zero/releases/tag/v0.7.0-i11-preview.1). [Instrukcja pobrania i odtworzenia obrazów](QA_IMAGES.md) opisuje układ archiwum. Kod oraz raporty obejmują etapy I05–I11; oświetlenie i termika pozostają domyślnie wyłączone z powodu niespełnionych budżetów.

## I11 — oświetlenie i styl

Opcjonalne `--gpu-lighting-quality off|low|high` dodaje światła 2D, cienie reagujące na destrukcję, liniowy HDR, bloom oraz paletę i wizualną wilgoć piasku. Misja i narzędzia korzystają ze wspólnego stanu; ustawienia światła są niezależne od jakości wody. Windows 67/68, GCC 11/11. Low osiąga do +1.028 ms przy limicie 0,35 ms; domyślne `off` pozostaje, a odbiór aktywnego okna: otwarty. Wstrząs i aberracja są osobno wyłączalne. [Raport I11](reports/I11_LIGHTING.md) zawiera kontrakt, sposób uruchomienia i wyniki.

## I10 — termika, gazy i ogień GPU

Opcjonalny moduł wspólnego świata GPU dodaje zachowawczy transport ciepła i gazów, spalanie drewna/oleju/gazu, parowanie/skraplanie oraz fazy lodu, lawy i metalu. Narzędzia pokazują temperaturę, tlen, paliwo, prędkość i ciśnienie projekcji. Sceny: `--gpu-thermal-scene fire|oil|quench|phases`; w misji: `--gpu-thermal on`. Nagrania v3 zapisują źródła energii; v1/v2 pozostają zgodne.

Termika jest domyślnie wyłączona: końcowa macierz nie spełnia budżetu 0,70 ms/tick dla scen gorących ani 0,05 ms dla zimnej włączonej termiki. Funkcjonalny zakres, testy, koszty i ograniczenia materiałów opisuje [raport I10](reports/I10_GPU_THERMAL.md). Kontrola: `--gpu-thermal-check --profile wyniki`; pomiar: `--gpu-thermal-benchmark --profile pomiary`.

## I09 — wspólne efekty GPU

`--gpu-vfx-quality off|low|high` steruje eksplozjami, trafieniami, plecakiem oraz wtórnymi cząstkami wody. Misja, laboratorium GPU i poligon CPU na GL 4.3 współdzielą pulę 16 384 slotów. Fizyka pozostaje domyślnie legacy, obraz wody basic. Na GL 3.3 działa uproszczony fallback.

Kontrole: `--gpu-vfx-check --profile wyniki`, pomiary: `--gpu-vfx-benchmark --profile pomiary`. [Raport I09](reports/I09_GPU_VFX.md) opisuje architekturę, LOD low, testy, budżet i ograniczenia.

Odbiór wydajności I09 pozostaje otwarty: końcowy najgorszy segment low dodał 0,678 ms przy limicie 0,25 ms. Szczegóły i warunki pomiarów są w raporcie I09.

## I08 — renderer i optyka cieczy

Renderer gry i narzędzi składa tło, teren, materiały i postać przed wodą. Woda oraz olej podbarwiają zanurzone obiekty i korzystają z refrakcji sprawdzającej cały odcinek próbkowania. Tarcze mają czytelny znak, HP i potwierdzony błysk trafienia.

`--gpu-render-quality basic|standard|high` zmienia wyłącznie obraz, także bez resetu w narzędziach. Domyślne `basic` wynika z pomiarów GTX 1650; `standard` dodaje filtr bilateralny, grubość segmentową i połysk, `high` także pianę i kaustyki. Rozdzielczość renderu pozostaje 960 × 576 niezależnie od rozmiaru okna. Fizyka nadal domyślnie `legacy`.

Kontrole i pomiary: `--gpu-render-check --profile wyniki`, `--gpu-render-benchmark --profile pomiary`. [Raport I08](reports/I08_SCENE_RENDERER.md) opisuje kontrakt przebiegów, testy, budżet i ograniczenia.

## I07 — opcjonalna fizyka cieczy

Domyślny profil `legacy` zachowuje ustawienia I06. Eksperymentalny profil `balanced` dodaje ważone granice, napięcie 2D, adhezję i ograniczoną wirowość: `StrefaZero.exe --mission --gpu-fluid-profile balanced`. Te same ustawienia działają w narzędziach świata. Nagrania v2 zawierają konfigurację fizyki; starsze nagrania v1 wymagają `legacy`.

Walidacja: `StrefaZero.exe --gpu-fluid-check --gpu-fluid-profile balanced --profile wyniki-i07`. Wyniki, koszt i ograniczenia: [I07_FLUID_QUALITY.md](reports/I07_FLUID_QUALITY.md).

## I00–I06 — rozwój GPU i protokół gry

**I06 (2026-10-30):** dodano jedną sesję reguł CPU, numerowane komendy i niezawodne zdarzenia, potwierdzenia i zwroty amunicji, tolerancję skoku, wejście poza areną oraz wspólną pozę/IK. [Raport I06](reports/I06_GAMEPLAY_PROTOCOL.md) zawiera walidację i jawne ograniczenia. Końcowe serie direct osiągają p95 CPU submit 0,029–0,042 ms/tick; wcześniejsze wyższe wyniki pozostają w raporcie, a stabilność wydajności jest nadal otwarta.

**Aktualizacja 2026-10-30:** decyzją użytkownika **I04.5 odłożono** z niespełnionym celem 1,5 ms i nierozpoznaną przyczyną skoków GPU. G3-C nie jest kontynuowane. Wdrożono **[I05 — jeden świat gry i narzędzi](reports/I05_SHARED_WORLD.md)**: jeden komplet 42 programów, zachowanie sceny między widokami, leniwe tworzenie CPU, opcjonalny cache shaderów i nagrywanie/odtwarzanie wspólnej sesji. Regresja Windows: 53/54, timeout okna misji wystąpił także na poprzedniej I04.5; szczegóły w raporcie I05.

**Eksperyment I04.5-C:** przeprowadzono 16 z 16 zaplanowanych pomiarów przepustowości [wariantów maski faz i lokalnego potwierdzenia projekcji](reports/I04_5C_GPU_WORK.md). Nie potwierdzono przyspieszenia. Opcje `phase_gating` i `skip_zero_projection` pozostają domyślnie OFF; wyniki QA są prowadzone w raporcie C. Poprawiono przekazywanie początkowej polityki odczytów do świata, także w starszych helperach. Badanie transportu opisuje [raport I04.5-B](reports/I04_5B_GPU_TRANSPORT.md); Wyniki późniejszego badania odczytów są w raporcie D poniżej.

**I04.5-B — transport:** dodano eksperymentalny UploadRing i ścieżki staged/immutable/auto z fallbackiem GL 4.3. Pełne QA **28/28 PASS**, porównanie **36/36 procesów**. Brak spójnej przewagi uzasadniającej zmianę domyślnego `direct`; duża zmienność między sesjami wystąpiła na identycznym EXE. Historyczne błędy C i ograniczenia są zachowane w [raporcie B](reports/I04_5B_GPU_TRANSPORT.md).

**I04.5-D — odczyty sąsiadów:** dodano eksperymentalne narrow i aktualizowany cache sorted. **35/35 QA**, **18/18 procesów porównawczych** i **7 okien bez utraty czasu**. Domyślne pozostają full/direct i obie opcje C OFF: cache +2 MiB spowalnia większe sceny, a niewielki zysk narrow nie jest spójny między scenami. [Raport D](reports/I04_5D_GPU_NEIGHBOR_READS.md) opisuje wyniki i warunki dalszego E.

**Stan po I04.5-E0, przed Q — profil kosztów i Dmax:** **16/16 pomiarów**, **6/6 kontroli**, 384 snapshoty. Profil wskazuje PBF w misji i kontakt ziaren w dużych scenach; lokalny Dmax odroczono, sondy pozostają OFF. Pełne QA **37/38**: zmienność wcześniejszego testu sprzężenia odtworzono także na niezmienionym D. Najpierw wymaga wyjaśnienia baza QA, potem punktowe strojenie PBF. [Raport E0](reports/I04_5E0_GPU_COSTS.md).

**I04.5-Q — metoda QA sprzężenia:** stara reguła odrzuciła identyczne referencje 1/16. Nowe odtwarzanie wspólnych stanów i stałe budżety przeszły **12/12** powtórek, wykrywając każde celowe wyłączenie sprzężenia. **38/38 w nowym protokole Q**; dawna reguła pozostaje diagnostyką, solver nie został zmieniony. Późniejsze badanie grup opisuje G1 poniżej. [Raport Q](reports/I04_5Q_GPU_COUPLING_QA.md).

**I04.5-G1 — niezależne grupy PBF:** `--gpu-pbf-size 64|128|256`, przy zachowaniu ziaren i redukcji 256. **44/44 QA**, po trzy powtórki Q na konfigurację, **16/16** końcowych pomiarów i **8/8** widocznych okien z fokusem bez utraty czasu. Brak powtarzalnego zysku: misja G 256/64 **2,83006/2,82413 ms**, więc domyślne 256 pozostaje bez zmian. Cel GPU 1,5 ms otwarty. [Raport G1](reports/I04_5G_GPU_PBF_GROUPS.md) zawiera kompletne dane, próby wstępne i odrębną propozycję G2; `scripts/profiling/profile-pbf.ps1` i `scripts/profiling/analyze-pbf.py` odtwarzają porównanie z opublikowaną Q.

**I04.5-G2 — kontrakt dostępu do banków PBF:** `--gpu-pbf-restrict on|off` obejmuje wyłącznie korektę i XSPH; domyślnie OFF. **52/52 QA**, 128/128 odrzuconych błędów wiązań, 8/8 Q i usterek, 6/6 widocznych okien z fokusem. Dwie serie po 18/18 procesów nie potwierdziły powtarzalnego przyspieszenia; w szybszej OFF/ON **2,81512/2,81293 ms**. Ta sama niezmieniona G1 miała około **2,8→12→2,8 ms**; przyczyna otwarta. [Raport G2](reports/I04_5G2_GPU_PBF_RESTRICT.md) zachowuje obie serie, faktyczne źródła GL, ograniczenia i punkt wyjścia do diagnozy G3.

**I04.5-G3-A — obserwacja zmienności:** runtime bez zmian; nowy harness zapisuje stan okna/fokusu, CPU, AC, priorytet i liczniki silników GPU. **25/25 procesów + 2/2 kontrole**, 13/13 negatywnych kontroli analyzera, 384 rekonstrukcje bez pominiętych par. Ukryta misja G1 bez sond **2,785–2,898 ms**; skoku do 12 ms nie odtworzono. Przyczyna i cel 1,5 ms nadal otwarte. [Raport G3-A](reports/I04_5G3_GPU_VARIANCE.md) zachowuje narzut, dane i wynik pierwszej ograniczonej próby; dalsze odtworzenie opisuje G3-B poniżej.

**I04.5-G3-B — wolny reżim odtworzony:** niezmieniona G1 po pełnym QA + Q zmieniła misję z **2,77 na 11,34 ms/tick**, a później wróciła do około 2,8 ms przed pokazaniem okna. **52/52 świeżego QA**, 6/6 dodatkowych Q, 192 rekonstrukcje w wolnym reżimie bez pominiętych par; histogramy i CSV 16k/32k identyczne z szybkimi próbami. To postęp diagnostyczny, bez nowej optymalizacji. Przyczyna pozostaje otwarta; następne G3-C rozdziela czas bezczynności i proces nadzorujący. [Raport G3-B](reports/I04_5G3B_GPU_REPLAY.md).

[Plan rozwoju GPU, wspólnego świata, wody i efektów](plans/PLAN_ROZWOJU_GPU.md) zawiera analizę review architektury z 2026-10-28, kolejność wdrożenia, zależności, cele pomiarowe i rejestr postępu. Wdrożono [instrumentację I00](reports/I00_GPU_PROFILING.md) i [pierścień odczytów I01](reports/I01_GPU_READBACK.md). Domyślny HUD korzysta z trzech slotów i odczytu na poziomie klatki, z trwałym mapowaniem lub fallbackiem GL 4.3. Wdrożono również [I02 — przebiegi, wiązania i FIFO](reports/I02_GPU_EXECUTION.md): 27 programów compute, około 72% mniej wywołań GL w porównywanym ticku i około 7% krótszy krok GPU misji w kontrolowanej serii. Wdrożono [I03 — dane i kompletne sąsiedztwo](reports/I03_GPU_NEIGHBORS.md): sortowane zakresy stabilnych ID, SoA, 42/43 → 2 budowy indeksu na tick i około 15% krótszy krok GPU misji względem ścieżki I02 w kontrolowanym A/B. Wdrożono [I04 — lokalne reakcje postaci](reports/I04_GPU_COUPLING.md): zwarta kolejka rzeczywistych reakcji, osobne kanały, redukcja hierarchiczna i ograniczenie próbek kapsuły do jej otoczenia. Domyślny combined łączy redukcję PBF i kontaktu, z osobnymi kanałami reakcji. Historyczny zakres review obejmował eksperyment PBF G po jawnej zmianie metody QA w Q; dalsze strojenie odłożono. Lokalny E pozostaje warunkowy. B/D zakończono jako badania; full/direct pozostają domyślne. Obie opcje C pozostają OFF. Decyzja użytkownika z 2026-10-30 zdejmuje blokadę I05; nie stanowi odbioru wydajności I04.5. Końcowa regresja I04: 21/21, okna combined 3/3; 16k/32k ziaren około 29%/33% krócej na GPU. Zysk małej misji nie jest potwierdzony, a cel 1,5 ms pozostaje otwarty. Historyczne spowolnienia opisano i zachowano w raportach.

## Misja GPU 0.7

Wybierz **Misja GPU — rozpocznij grę** albo `StrefaZero.exe --mission`. Zniszcz trzy tarcze i dotrzyj do zielonego wyjścia. Przestrzel zaporę oraz podporę silosu, aby uwolnić wodę i piasek. Postać, broń i materiały współdziałają na GPU; gracz chodzi po usypach, rozpycha ziarna i doświadcza wyporności. Pomarańczowe przegrody są zniszczalne. Sterowanie jak na poligonie; F5 odtwarza misję.

Aktywne ID oraz pośrednie uruchamianie obliczeń i rysowania zastępują przetwarzanie całej puli po użyciu pędzla. Modele, architektura, ograniczenia i pomiary: [GPU_MISSION_070.md](reports/GPU_MISSION_070.md). Testy: `--mission-test --profile <folder>` i `--mission-smoke-test --profile <folder>`.

## Laboratorium GPU 0.6.1

W menu wybierz **Laboratorium GPU — ciecze i ziarna** lub uruchom `StrefaZero.exe --gpu-laboratory`. Woda i olej korzystają z solvera PBF, a piasek, popiół i metalowe drobiny z kontaktów PBD. Krok czasu wynosi 1/120 s i zawiera dwa podkroki. Ruch oraz powierzchnie cieczy są interpolowane; cząstki nie przeskakują całymi rzędami siatki.

LPM rysuje, PPM usuwa, kółko zmienia pędzel, Spacja pauzuje, F wykonuje krok. Nowe sceny: **Klepsydra**, **Olej pod wodą**, **Kropla oleju**. Przegląd zmian i testy: [PHYSICS_REVIEW_061.md](reports/PHYSICS_REVIEW_061.md). Oprócz trzech scen ziaren i pustej planszy dostępne są zbiornik wody, przerwana zapora, naczynia połączone, olej na wodzie i piasek w wodzie. Limit 32768 obejmuje wspólnie ciecze i ziarna. Stan, edycja, wyszukiwanie sąsiadów i renderowanie pozostają na GPU. Panel podaje czas obliczeń karty, błąd gęstości cieczy oraz pominięty czas przy przeciążeniu.

Gazy, temperatura, reakcje oraz referencyjny poligon CPU pozostają w poprzednim modelu. Misja GPU korzysta ze wspólnego świata postaci i materiałów. Światy nie wymieniają jeszcze stanu. Ciecze GPU używają innego modelu niż CPU/MAC. Matematyka, ograniczenia, testy i pomiary: [GPU_LIQUIDS.md](reports/GPU_LIQUIDS.md). Historia ziaren 0.5: [GPU_MIGRATION.md](reports/GPU_MIGRATION.md).

Aplikacja próbuje utworzyć kontekst OpenGL 4.3 i wraca do 3.3, jeśli sterownik go nie udostępnia. `--opengl33` wymusza starszy kontekst do kontroli zgodności; tryb ziaren GPU jest wtedy niedostępny. Nie jest wymagana instalacja CUDA ani Vulkan.

## Wydajność i matematyka 0.4.2

Stała pula CPU, MIC(0), ograniczenie skanowania cieczy, pomijanie pustych kanałów i ponowne używanie buforów. Poprawione CFL, wymiana ciepła wielu sąsiadów, szczelne składowe ciśnienia i aktywacja gorącego paliwa. Testy porównują bitowo 1/2/4 wątki. Pomiary fizyki, źródła metod i ocena GPU: [PERFORMANCE_AUDIT.md](reports/PERFORMANCE_AUDIT.md).

Fizyka laboratorium pracuje w osobnym wątku, a okno dostaje niezmienne obrazy świata. Nie ma nadrabiania ośmiu kroków przed kolejną obsługą wejścia. Panel pokazuje faktyczne kroki fizyki/s osobno od FPS okna. Ciężka scena może spowolnić czas symulacji, ale nie zatrzymuje menu i kontrolek. Szczegóły i wyniki: [LAB_PERFORMANCE.md](reports/LAB_PERFORMANCE.md).
