# I04.5-B — transport i własność buforów GPU

Prace: 2026-10-29–30. Baza: lokalne I04.5-A/F/C na `6f318ea`. Historyczne [A/F](I04_5_GPU_STABILIZATION.md) i [C](I04_5C_GPU_WORK.md) pozostają niezmienione.

Zakończono implementację i porównanie trzech ścieżek transportu. **Pełne QA 28/28 PASS; seria 36/36 procesów bez błędu.** Nie uzyskano spójnego zysku uzasadniającego zmianę ustawień domyślnych: `storage=direct`, `phase_gating=false`, `skip_zero_projection=false`. Nowe ścieżki są eksperymentalne. Cel 1,50 ms/tick i odbiór całego I04.5 pozostają otwarte.

## Baza QA przed zmianą

Powtórzono cztery nieudane testy z C na zachowanym buildzie C, bez zmiany limitów ani tolerancji. `app.opengl_smoke` przeszedł w 29,61 s, `gpu.coupling` w 8,26 s, a `app.opengl33_smoke` w 29,53 s. `gpu.mission_ui` ponownie przekroczył 60 s. Wynik **3/4**, kod CTest 8, 127,64 s. [Log](../measurements/gpu-i04-5b/baseline-ctest.log), [JUnit](../measurements/gpu-i04-5b/baseline-ctest.xml).

To nie dowód naprawy wcześniejszej zmienności. Poprzednie błędy pozostają w raporcie C; jego kontrole odtworzyły także na niezmienionym A błąd tolerancji lokalnego sprzężenia i utratę czasu misji. Nie poluzowano warunków odbioru i nie zmieniono ustawień systemu ani sterownika.

## Co zmieniono

Zwykłe sterowanie misją już korzystało z uniformów. Zapisy CPU do SSBO dotyczą głównie ładowania/resetu, edycji i zewnętrznych eksplozji. Badanie obejmuje więc także inicjalizację świata, nie wyłącznie koszt pojedynczego ticka.

| Opcja `--gpu-storage` | Bufory świata | Zapis danych CPU |
| --- | --- | --- |
| `direct` | dotychczasowe mutable `GL_DYNAMIC_COPY` | `BufferSubData` bezpośrednio do bufora świata |
| `staged` | mutable, GL 4.3 | osobny `UploadRing` i `CopyBufferSubData` |
| `immutable` | `BufferStorage` z **flags=0** | wyłącznie kopie z `UploadRing`; wymagane ARB_buffer_storage lub GL 4.4 |
| `auto` | immutable, gdy dostępne; inaczej staged | ten sam pierścień; efektywny tryb zapisywany w metadanych |

To inny eksperyment niż odrzucone w C `BufferStorage(GL_DYNAMIC_STORAGE_BIT)` z bezpośrednimi aktualizacjami CPU. Brak `DYNAMIC_STORAGE_BIT` blokuje takie aktualizacje, pozostawiając dozwolone kopie po stronie serwera. Brak `MAP_*` i `CLIENT_STORAGE_BIT` nie gwarantuje miejsca alokacji w VRAM. Podstawa kontraktu: [specyfikacja Khronos ARB_buffer_storage](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_buffer_storage.txt).

Pierścień ma trzy sloty i używa funkcji GL 4.3. Fence dotyczy zakończenia kopii ze slotu. Przy ponownym użyciu wykonywany jest pojedynczy poll z timeoutem 0; zajęty slot dostaje nowy magazyn przez `BufferData`, a stary pozostaje obsługiwany przez GL do zakończenia wcześniejszych poleceń. Nowy magazyn ma rozmiar bieżącego payloadu, więc komenda 16 B nie powoduje ponownej alokacji dawnego uploadu cząstek 2 MiB. Nie ma pętli oczekiwania CPU ani cichego pomijania komend. Poszczególne wywołania GL nadal mogą czekać wewnątrz sterownika.

Kopie mają bariery dla aktualizacji buforów, shader storage i komend indirect. Nie dodano wiązania SSBO: osiem wiązań świata pozostaje zachowane. Nie zmieniono shaderów, równań fizyki ani kolejności kroków solvera. Odczyty HUD nadal mają własny pierścień i generacje; odczyty walidacyjne są wykonywane poza pomiarem.

