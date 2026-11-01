# I11 — światło, HDR i wygląd piasku

Data: 2026-10-31. Wdrożenie na zachowanej roboczej bazie I10. **Oświetlenie pozostaje domyślnie `off`; odbiór kosztu i aktywnego okna jest prowadzony osobno od poprawności.** I10 i wcześniejsze otwarte budżety nie zostają przez to odebrane.

## Potok i zakres

Misja oraz laboratorium GPU korzystają z tego samego `SceneRenderer`, biblioteki programów i stanu prezentacji. Woda zachowuje własne `basic|standard|high`; nowe światło ma niezależne `off|low|high`. CPU i OpenGL 3.3 korzystają z dotychczasowej ścieżki.

| Przebieg | Zasoby / kontrakt |
| --- | --- |
| Prezentacja ticka | Pole woda/olej/piasek, wilgoć i światła; raz na wykonany tick 120 Hz |
| Scena | Liniowy RGBA16F, teren, ziarna, aktorzy, zanurzone VFX |
| Światła | RGBA16F 320×192; 16 źródeł low / 32 high; odbiorniki są mnożone w przestrzeni liniowej |
| Cienie | Dokładne supercover DDA; high dodaje pomocnicze JFA+1 i miękki brzeg |
| Ciecz, termika, VFX | Kompozycja w RGBA16F; dystorsja czyta osobną kopię tego samego formatu |
| Bloom | Dual Kawase, low 240×144 i 120×72; high dodatkowo 480×288 |
| Obraz końcowy | Tonemapping ACES fitted z korektą ciemnych tonów, LUT 16³, pojedyncze kodowanie sRGB, Bayer 4×4 i RGBA8 |
| Oznaczenia / UI | Tarcze, HP i wyjście po postprocessie; potem ImGui i ostry celownik |

Kolorowe tekstury i stałe koloru są dekodowane dotychczasowymi funkcjami shaderów. Pośrednie bufory HDR nie są kodowane sRGB. `GL_FRAMEBUFFER_SRGB` pozostaje wyłączone: końcowy shader wykonuje kodowanie raz. Użyta odmiana [krzywej Narkowicza](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/) ma przesunięcie wejścia o 0,03 i odjęcie/normalizację tej wartości, aby zachować czytelność ciemnej palety pixel artu. To stylizowana odmiana dopasowania, nie pełny transform ACES. LUT jest atlasem 64×64 z interpolacją między 16 przekrojami; jego korekta koloru jest łagodna.

Ekspozycja wynosi 1; początkowa siła bloom 0,08, winiety 0,10. Wstrząs i aberracja są domyślnie wyłączone. Efekty mają osobne suwaki 0–1 i są zapisywane w ustawieniach. Ruch kamery i aberracja gasną wraz z potwierdzonym błyskiem eksplozji. Pauza zamraża zegar efektów. Oznaczenia celów otrzymują transformację kamery, natomiast tekst i celownik pozostają ostre. Celowanie i pędzel stosują transformację odwrotną.

## Światła i osłony

Lampy sceny, lufa, eksplozje, ogień i lawa korzystają z jednej ograniczonej listy. Światła zdarzeń mają własny kursor generacji/ticka i sprawdzają ID potwierdzonych zdarzeń; nie potwierdzają ich w imieniu sesji gry. Działają przy VFX `off`. Priorytet: eksplozja, lufa, lampy, materiały. Materiały są analizowane równolegle w kaflach 8×8, po czym wybierana jest ograniczona lista. Jej przepełnienie ogranicza wyłącznie światło dekoracyjne.

DDA sprawdza maskę terenu, także boczne komórki przy przejściu przez narożnik. Wyłącza z testu własną komórkę powierzchni odbiornika, kończy promień przy źródle i traktuje wyczerpanie limitu jako zasłonięcie. Emitery materiałów umieszczane są przy rzeczywistej odsłoniętej powierzchni. Źródło zamknięte wewnątrz osłony nie świeci przez nią.

