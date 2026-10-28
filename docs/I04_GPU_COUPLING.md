# I04 — lokalne reakcje postaci i redukcja GPU

Data: 2026-10-28. Status: **wdrożone; regresje 21/21, okna combined 3/3; cel 1,5 ms GPU otwarty**. Poprzedni etap: [I03](I03_GPU_NEIGHBORS.md). [Plan](PLAN_ROZWOJU_GPU.md).

## Zakres i kompletność

Doprecyzowano proponowaną metodę I04: zamiast osobnej, buforowanej listy komórek wokół postaci zbieramy rzeczywiste reakcje podczas istniejących przebiegów fizyki. Każdy producent nadal odwiedza aktywne ID; kosztowna redukcja nie skanuje już całego świata. Bieżący test AABB obejmuje wszystkie próbki wnętrza kapsuły z promieniem jądra 2: x ±6,01, y od −20,01 do +2,01 względem stóp. Dalekie ciecze pomijają 25 testów próbek. Żaden punkt z niezerowym wkładem jądra nie leży poza tym obszarem. Korekty i nowa pozycja kapsuły są uwzględniane przy każdym producencie; nie ma listy geometrycznej, która mogłaby utracić ważność.

Producent zapisuje reakcję oraz dopisuje ID do zwartej kolejki, gdy XY lub kontakt podparcia Z są niezerowe. PBF i kontakt mają niezależne kanały, z tagiem `id + channel*N`. Dwa źródła odwiedzają każde ID najwyżej raz przed redukcją, więc pojemność 2N = 65536 jest pełną granicą świata, a nie limitem sąsiadów. Opór ma osobną, późniejszą partię. Licznik przepełnienia jest jawny; test sprawdza pełną pojemność i odrzucenie większego żądania przed zapisem.

GPU generuje argumenty indirect. Do jednej grupy reakcje sumuje jeden przebieg; większe listy mają równoległe sumy częściowe i finał jednej grupy. Rozmiary 64/128/256 obsługują do 1024 grup. Nie używamy atomowych sum float. Bariery SSBO i COMMAND rozdzielają produkcję, odczyt argumentów i konsumpcję. Zera grup pomijają pracę GPU; wywołania API pozostają. Puste partie nie powtarzają poprzednich reakcji.

Krok 1/120 s, dwa podkroki, iteracje i współczynniki materiałów pozostają bez zmian. `reference` zachowuje globalną redukcję I03, `local` zachowuje kolejność PBF → redukcja → kontakt → redukcja, a `combined` zbiera oba kanały i stosuje jedną redukcję po kontakcie. Wariant combined zmienia kolejność rozwiązania; po pełnych testach misji i porównaniu narzutu wybrano go jako domyślny. Local pozostaje wariantem porównawczym.

## Transport, reset i pamięć

Stan cząstek, SoA, stabilne ID i indeks I03 pozostają wspólne. Trzy kanały terms zajmują 3N vec4 w buforze samych danych float (1 572 864 B). Nagłówek 64 B, 2N tagów uint i 1024 vec4 sum częściowych umieszczono za indeksem przestrzennym w drugim, już istniejącym SSBO (278 592 B metadanych). Zachowano osiem wiązań. Referencja zachowuje stare 2N vec4 i nie alokuje metadanych kolejki. Dwa nowe argumenty dispatch dodają 24 B. Wszystkie osiem SSBO zajmuje 8 821 464 B, około 8,41 MiB: przyrost 802 904 B, około 0,77 MiB. Alokacja jest stała przy 3k/16k/32k i gęstych kontaktach; nie tworzymy tablicy wszystkich par.

Reset i ponowne skonfigurowanie misji zerują nagłówek oraz argumenty przed nową generacją. Snapshot HUD 272 B i pierścień I01 pozostają bez zmian. Liczniki nowej kolejki są czytane wyłącznie w walidacji i poza pomiarem benchmarku. Diagnostyczny odczyt argumentów korzysta ze świeżej kopii staging: bezpośredni GetBufferSubData na zużywanym buforze indirect zwracał na sprzęcie referencyjnym poprzednie argumenty. Kopia z GPU i brak ponownego wykonania pustej partii potwierdzają właściwy stan. Nie dodano oczekiwania CPU w pętli gry.