`UploadStats` zapisuje liczbę payloadów, bajty, kopie, poll fence, orphaning, alokacje oraz bieżący i szczytowy rozmiar magazynów trzech nazw buforów. Nie mierzy pamięci starych magazynów zatrzymanych przez sterownik ani fizycznej rezydencji VRAM. Licznik obserwowanych wywołań obejmuje teraz także opcjonalne `BufferStorage`; historycznych totals nie należy bez korekty porównywać z nową inicjalizacją.

## Walidacja

`--gpu-transport-check` sprawdza:

- nadpisywanie zakresów i offsety, obroty pierścienia, natychmiastową zmianę danych klienta po powrocie z uploadu oraz wymuszoną zajętość fence;
- rzeczywiste `BUFFER_IMMUTABLE_STORAGE=true` i `BUFFER_STORAGE_FLAGS=0`, gdy wybrano immutable;
- automatyczny fallback z wyłączoną flagą opcjonalnej możliwości w loaderze; jest to kontrola ścieżki GL 4.3 na bieżącym sprzęcie, nie test drugiego sterownika;
- zgodność stanu małej sceny z direct, stabilne ID/materiały, paint/erase, pełne 32768 slotów, reset, pomocnicze bufory walidacji i dwie komendy eksplozji;
- zero kopii HUD od tworzenia świata przy okresie 0.

Walidacja wstępna oraz końcowa przeszły. Cztery tryby świata dały maksymalny błąd stanu **0**; każdy wykonał 64 uploady. Osobne testy pierścienia wymusiły po 10 przypadków orphaningu na ścieżkach staged/immutable/auto. Instrumentacja świata `auto` również przeszła z trzema trybami timerów i kontrolą początkowej polityki profilera. [Log transportu](../measurements/gpu-i04-5b/transport-final/transport-tests.txt), [instrumentacja](../measurements/gpu-i04-5b/profile-auto/instrumentation-tests.txt).

## Pomiar i odtwarzanie

```powershell
.\StrefaZero.exe --gpu-transport-check --profile .\transport-check
.\scripts\profile-transport.ps1 -Executable '<gra>\StrefaZero.exe' -Output '<nowy katalog>' -StorageModes direct,staged,immutable -ReadbackPeriods 2,0 -Repetitions 6 -SkipWindows
python .\scripts\analyze-stabilization.py --input '<wyniki>'
```

Protokół: sześć warunków (3 ścieżki × okres HUD 2/0), sześć powtórzeń, pełny rotowany porządek Latin, osobny natywny proces dla każdego warunku. Każda scena ma świeży świat, 120 ticków rozgrzewki, 240 pomiarowych i 120 próbek renderu. Timer elapsed, liczniki włączone, sprzężenie oraz diagnostyka aktywne. Render 960 × 576, GTX 1650, Release MSVC `/W4 /WX`, IPO. Drain i poll HUD co 16 ticków są poza CPU submit, ale w czasie ściennym przepustowości. Czas tworzenia świata jest zapisany osobno.

Pierwsza seria nie ma raportu zakończenia: ukończyła 11/36 procesów z kodem 0. Zachowano ją w `throughput/`, ale nie włączono do tabeli pełnej serii. [Proweniencja przerwania](../measurements/gpu-i04-5b/interrupted-series.json). Nowa seria jest w `throughput-complete/`.

Analyzer schema 4 sprawdza żądany i efektywny storage oraz okres odczytów; `auto` dopuszcza wyłącznie faktyczne staged lub immutable. Niezgodna lub brakująca efektywna konfiguracja unieważnia porównanie. Pozostaje walidacja kodu wyjścia, timeoutu, timerów i wariantów C. Składnia PowerShell 5.1 i testy kontrolne analyzera przeszły. [Składnia](../measurements/gpu-i04-5b/script-syntax.json), [kontrole analyzera](../measurements/gpu-i04-5b/analyzer-fixture-checks.json).

### Wyniki pełnej serii

