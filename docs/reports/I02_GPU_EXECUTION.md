# I02 — przebiegi, wiązania i kolejki pomiarów GPU

Data: 2026-10-28. Poprzedni etap: [I01](I01_GPU_READBACK.md). [Plan iteracji](../plans/PLAN_ROZWOJU_GPU.md).

## Zakres

Zachowano krok 1/120 s, dwa podkroki kontaktów, liczbę iteracji i model sił. I02 zmienia sposób zlecania tej samej fizyki. Budowa sąsiedztwa i globalna redukcja reakcji postaci pozostają do I03–I04.

- Z tych samych źródeł GLSL powstaje **27 programów compute**: 14 dla ziaren i danych pomocniczych, 5 dla cieczy, 8 dla misji. Definicje `PASS_*`, `PASS_OPERATION` i rozmiary grup są wstawiane po `#version`. Operacja jest stałą czasu kompilacji. Błędy kompilacji zawierają nazwę pliku i definicje wariantu.
- Lokalny tracker wiąże osiem SSBO na początku harmonogramu, potem zmienia tylko potrzebne wiązania ping-pong i program. Przy wejściu do kolejnej publicznej operacji obliczeniowej unieważniamy tracker. Nie zakłada on wyłącznego dostępu do kontekstu pomiędzy światem, rendererem i ImGui.
- Timery fizyki, renderu i grup przebiegów mają osobne FIFO. Sprawdzamy najstarszy oczekujący wynik i kończymy na pierwszym niegotowym. Pełne kolejki pomijają nowy pomiar zamiast nadpisywać query. Generacja chroni nowy świat przed czasami starej sceny.
- GPU generuje dodatkowe argumenty indirect dla przebudowy aktywnych ID, synchronizacji obu kopii cząstek, pola terenu, wybuchów, gruzu i pustej redukcji. Zera ograniczają pracę urządzenia; **wywołanie API nadal występuje**. Harmonogram nie podejmuje tych decyzji na podstawie spóźnionych liczników CPU.
- Impuls wybuchu aktualizuje tylko własne ID w miejscu. Dzięki temu brak wybuchu może wykonać zero grup bez przełączenia na starą kopię ping-pong. Sąsiedztwo i pozostałe przebiegi Jacobi zachowują oddzielny odczyt i zapis.

Bufor aktywnych/wolnych ID ma teraz 28 słów komend zamiast 7: **84 B dodatkowych danych na świat**. ABI cząstki i snapshotu misji pozostaje bez zmian. Liczba programów i koszt ich inicjalizacji wzrosły; współdzielenie zasobów dwóch światów należy do I05.

## Zależności i bariery

Nie usuwamy barier między kolejnymi iteracjami solvera ani budową siatki. Łączymy dotychczasową dodatkową barierę po finalizacji aktywnych ID z barierą tego przebiegu.

| Producent → konsument | Dane | Synchronizacja |
| --- | --- | --- |
| Początek ticka → wybuchy / gruz | Stan gry, komendy indirect | SSBO + COMMAND |
| Niszczenie terenu → konwersja gruzu | Wytrzymałość komórek, licznik oczekujących | SSBO |
| Konwersja gruzu → inicjalizacja przebudowy | Nowe ID, teren, znacznik zmian | SSBO |
| Inicjalizacja przebudowy → zbieranie / kopia / SDF | Komendy warunkowej pracy, liczniki | SSBO + COMMAND |
| Zbieranie ID → finalizacja | Aktywne i wolne ID, liczby materiałów | SSBO |
| Finalizacja → cząstki / redukcja / rysowanie | Komendy dispatch i draw | SSBO + COMMAND |
| Kopia członkostwa → kolejne przełączenia ping-pong | Obie kopie cząstek | SSBO |
| Solver / siatka → kolejny przebieg | Pozycje, head/link, składniki sił | SSBO |
| Shadery → kopia do pierścienia HUD | Liczniki i snapshot | BUFFER_UPDATE przed kopią; dalszy kontrakt I01 |

Komenda gruzu jest konserwatywna: dopuszcza pracę również wtedy, gdy bieżący wybuch **może** dopiero utworzyć oczekujące komórki. Sam brak oczekującego gruzu na początku ticka nie może zablokować jego konwersji po zniszczeniu ściany w tym samym ticku. Przy pełnej puli komórki pozostają oczekujące zgodnie z dotychczasowym kontraktem.

Podstawy kontraktów: [ARB_compute_shader — dispatch, zera i argumenty indirect](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_compute_shader.txt), [bariery pamięci](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_shader_image_load_store.txt), [widoczność SSBO](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_shader_storage_buffer_object.txt), [ARB_timer_query](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_timer_query.txt). `barrier()` synchronizuje redukcję wewnątrz jej jednej grupy, nie zastępuje barier między dispatchami.

## Warianty i odtwarzanie