## Weryfikacja metody

Sumy GPU porównano z niezależną sumą double dla pustych list, końcówek grup, 32768 i 65536 reakcji, obu kanałów, podparcia przy zerowym XY, pustego ponowienia i resetu. Sprawdzono rzeczywiste argumenty zapisane przez GPU oraz liczbę grup każdego poziomu. Wariant lokalny porównano z dwoma niezależnymi uruchomieniami globalnej referencji, także po przestawieniu kapsuły. Sama referencja dała średnią różnicę cząstek 0,530 komórki, local 0,536; różnice agregatów wyniosły 0,0255 i 0,0079 komórki, a maksymalne różnice postaci 0,00043 i 0,00230. Test stosuje jawne tolerancje i kontroluje zmienność referencji zamiast wymagać identycznych trajektorii każdej cząstki w turbulentnym kontakcie.

Harness Windows czeka na rzeczywisty proces GUI i kończy go po limicie. Zwykłe zakończenie wrappera WSL nie gwarantowało zakończenia procesu gry po timeout; pierwsze próby i ich błędy zachowano w danych. Nowe pomiary uruchamiane są sekwencyjnie przez natywny nadzór.

## Pomiary kontrolowane

GTX 1650, NVIDIA 610.47, GL 4.3, Release MSVC `/W4 /WX`. Pięć scen, trzy warianty, trzy powtórzenia. Świeży świat, 120 ticków rozgrzewki, 240 pomiarowych; odwrócona kolejność wariantów w drugim powtórzeniu. Grupy timerów włączone we wszystkich wariantach. Odczyty diagnostyki przez kopię staging poza czasem kroku. `glFinish` i odbiór HUD co 16 ticków są granicą harnessu.

| Scena | Wariant | Mediany GPU [ms], 3 powtórzenia | p95 GPU [ms], 3 powtórzenia |
| --- | --- | --- | --- |
| mission | i03_reference | 3.052 / 3.224 / 3.133 | 3.769 / 4.787 / 3.328 |
| mission | local | 3.090 / 3.150 / 3.103 | 4.626 / 3.792 / 3.295 |
| mission | combined | 3.090 / 3.093 / 2.968 | 4.665 / 4.643 / 3.238 |
| stress_16k | i03_reference | 2.838 / 2.949 / 2.863 | 3.412 / 3.612 / 3.035 |
| stress_16k | local | 1.988 / 2.086 / 2.046 | 2.439 / 2.655 / 2.204 |
| stress_16k | combined | 2.191 / 2.031 / 1.991 | 3.387 / 2.692 / 2.225 |
| stress_32k | i03_reference | 5.675 / 5.825 / 5.644 | 6.584 / 6.655 / 5.894 |
| stress_32k | local | 3.889 / 3.836 / 3.758 | 5.611 / 4.907 / 4.094 |
| stress_32k | combined | 4.194 / 3.788 / 3.722 | 5.783 / 4.825 / 4.082 |
| mission_route | i03_reference | 3.174 / 3.200 / 3.180 | 3.864 / 3.980 / 3.252 |
| mission_route | local | 3.160 / 3.236 / 3.168 | 3.864 / 4.067 / 3.295 |
| mission_route | combined | 3.010 / 3.098 / 3.126 | 4.145 / 3.769 / 3.215 |
| coupled_tank | i03_reference | 4.504 / 4.001 / 3.736 | 6.045 / 4.530 / 4.184 |
| coupled_tank | local | 3.981 / 4.278 / 4.217 | 6.103 / 4.940 / 4.348 |
| coupled_tank | combined | 3.796 / 3.844 / 3.836 | 4.606 / 4.739 / 4.020 |