**36/36 procesów zakończyło się kodem 0, bez timeoutu; analyzer zwrócił 648 wierszy i zero ostrzeżeń.** Wszystkie procesy używały tego samego EXE. Poniżej mediana sześciu median GPU, ms/tick; nie jest to percentyl próbki połączonej.

| Storage / okres HUD | Misja | 16k | 32k | Misja p95 | Misja CPU submit mediana / p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| direct / 2 | 2,90990 | 1,79147 | 3,21694 | 3,39459 | 0,00615 / 0,01800 |
| staged / 2 | 2,84339 | 1,79686 | 3,22942 | 3,38203 | 0,00620 / 0,02240 |
| immutable / 2 | 2,85688 | 1,80115 | 3,21858 | 3,37474 | 0,00610 / 0,02355 |
| direct / 0 | 2,85048 | 1,80626 | 3,23179 | 3,37046 | 0,00610 / 0,01405 |
| staged / 0 | 2,86410 | 1,79738 | 3,21230 | 3,38842 | 0,00610 / 0,01420 |
| immutable / 0 | 2,85029 | 1,80310 | 3,22632 | 3,43347 | 0,00615 / 0,01405 |

Przy okresie 2 ścieżki staged/immutable mają medianę misji krótszą o około 2,29%/1,82%, lecz nie utrzymują przewagi we wszystkich scenach i przy okresie 0. Zakres median misji direct/2 wyniósł 2,78118–2,92250 ms, staged/2 2,80029–2,87514, immutable/2 2,83552–2,92125. Nie wykonano testu istotności; nie traktujemy niewielkich różnic jako ustalonego przyspieszenia. [Pełne statystyki i zakresy](../measurements/gpu-i04-5b/comparison-summary.json), [kolejność i status procesów](../measurements/gpu-i04-5b/throughput-complete/runs.csv).

Mediana czasu tworzenia cached świata misji: direct/2 **69,13 ms**, staged/2 **70,30 ms**, immutable/2 **70,09 ms**. To pomiar inicjalizacji ze znanymi już shaderami w cache sterownika, nie zimnej kompilacji. Osiem buforów świata zajmuje 8 821 464 B. Nowe ścieżki w tej serii utrzymywały do 2 343 184 B uploadu dla misji i osiągnęły szczyt 4 440 064 B magazynów staging; direct nie alokuje ich wcale. Oba warianty mają identyczny zakres wiązań SSBO i dispatchy. Przy okresie 2 licznik HUD misji wynosił 73 zlecone kopie w każdym procesie, przy okresie 0 dokładnie zero, również od tworzenia świata. [Dane inicjalizacji i transportu](../measurements/gpu-i04-5b/transport-accounting.json).

### Powrót szybszego reżimu bez zmiany EXE

Niekompletna seria z 29 października miała mediany misji **12,0465–12,5235 ms**. Pełna seria z 30 października: **2,7812–2,9553 ms**. Hash EXE w obu seriach jest identyczny (`dec2c499…1387526b`), a wszystkie 17 plików shaderów ma identyczne hashe jak w C. Nie można przypisać tej dużej zmiany kolejnej kompilacji lub samemu wdrożeniu uploadu.

Dla direct/2 mediana zegara grafiki z telemetrii całych procesów zmieniła się z 1395 do 1890 MHz, a mocy z 18,62 do 40,835 W. Telemetria obejmuje również inicjalizację i walidację, nie tylko mierzone ticki. Jest to korelacja warunków, nie ustalenie przyczyny spowolnienia. Nie zmieniano ustawień systemu ani sterownika. Nie mieszamy obu reżimów w jedną średnią. [Porównanie sesji i hashy](../measurements/gpu-i04-5b/regime-comparison.json), [audyt shaderów](../measurements/gpu-i04-5b/shader-source-audit.json).

## Końcowy odbiór

**Pełny CTest: 28/28 PASS, kod 0, 231,85 s.** Przeszły nowe `gpu.transport` i `gpu.transport_mission` z automatycznie wybraną ścieżką immutable, wszystkie testy C, domyślna misja, lokalne sprzężenie, oba UI, GL 3.3 i testy CPU. Zachowano istniejące limity i tolerancje. [Log](../measurements/gpu-i04-5b/ctest.log), [JUnit](../measurements/gpu-i04-5b/ctest.xml), [podsumowanie](../measurements/gpu-i04-5b/ctest-summary.json).