```powershell
# Macierz poprawności: pusta/pełna pula, końcówki grup, kolejki i edycje.
.\StrefaZero.exe --gpu-execution-check --profile .\execution-tests

# Trzy powtórzenia kontrolowanego benchmarku.
.\StrefaZero.exe --gpu-execution-profile --profile .\execution-benchmark

# Opcjonalna seria okien obu harmonogramów.
.\scripts\profile-execution.ps1 -Executable '<gra>\StrefaZero.exe' -Output '<wyniki>' -IncludeWindows

# Diagnostyczne odtworzenie starego sposobu zlecania pracy.
.\StrefaZero.exe --mission --gpu-execution reference
```

`--gpu-execution optimized` jest domyślne. `--gpu-local-size 64|128|256` i `--gpu-reduction-size 64|128|256` służą do strojenia. Ubershadery wymagają takiego samego rozmiaru grupy dla pracy cząstek i redukcji; rozdzielone programy pozwalają stroić je osobno. Parametry trybu i rozmiary grup trafiają do liczników testu okna.

`reference` to ścieżka porównawcza w tym samym buildzie: ubershadery, pełne wiązania, skanowanie timerów i dotychczasowe warunki wewnątrz shaderów. Korzysta z tego samego transportu I01 i wspólnego refaktoru barier/ABI komend; nie jest binarnie identycznym wydaniem I01. Warianty `bindings`, `passes`, `fifo` dodają kolejne zmiany, a `optimized_*` włącza też warunkowe komendy GPU.

Benchmark ma świeży świat dla każdego wariantu, 120 ticków rozgrzewki i 240 pomiarowych. Trzy powtórzenia odwracają kolejność wariantów co drugą serię. `glFinish()` i zewnętrzny odczyt HUD co 16 ticków są granicą harnessu, poza czasem kroku CPU. Raport zawiera również koszt odczytu i średnią całej partii. Znaczniki czasu grup są włączone we wszystkich porównywanych wariantach.

## Wyniki kontrolowane

GTX 1650, NVIDIA 610.47, GL 4.3. Mediany czasu GPU kroku w trzech powtórzeniach [ms]:

| Wariant | Misja 3096 | 16 tys. ziaren z postacią | 32768 ziaren z postacią |
| --- | --- | --- | --- |
| reference | 3,630 / 3,663 / 3,734 | 3,233 / 3,256 / 3,254 | 6,218 / 6,244 / 6,253 |
| bindings | 3,643 / 3,635 / 3,658 | — | — |
| passes | 3,370 / 3,413 / 3,461 | — | — |
| fifo | 3,369 / 3,382 / 3,414 | — | — |
| optimized, grupa 256 | 3,409 / 3,385 / 3,398 | 2,922 / 2,913 / 2,917 | 5,726 / 5,726 / 5,753 |
| optimized, grupa 128 | 3,459 / 3,472 / 3,418 | 2,959 / 2,935 / 2,944 | 5,800 / 5,785 / 5,859 |
| optimized, grupa 64 | 3,387 / 3,387 / 3,417 | 2,978 / 2,970 / 2,986 | 5,820 / 5,803 / 5,844 |
| grupa 256, redukcja 128 | 3,600 / 3,715 / 3,596 | — | — |
| grupa 256, redukcja 64 | 3,804 / 3,868 / 3,852 | — | — |

Dla median z trzech powtórzeń zysk wynosi około **7% dla misji, 10% dla 16 tys. ziaren i 8% dla pełnej puli**. Największą mierzalną zmianę GPU daje specjalizacja programów. Sam tracker wiązań odciąża głównie CPU. Warunkowych komend nie przedstawiamy jako osobnego, potwierdzonego przyspieszenia całej misji względem wariantu `fifo` — różnice są małe.

Domyślnie pozostaje **256 wątków dla pracy cząstek i redukcji**. Grupa 64 była bliska 256 w małej misji, ale wolniejsza w obu większych scenach. Redukcje 64/128 pogorszyły koszt. Przebiegi skalarne mają własne programy z jednym wątkiem.

P95 CPU submit dla misji: reference **0,073 / 0,084 / 0,082 ms**, optimized 256 **0,071 / 0,066 / 0,054 ms**. To czas zlecania kroku, nie całej klatki ani zewnętrznego odczytu HUD.

Przykładowy tick 122, 3096 cząstek, pierwsze powtórzenie, ten sam harness:

| Licznik | reference | optimized 256 |
| --- | ---: | ---: |
| Wszystkie wywołania GL | 2662 | 740 |
| Wiązania SSBO | 1632 | 108 |
| Wiązania buforów ogólnych | 109 | 3 |
| Użycia programów | 290 | 204 |
| Aktualizacje uniformów | 209 | 3 |
| Dispatch API | 204 | 204 |
| W tym indirect | 107 | 154 |

Liczba wywołań GL spadła o około **72%**. Nadal zlecamy 204 dispatchy w zwykłym kroku i 208 w kroku diagnostycznym. Siatka jest budowana 42 / 43 razy na tick; I02 tego nie zmienia. Do celu 1,5 ms GPU nadal brakuje optymalizacji sąsiedztwa i sprzężenia postaci.