P95 CPU submit domyślnego wariantu combined:
- mission: 0.029 / 0.027 / 0.078 ms; p99 0.121 / 0.181 / 0.147 ms.
- stress_16k: 0.049 / 0.038 / 0.024 ms; p99 0.183 / 0.216 / 0.146 ms.
- stress_32k: 0.078 / 0.031 / 0.019 ms; p99 0.216 / 0.127 / 0.096 ms.
- coupled_tank: 0.051 / 0.080 / 0.072 ms; p99 0.202 / 0.131 / 0.116 ms.

Przykładowy zwykły tick misji mieszanej ma 130 dispatchy API w reference, 172 w local i 140 w combined. To odpowiednio 42, 42 i 26 logicznych redukcji; nowe warianty mają dwa zlecenia indirect na redukcję, z zerami dla pustej lub zbędnej pracy. Nie przedstawiamy kompakcji jako zmniejszenia liczby wywołań API. GPU zlicza rzeczywiście przetworzone reakcje, grupy oraz partie, osobno od żądań harmonogramu.

Cel 1,5 ms GPU/tick nie został osiągnięty. P95 CPU submit w końcowych seriach mieści się poniżej 0,1 ms; p99 ma jeszcze wyższe skoki. Warunki podczas pracy zmieniały się: oryginalny I03 we wstępnej serii miał około 13–16 ms, a końcowa referencja 3,05–3,22 ms. Nie łączymy tych różnych reżimów w jednym procencie poprawy. Surowe dane zawierają także szerokie ogony opóźnień. Nie przypisujemy ich automatycznie jednemu odczytowi ani nie uznajemy problemu z I00–I03 za naprawiony.

Pozostały koszt obejmuje globalne przebiegi PBF/PBD oraz sposób wykonywania zleceń sterownika. Następny eksperyment wydajności: ta sama trasa przy widocznym i ukrytym oknie, wraz z wyłączonymi timerami grup, oraz osobny pomiar producentów PBF i kontaktu. Umożliwi to oddzielenie kosztu solvera od reżimu okresowych spowolnień. I05 pozostaje etapem wspólnego silnika i zasobów.

Dla median z trzech końcowych powtórzeń combined skraca czas GPU dla 16k ziaren o około **29%**, a 32k o **33%**. Mała misja ma około **3,1 ms/tick**, a różnica względem referencji około 1,4% mieści się w rozrzucie prób. Dla zbiornika z kontaktami różnica median wynosi około 4%, również przy nakładających się zakresach. Nie deklarujemy dla tych dwóch scen potwierdzonego przyspieszenia.

Combined ma mniej zleceń niż pośredni local i przeszedł pełne testy misji, dlatego został domyślny. Końcowa referencja zachowuje globalny algorytm I03 we wspólnym buildzie; nie jest binarnie identycznym starym wydaniem. Oryginalna paczka została osobno uruchomiona jako kontrola.

Scena mission_route mierzy pierwsze 360 ticków i nie miała jeszcze reakcji postaci. Osobny coupled_tank wymusza niepuste kolejki w mierzonych 240 tickach. Liczby reakcji, maksimum w partii, sumy częściowe, p99/maksima GPU/CPU oraz fazy są w CSV.

## Regresje i okno

- **21/21 pełnego zestawu CTest przeszło na końcowym buildzie**, w tym misja, instrumentacja, kolejka, sąsiedztwo, ziarna, ciecze, przepływ, oba laboratoria oraz OpenGL 3.3.
- Osobno przeszły pełna misja globalnej referencji, instrumentacja referencji i audyt z KHR_debug. Wymuszony GL 3.3 zwrócił oczekiwane 77.
- Combined przeszedł także osobny pełny przebieg: woda utrzymuje postać, lżejszy olej pozwala jej opaść, piasek daje podparcie i pozwala chodzić. Trasy 30/60/144 FPS mają komplet celów i destrukcję materiałów.
- Wstępna wersja miała timeouty i błędy walidacji opisane wyżej; nie usunięto ich z surowych danych. Końcowe testy zachowują dotychczasowe limity sąsiedztwa, wykonania i testów okna. Przerwany wczesny przebieg local nie jest liczony jako pełny test.