Maska jest porównywana na GPU przed renderem. Faktyczna zmiana, również wypalenie terenu, uaktywnia odbudowę w tej samej klatce. JFA używa dwóch tekstur RG16UI, kroków 256…1 oraz dodatkowego kroku 1. Nie jest dokładnym SDF i nie zastępuje testu widoczności. Ruchome ziarna i aktorzy otrzymują światło; ich własne cienie geometryczne nie należą do I11. Nie dodano światła pośredniego.

## Piasek i wilgoć

Cztery odcienie są wybierane przez stabilny hash ID. Wizualne AO korzysta z pola gęstości piasku. Szybkie zderzenia przy terenie emitują pył i krótkie smugi do istniejącego `VfxPool`, z jego limitem i rezerwą dla ważniejszych emisji. Dekoracje nie dodają masy fizycznej.

Wilgoć jest oddzielną teksturą **R32F 256×128**. R16F z planu zastąpiono R32F, ponieważ krok wysychania 1/3600 jest zbyt mały, aby zachować żądany czas po zaokrągleniach połowicznej precyzji. Nie zmieniono 64-bajtowego ABI cząstek ani znaczenia `motion.w`.

Kontakt z ciekłą wodą zwiększa wilgoć o 4/s, poza kontaktem ubywa 1/30 na sekundę. Mnożnik temperatury to `clamp(1 + max(T−293,15;0)/100; 1; 8)`. Przy termice off używana jest temperatura otoczenia; przy I10 pole kontaktu uwzględnia udział fazy ciekłej nośnika. Model opisuje wygląd, bez pobierania masy wody, zmiany entalpii i kohezji solvera.

Przed przydzielaniem slotów przez pędzel, gruz i kondensację osobny przebieg zeruje wilgoć nieaktywnych ID. Alokatory korzystają wyłącznie z tych wolnych slotów. Shadery numeryczne pozostają bajtowo zgodne z zachowaną bazą I10; obraz wilgoci nie jest częścią ich interfejsu. Reset zeruje cały bufor; zmiana materiału usuwa wilgoć w następnym ticku. Prezentacja pozostaje aktualizowana również przy świetle off, aby przełączanie jakości nie traciło historii. Render i powtórzony tick nie zmieniają wilgoci. Nagrania v1–v3 zachowują format i odtwarzają stan wyglądu z resetu oraz wykonanych ticków.

## Uruchamianie i pomiary

```powershell
./StrefaZero.exe --mission --gpu-lighting-quality low
./StrefaZero.exe --gpu-laboratory --gpu-lighting-quality high
./StrefaZero.exe --gpu-thermal-scene fire --gpu-lighting-quality high
./StrefaZero.exe --gpu-lighting-check --gl-debug --profile wyniki-jakosci
./StrefaZero.exe --gpu-lighting-benchmark --profile wyniki-pomiarow
python scripts/profiling/analyze-lighting.py wyniki-pomiarow --controls
./scripts/profile-lighting.ps1 -Executable ./StrefaZero.exe -Output wyniki-okien -Suite windows
```

Kontrola jakości obejmuje niezależny orakl odcinek/AABB, JFA względem dokładnego pola, cienką ścianę, destrukcję, wilgoć, gorące wysychanie, ponowne użycie slotu, potwierdzone zdarzenia bez VFX, HDR/sRGB oraz niezmienność fizyki i obrazu stanu podczas renderowania. Oddzielny test CPU sprawdza 30/60/144 FPS, ustawienia i CLI.

Macierz ma 6 scen × 2 jakości × 3 bloki ABBA × 4 segmenty × 240 próbek = **34 560 klatek**. Każdy segment poprzedza 120 klatek rozgrzewki. Fizyka jest zamrożona na identycznej kopii GPU. Kandydat wykonuje dwie aktualizacje prezentacji, w tym odpowiednie przygotowanie wolnych ID, oraz render; diagnostyczna referencja off pomija nowe aktualizacje, aby ich koszt był w całości wliczony do przyrostu. To referencja renderu zgodnego z I10, nie pomiar całej aplikacji I10. Zwykły runtime off nadal utrzymuje historię wilgoci.