Sukces w obecnej sesji nie unieważnia wcześniejszych błędów ani nie dowodzi usunięcia historycznej zmienności sprzężenia. Baza C i jej nieudane próby pozostają historycznie opisane. Kolekcja QA pomija starsze pliki z ponownie używanych katalogów; [proweniencja kolekcji](../measurements/gpu-i04-5b/qa/collection-provenance.json) podaje wykluczenia.

Dodatkowe dwie kontrole widocznego okna z KHR_debug i licznikami również przeszły: po 900 klatek, 1798 ticków, wynik misji 3, bez utraty czasu. Faktyczny fokus był aktywny we wszystkich 870 analizowanych klatkach po rozgrzewce; brak minimalizacji.

| Kontrola | Mediana pełnej klatki | p95 | p99 | Maksimum |
| --- | ---: | ---: | ---: | ---: |
| direct | 16,6686 ms | 16,6693 ms | 16,7198 ms | 46,7086 ms |
| auto → immutable | 16,6685 ms | 16,6693 ms | 16,7172 ms | 40,9844 ms |

Statystyki pełnej klatki pochodzą z `frame-timings.csv`, po pominięciu pierwszych 30 klatek; obejmują również testowe przechwytywanie obrazu w pętli. Końcowa walidacja po pętli jest osobną fazą. To jedna para diagnostyczna, nie sześć wymaganych prób odbioru całego I04.5. [Pełne klatki](../measurements/gpu-i04-5b/debug-windows/full-frame-summary.json), [wyniki procesów i misji](../measurements/gpu-i04-5b/debug-windows/comparison.json).

W obu logach odebrano po jednym komunikacie `id=131186` przy walidacji; zero w fazie inicjalizacji i runtime. Logger nie zgubił komunikatów. Faza oznacza moment odbioru callbacku; asynchroniczna dostawa może opisywać wcześniejszą pracę. Nie wyciszano callbacku i nie uznajemy braku komunikatu w runtime za dowód stałego położenia danych w VRAM.

Paczka `StrefaZero-Windows-x64-I04_5-B` przeszła własny `--gpu-transport-check` i `--gpu-profile-check --gpu-storage auto` (oba kod 0), a wymuszony `--opengl33 --gpu-transport-check` zwrócił oczekiwane 77. EXE i assety porównano z finalnym buildem/źródłami. Paczka ma własny manifest SHA256 i sprawdzenie CRC ZIP; jest kompilacją diagnostyczną, bez nowego tagu wydania. [Transport paczki](../measurements/gpu-i04-5b/package-transport-check/transport-tests.txt), [instrumentacja](../measurements/gpu-i04-5b/package-profile-auto/instrumentation-tests.txt), [fallback](../measurements/gpu-i04-5b/package-gl33-skip/native-result.json), [zapis źródeł i EXE](../measurements/gpu-i04-5b/source-state.json).

**Decyzja:** zachowujemy `direct` jako domyślne. `staged`, `immutable` i `auto` pozostają opcjami do badań, z pełną kontrolą poprawności i jawnie raportowaną pamięcią. Zysk niewielkiej mediany w jednym warunku nie równoważy braku spójnej przewagi i dodatkowej pamięci. Obie opcje C nadal OFF.

Badanie B i implementacja transportu są zakończone jako eksperyment. Nadal nie zamknięto przyczyny zmiennych warunków pracy, celu 1,50 ms ani odbioru sześciu porównywalnych okien całego I04.5. Następny zakres to przypisanie kosztu obecnych przebiegów przed decyzją o warunkowym D/E; nie zakładamy, że sortedHot lub lokalny Dmax na pewno pomoże. I05 nadal zależy od oceny całego I04.5. Polecenia i deklaracje z dostarczonego review pozostają materiałem do weryfikacji, a nie samodzielną zgodą ani własnym wynikiem QA.