Końcowy combined: **3/3 poprawne próby okna**, bez utraty czasu. Referencja: **2/3**; jedną próbę z błędem pozostawiono w tabeli. Wszystkie próby to 900 klatek przy limicie 60 FPS, VSync wyłączone. Pominięto pierwsze 30 klatek tylko w obliczeniu percentyli; CSV je zachowuje. CPU klatki jest sumą kosztu wszystkich ticków i odbioru HUD z tej samej klatki.

| Wariant / próba | Mediana klatki [ms] | p95 klatki [ms] | p95 łącznego CPU [ms] | Utracony czas [s] |
| --- | ---: | ---: | ---: | ---: |
| combined / 1 | 31.577 | 46.545 | 11.574 | 0.000 |
| reference / 1 | 31.612 | 46.476 | 11.414 | 0.000 |
| combined / 2 | 31.597 | 46.540 | 4.920 | 0.000 |
| reference / 2 | 31.698 | 46.626 | 16.617 | 0.192 |
| combined / 3 | 31.633 | 46.596 | 14.618 | 0.000 |
| reference / 3 | 31.770 | 46.649 | 19.956 | 0.000 |

Dodatkowa para z widocznym oknem przeszła dla reference i combined. To pojedyncza kontrola, nie trzy powtórzenia. Ograniczone do 60 FPS próby nie mierzą przyrostu maksymalnego FPS. Osobnego pełnego benchmarku klatki bez limitu FPS nie wykonano. Referencja w próbie 2 ukończyła cele, ale utraciła 0,191667 s czasu i osiągnęła p95 klatki 46,63 ms. Kontrole niezmienionego I03 (oba zwykłe okna, laboratorium i misja) w późniejszym uruchomieniu przeszły. Historia okresowych spowolnień pozostaje otwarta.
Widoczne reference: mediana 33.333 ms, p95 34.297 ms, utracony czas 0 s.
Widoczne combined: mediana 33.325 ms, p95 34.702 ms, utracony czas 0 s.

Paczka Windows I04 przeszła audyt sprzężenia z własnego katalogu; GL 3.3 zwrócił 77. EXE, DLL i wszystkie shadery porównano bajtowo z buildem i źródłami. SHA-256 i wyniki kontroli są przy paczce oraz danych. KHR_debug w dodatkowym audycie zapisał 256 komunikatów z limitem logu oraz 95 dalszych pominiętych; dotyczyły m.in. migracji bufora stanu misji po synchronicznym odczycie walidacji. Audyt nie zgłosił błędu GL, ale log nie jest pełnym zapisem wszystkich komunikatów.


Wstępny układ łączył duży stały wektor terms i metadane atomowe w jednym bloku. Testy sąsiedztwa i wykonania przekroczyły wtedy swoje limity 120 s. Po rozdzieleniu danych i ponownym sprawdzeniu cztery kluczowe testy przeszły także przy pierwszej kompilacji nowych wariantów. Rejestrowano tworzenie świata około 11–15 s przy pierwszych kompilacjach i około 0,06–0,1 s przy rozgrzanym cache. Stare timeouty zachowano; koszt kompilacji nie jest czasem kroku fizyki.

## Odtwarzanie

```powershell
.\StrefaZero.exe --gpu-coupling-check --profile .\coupling-tests
.\StrefaZero.exe --gpu-coupling-profile --profile .\coupling-benchmark
.\StrefaZero.exe --mission --gpu-coupling combined
.\StrefaZero.exe --mission --gpu-coupling local
.\StrefaZero.exe --mission --gpu-coupling reference
.\scripts\profile-coupling.ps1 -Executable '<gra>\StrefaZero.exe' -Output '<wyniki>' -IncludeWindows
```

`combined` jest domyślny. `local` zachowuje sekwencyjne zastosowanie reakcji, a `reference` globalną redukcję I03. Przełącznik całego zestawu `--gpu-execution` należy podawać przed szczegółowymi opcjami. Skrypt Windows czeka na natywny proces. Benchmark i pełny audyt poprawności są osobnymi poleceniami.

Surowe dane: [gpu-i04](gpu-i04/). Kolejny zakres: **I05 — jeden silnik i współdzielone zasoby**. Cel GPU i historia spowolnień pozostają jawnie otwarte.