Liczniki `dispatches` i czas CPU submit nadal obejmują pełną pracę. Dodatkowy `presentation_dispatches` oznacza rzeczywiste wywołania prezentacji, dzięki czemu test kontraktu solvera porównuje `dispatches − presentation_dispatches` i niezależnie wymaga czterech wywołań prezentacji w swojej scenie. Nie odejmuje się ich od kosztu I11. Kompletność framebufferów jest sprawdzana przy inicjalizacji; nie odpytuje się jej co tick ani dla każdego poziomu bloom.

Końcowy profil rozdziela `lighting`, `composition`, `bloom` i `postprocess`. Kompozycja obejmuje odcinek po zastosowaniu światła do nieprzezroczystej sceny: rekonstrukcję/filtrowanie cieczy oraz kompozycję HDR cieczy, termiki i VFX. Bloom zaczyna się dopiero po tym odcinku. `presentation_tick` mierzy sam tick prezentacji; przygotowanie narodzin jest dodatkowo objęte zewnętrznym pomiarem całej pary. Analizator wymaga kompletnych 51 840 próbek przebiegów, skończonych nieujemnych czasów oraz sumy etapów nieprzekraczającej czasu całości.

Scena wybuchu używa rzeczywistej zmienionej geometrii, a parametry światła potwierdzonego wybuchu są zamrożone na czas porównania. Czasowe gaśnięcie i deduplikacja mają osobne testy. Macierz zapisuje koszt całej pary, CPU, przebiegi, pamięć samego modułu świateł i ograniczenia źródeł. VFX są w niej wyłączone; dodatkowy pył jest sprawdzany w rzeczywistych oknach z VFX low. Bufory HDR sceny i świata są dodatkową pamięcią renderera. Nie obejmuje fizyki ani prezentacji okna. Asynchroniczne timery nie czekają na wynik w zwykłej pętli.

Budżet low: każdy segment kandydata ≤0,35 ms przyrostu względem referencji ABBA. Przebudowa pola cieni ma osobny cel 1 ms. Pełna klatka wymaga p95 ≤16,67 ms bez VSync i braku utraty czasu; mediana <6,5 ms jest celem rozszerzonym. Próby bez fokusu nie kwalifikują aktywnego okna. Włączenie I10 pozostaje oddzielnym obciążeniem i nie jest warunkiem startu I11.

## Wyniki i artefakty

Pełna regresja Windows przed ostatnią korektą granic timerów: **67/68**, błędy 1, pominięcia 0; GCC: **11/11**. Po korekcie timerów końcowe kontrole Windows: **5/5** (kontrakt CPU/CLI, presja, jakość światła, renderer i instrumentacja), a powtórzony kontrakt GCC: **1/1**. Dodatkowy test VFX z oświetleniem high i debugiem GL zakończył się poprawnie przed tą korektą. Okna mierzą wcześniejszy plik EXE z wyłączonymi timerami przebiegów; ostatnia zmiana dotyczy wyłącznie aktywnego profilera, a hashe obu wersji pozostają w metadanych prób. Końcowa macierz i testy skopiowanej paczki używają pliku EXE z poprawionym profilerem. Kontrole analizatorów odrzuciły 7 uszkodzonych macierzy i 10 niekwalifikowanych prób okna. W końcowej pełnej serii jedynym niezaliczonym testem był `gpu.mission_ui` (timeout 60,20 s), mimo wcześniejszego preflight 5/5 i misji 15,53 s. Świeże, pojedyncze kontrole I10 oraz I11 po serii również przekroczyły ten sam limit 60 s. Przyczyna rozbieżności pozostaje nieustalona; nie deklarujemy pełnego odbioru okna. Dwie wcześniejsze nałożone próby kontrolne oznaczono jako nieważne i zachowano oddzielnie.