W całym pierwszym powtórzeniu misji p95 liczby odpytań dostępności query na tick zmalało z **30 do 2** po włączeniu FIFO. W ticku 122 z tabeli oba warianty miały akurat tylko dwa oczekujące wyniki. To porównanie całej serii, nie dopisanie innej wartości do tego przykładowego ticka.

## Kontrola pełnych klatek

Trzy powtórzenia dla reference i optimized, odwrócona kolejność w drugim powtórzeniu, 900 klatek każdej próby. Wszystkie **6/6** zakończyły misję z wynikiem 3, bez utraty czasu zegara. Tabela pomija pierwsze 30 klatek rozgrzewki; surowe CSV zachowuje je.

Łączny koszt CPU to `frame_step_submit_ms + ui_poll_ms` z tej samej klatki. Uwzględnia wszystkie ticki i zewnętrzny odczyt HUD, więc wynik nie opiera się na przeniesieniu pracy poza licznik ticka.

| Wariant / powtórzenie | Mediana klatki [ms] | p95 klatki [ms] | Mediana łącznego CPU [ms] | p95 łącznego CPU [ms] |
| --- | ---: | ---: | ---: | ---: |
| reference / 1 | 16,669 | 16,673 | 0,109 | 0,131 |
| optimized / 1 | 16,669 | 16,824 | 0,077 | 0,111 |
| reference / 2 | 16,668 | 16,674 | 0,102 | 0,127 |
| optimized / 2 | 16,669 | 16,670 | 0,076 | 0,097 |
| reference / 3 | 16,668 | 16,695 | 0,106 | 0,131 |
| optimized / 3 | 16,668 | 16,670 | 0,073 | 0,093 |

Przy limicie 60 FPS mediana klatki nie przyspiesza. Wyniki potwierdzają mniejszy koszt CPU w tej serii, nie rozwiązanie historycznego, okresowego spowolnienia GPU z I00–I01. Tutaj nie wystąpił reżim około 22 ms na tick; brak jego wystąpienia nie identyfikuje przyczyny ani nie dowodzi jej usunięcia.

## Weryfikacja i odbiór

- Release MSVC `/W4 /WX`: kompilacja poprawna.
- **12/12 wybranych regresji przeszło**: zegar, ustawienia, misja GPU z trasami 30/60/144 FPS, instrumentacja, nowy test execution, pierścień odczytów, okno misji, ziarna, ciecze, przepływ, okno laboratorium GPU i CPU/GL 3.3. Zarejestrowanych jest 19 testów; nie deklarujemy ponownego uruchomienia wszystkich trybów CPU.
- Osobno przeszła pełna misja i instrumentacja w trybie reference.
- Testy wielkości grup obejmują 0, L−1, L, L+1 i 32768 aktywnych ID; sprawdzają też, że każde ID faktycznie wykonało krok grawitacji.
- Kolejki query rzeczywiście się przepełniły: w seriach po 70 ticków przyjęto 32–33 pomiary i jawnie pominięto pozostałe. Wszystkie przyjęte wyniki odebrano; stare generacje odrzucono.
- Wymuszony GL 3.3 poprawnie pominął nowy test GPU kodem 77.
- `glGetError()` w teście execution nie zgłosił błędu. KHR_debug w walidacji zapisał powtarzalne ostrzeżenia o migracji bufora active/free IDs po jego synchronicznym odczycie. Log ma limit 256 wpisów i zgłosił 1353 dalsze pominięte komunikaty; nie jest pełnym zapisem wszystkich komunikatów sterownika. Benchmark używa świeżych buforów i nie czyta synchronicznie komend podczas pomiaru.
- Seria okien: **6/6 poprawnych prób**, bez zmiany kryteriów zegara.
- Paczka Windows I02 przeszła test execution z nowej lokalizacji; GL 3.3 poprawnie zwróciło 77. EXE i shadery porównano z buildem i źródłami, a sumy SHA-256 zapisano przy paczce i danych.

Nowe testy sprawdzają rzeczywiste komendy zapisane przez GPU, wszystkie aktywne ID po kroku grawitacji, końcówki grup, pełną pojemność, FIFO, reset, wybuch na pustej scenie i konwersję gruzu w tym samym ticku, brak odradzania usuniętych ID oraz zgodność impulsu w miejscu ze ścieżką referencyjną.

Surowe dane: [gpu-i02](../measurements/gpu-i02). I02 jest wdrożone i odebrane w zakresie harmonogramu, specjalizacji i FIFO. Pędzel zachowuje pełną ścieżkę przebudowy jako granicę edycji; warunkowa praca dotyczy automatycznego przygotowania ticka misji. Wyników kontrolowanego harnessu nie traktujemy jako gwarancji FPS.

Następny zakres po odbiorze: **I03 — uporządkowanie danych i kompletne sąsiedztwo z bezpiecznym marginesem ruchu**.