Macierz zawiera **34,560 próbek / 144 segmenty**. Przyrost low względem ABBA wyniósł 0.650896–1.027516 ms; najgorszy segment: `fire`, blok 3, kolejność 2. Limit 0,35 ms: **FAIL** (36/36 przekroczeń). High: 0.745840–1.101245 ms; jest trybem dodatkowym, bez osobnego przyznanego budżetu. Ustabilizowany przydział zasobów samego modułu wyniósł 9 627 584 B (około 9,18 MiB); CSV obejmuje też stan przed pierwszą alokacją HDR. Osobne bufory HDR renderera nie są w tej liczbie.

Przebudowa cieni high po zmianie terenu: 60 próbek, mediana 0.121136 ms, p95 0.125952 ms, maksimum **0.269440 ms**; limit 1 ms: **PASS**. To pomiar przebiegu odbudowy, a nie całej klatki destrukcji.

Wszystkie 12 procesów okna uruchomiono osobno z żądaniem co najmniej 30 s, po trzy misji i laboratorium w każdym trybie VSync, z oświetleniem low i VFX low. Misja zachowuje również wymóg co najmniej 900 klatek i 1700 ticków, dlatego wolniejsza próba może trwać dłużej; tabela podaje rzeczywisty czas. Poniższe wyniki dotyczą klatek po rozgrzewce. „Kwalifikowana” wymaga fokusu, widoczności, pełnego zapisu, zgodnego VSync, co najmniej 29,9 s, braku utraty czasu oraz zaległości poniżej 0,1 s.

| Próba | Mediana ms | p95 ms | Fokus | Kwalifikowana | Klatki / czas s |
| --- | ---: | ---: | --- | --- | ---: |
| laboratory-off-1 | 71.478 | 73.232 | tak | nie | 323 / 30.05 |
| laboratory-off-2 | 72.459 | 75.023 | tak | nie | 318 / 30.05 |
| laboratory-off-3 | 72.187 | 73.860 | tak | nie | 321 / 30.05 |
| laboratory-on-1 | 73.104 | 74.818 | tak | nie | 314 / 30.06 |
| laboratory-on-2 | 72.296 | 74.245 | tak | nie | 318 / 30.00 |
| laboratory-on-3 | 73.380 | 75.165 | tak | nie | 314 / 30.07 |
| mission-off-1 | 71.566 | 74.041 | tak | nie | 780 / 60.44 |
| mission-off-2 | 74.125 | 76.205 | tak | nie | 780 / 61.60 |
| mission-off-3 | 70.690 | 72.996 | tak | nie | 780 / 59.99 |
| mission-on-1 | 74.454 | 76.924 | tak | nie | 780 / 61.84 |
| mission-on-2 | 72.792 | 75.262 | tak | nie | 780 / 60.94 |
| mission-on-3 | 71.358 | 73.417 | tak | nie | 780 / 60.17 |

Odbiór aktywnego okna bez VSync: **NIEODEBRANY**. VSync oceniono osobno: **NIEODEBRANY**. Utrata czasu w zapisanych próbach: maks. 0.000000 s; największa zaległość 31.873230 s. Wszystkie próby zachowały fokus; przekroczono p95 oraz limit zaległości.

**Decyzja: oświetlenie domyślnie `off`, termika nadal `off`.** Warunkiem zmiany domyślnego światła pozostaje jednoczesny odbiór limitu modułu i aktywnego okna bez VSync. I11 wdrożono funkcjonalnie, a niespełnione lub niekwalifikowane bramki wydajności pozostają otwarte. Historyczne wyniki I04.5/I07/I09/I10 zachowują swój status.


Środowisko: Windows x64 Release, MSVC `/W4 /WX` i LTO; NVIDIA GeForce GTX 1650, OpenGL 4.3, sterownik 610.47, obraz 960×576. Osobny rdzeń jest kompilowany GCC 13.3 z błędami zamiast ostrzeżeń. Pomiary dotyczą tego sprzętu i konfiguracji.

Kontrola jakości przeszła niezależny orakl 1027 promieni, porównanie JFA z dokładną referencją (maksymalny błąd tej sceny 0 komórek), osłonę 1px i ścianę za lampą, destrukcję w tej samej klatce, kontakt i wysychanie 30 s, limit temperatury 8×, ponowne użycie ID, odporność na render/powtórzony tick oraz HDR i pojedynczą konwersję sRGB. Obrazy sześciu scen zachowują dokładnie to samo surowe pole cieczy w off/low/high. Izolowany przebieg 120 ticków ma bajtowo identyczne trajektorie w trzech jakościach.

Próba presji obejmuje 32 768 szybkich ziaren w jednej komórce: całe pole prezentacji pozostaje skończone, fizyczne bufory się nie zmieniają. Sumowana prędkość jest kodowana w FP16 po podzieleniu przez 80, aby nie przepełnić kanału. Lista 960 kandydatów materiałowych kończy się maksymalnie 32 światłami; odrzucone dekoracje są raportowane.

Pierwsza pełna macierz przed rozdzieleniem granic timerów miała poprawny pomiar zewnętrznej pary, lecz kolumna bloom obejmowała również kompozycję. Low dodał wtedy 2,431784–3,892643 ms. Zachowano cały zestaw jako `initial-trials/benchmark-inclusive-bloom`. Powtórka po korekcie timerów jest wyraźnie szybsza; nie przypisujemy tej różnicy samej korekcie pomiaru. Przyczyna zmienności czasu pozostaje otwarta. Oba zestawy przekraczają ten sam limit 0,35 ms we wszystkich 36 porównaniach low.

Wcześniejsze nieudane i przerwane próby zachowano w `i11/initial-trials` oraz rejestrze historii. Pierwsza kontrola ujawniła brak rozwijania nowych nagłówków GLSL. Porównanie obrazów wykryło utratę wiązania bufora cząstek po świetle, przez co znikała woda; poprawka przywraca wiązanie, a test porównuje całe pole cieczy. Deklaracja wilgoci w shaderze numerycznym powodowała timeout kompilacji gorącego wariantu sorted/compatibility C; zastąpiono ją osobnym przebiegiem przed narodzinami. Odtworzony kontrolny I10 przeszedł ten wariant w 2,825 s, I11 w 4,996 s (88 audytów). Końcowe 15 shaderów numerycznych jest identyczne z zachowaną bazą I10.

Wstępna pełna seria miała 66/68: stary test liczby dispatchów nie uwzględniał jawnej prezentacji, a test misji przekroczył 60 s. Zachowano wynik i timeout. Po rozdzieleniu liczników oraz usunięciu powtarzanego sprawdzania framebufferów wydzielona próba pięciu testów przeszła, w tym misja w 15,53 s, z niezmienionym limitem 60 s. Kontrolny I10 przeszedł tę samą misję w 15,573 s.

Porównanie obrazów: [off / low / high](../measurements/i11/quality/comparison.png). Surowe obrazy BMP wszystkich sześciu scen, CSV, logi, JUnit, zachowana baza I10, różnica źródeł, hashe shaderów i pochodzenie pliku EXE znajdują się w `i11/`.

## Weryfikacja paczki Windows

Skopiowany plik `StrefaZero.exe` (SHA-256 `4e1558eb8b091975224f1c5710db8df1e4ff3d74c2f6fe49b6944c5118f53658`) przeszedł trzy kolejne, rozłączne uruchomienia: kontrolę jakości I11 z debugiem GL, smoke CPU/OpenGL 4.3 oraz smoke OpenGL 3.3. Wszystkie procesy zwróciły 0 bez timeoutu. Metadane i logi są w `i11/package-quality`, `i11/package-cpu` i `i11/package-legacy`. Końcowy ZIP zawiera manifest źródeł, hashe wszystkich plików, licencje i raport; jego osobna suma SHA-256 znajduje się obok archiwum.

W publikacji GitHub nowe surowe BMP są załącznikiem wydania; [instrukcja pobrania obrazów](../QA_IMAGES.md) odtwarza ich pierwotne ścieżki w repozytorium. Pozostałe dane I11 i porównanie PNG są dostępne bezpośrednio w Git.
