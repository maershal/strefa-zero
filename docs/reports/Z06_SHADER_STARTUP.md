# Z06 — start trybu GPU: kompilacja shaderów

Pomiar z 2026-11-14 na GTX 1650 (laptop, i5-9300HF, sterownik 610.47, zasilanie sieciowe, plan „Zrównoważony”).
Narzędzie: `scripts/profiling/shader-startup.ps1` (opis w nagłówku skryptu), które uruchamia
`StrefaQA --mission-ui-smoke-test` w świeżym profilu (ustawienia domyślne: okno 1280×800, VSync włączony) i czyta
`gpu-startup.txt`. A = `main` (`fdd1e26`), B = obie części Z06 (część 1: równoległa kompilacja i błędy, część 2:
ekran postępu i raport); kolejność ABBA, 3 powtórzenia na tryb, czyli 6 przebiegów na wariant, przerwa 15 s przed
każdym. Surowe dane: [z06-shader-startup-2026-11-14](https://github.com/maershal/strefa-zero-measurements/tree/079ac28fc1f30ac3e7276aa17b96acfc23ccc044/z06-shader-startup-2026-11-14).

## Odpowiedź w skrócie

- Pierwsze wejście do trybu GPU na zimno (bez cache sterownika i bez cache binarnego) skraca się z **10,7 s do
  4,9 s** (mediana, −54%; w serii bez telemetrii 10,46 → 4,69 s). Programów jest dziś 69, nie 42 jak w I05.
- Okno odpowiada podczas kompilacji: klatka „Kompilacja shaderów… n/m” co 100 ms (33 klatki na zimno). Najdłuższa
  przerwa bez obsługi zdarzeń wynosi 0,44–0,52 s (wcześniej cały start, ok. 10 s). Przyczyna przerw opisana niżej.
- Drugie i kolejne uruchomienia na tym samym sterowniku trwają ok. 80–90 ms przed zmianą i po niej, bo działa
  wtedy własny cache dyskowy sterownika NVIDIA.
- **Rekomendacja: nie włączać domyślnie cache binarnego (`--shader-cache`) w StrefaZero.** Na GTX 1650 wczytanie
  69 binariów jest ok. 2,5–3× wolniejsze niż cache sterownika (seria z telemetrią: 333 wobec 136 ms, bez
  telemetrii: 259 wobec 82 ms), a pierwszego startu nie skraca, bo na zimno cache binarny też jest pusty.

## Pomiar główny (GTX 1650, z telemetrią)

Przed pierwszym przebiegiem 300 s bezczynności (GPU 54 °C), przed przebiegami 57–58 °C, maksimum 62 °C. Telemetria
`GpuTelemetry.psm1` co 100 ms przez całą serię: żaden przebieg nie miał `hw_*` ani `sw_thermal_slowdown` (0 z 36
zdyskwalifikowanych), `sw_power_cap` w żadnym. `first_world_ms` mierzy od wyboru trybu do otwartego świata
(z kompilacją VFX, kerneli świata, renderera i oświetlenia). `compile_ms` to czas biblioteki shaderów bez
rysowania klatek postępu. Mediany z 6 przebiegów (min–max).

| Tryb | A: `first_world_ms` | B: `first_world_ms` | A: `compile_ms` | B: `compile_ms` | B: najdłuższa przerwa okna | B: klatki |
| --- | --- | --- | --- | --- | --- | --- |
| zimny (cache sterownika wyłączony, bez binariów) | 10 740 (10 541–10 889) | 4 938 (4 797–5 483) | 10 556 | 4 708 | 485 ms (441–502) | 33 |
| cache sterownika (drugi start) | 135,7 (132,5–173,3) | 135,7 (131,0–136,0) | 31,1 | 33,4 | 34 ms | 1 |
| cache binarny `--shader-cache on`, sterownik bez cache | 333 (324–399) | 369 (344–463) | 0 (69 trafień) | 0 (69 trafień) | 17 ms | 1 |

Telemetria co 100 ms spowalnia sam start: pełne zapytanie `nvidia-smi` (zegary, moc, przyczyny zdarzeń zegara)
dodaje ok. 50 ms do ciepłego startu (sprawdzenie po serii: B z cache sterownika 82–98 ms bez `nvidia-smi`,
134–151 ms z pełnym zapytaniem co 100 ms, 83–84 ms co 500 ms poza pierwszym przebiegiem; tabela w README danych).
Porównanie A–B jest więc ważne w obrębie serii, a bezwzględne czasy ciepłego startu podaje seria bez telemetrii.

Czas na program na zimno: A 10 556 / 69 = 153 ms, kompilacja po kolei. W B programy kompilują się równolegle,
więc czas pojedynczego programu nie ma sensu; 4 708 ms to 68 ms na program w przeliczeniu. Na zimno w B część
`submit_ms` (zlecenia, ok. 350–370 ms) to głównie czekanie `glCreateProgram` na zajęte wątki kompilatora
sterownika, ok. 5 ms na program.

Z cache binarnym B jest wolniejsze o 35 ms (mediana; rozrzut B 344–463 ms), w serii bez telemetrii o 7 ms. B rysuje
w tym trybie jedną klatkę postępu (start trwa ponad 100 ms), a z VSync `present` może czekać do jednej klatki
ekranu; osobno tego nie mierzyłem. Z cache sterownika różnicy nie ma (135,7 ms w obu wariantach; bez telemetrii
B wolniejsze o 2,7 ms, w granicach rozrzutu).

## Serie bez telemetrii (GTX 1650)

Ta sama macierz wcześniej tego samego dnia, bez telemetrii i bez bezczynności przed serią (GPU 52–56 °C przed
przebiegami). Seria kontrolna po ostatniej poprawce (raportowanie postępu także przy zlecaniu): tylko tryb zimny,
4 przebiegi ABBA na wariant.

| Tryb | A: `first_world_ms` | B: `first_world_ms` | A: `compile_ms` | B: `compile_ms` | B: najdłuższa przerwa okna |
| --- | --- | --- | --- | --- | --- |
| zimny | 10 460 (10 355–10 498) | 4 686 (4 644–4 726) | 10 381 | 4 577 | 440–515 ms |
| cache sterownika | 82,1 (80,8–84,3) | 84,8 (81,1–86,1) | 30,6 | 33,3 | 8–9 ms |
| cache binarny | 259 (255–267) | 266 (258–276) | 0 | 0 | 9–11 ms |
| zimny, kontrola | 10 749 (10 667–10 821) | 4 857 (4 820–5 714) | 10 665 | 4 711 | 474–543 ms |

## Mesa (llvmpipe, tylko informacyjnie)

WSL2, Mesa 25.2.8, llvmpipe (LLVM 20.1.2), ten sam laptop, kompilacja GCC Release; ABBA, 2 powtórzenia, czyli
4 przebiegi na wariant. Zimny = `MESA_SHADER_CACHE_DISABLE=true`, drugi start = cache dyskowy Mesy rozgrzany
jednym przebiegiem. llvmpipe ma `GL_KHR_parallel_shader_compile`, ale kompiluje w samym `glCompileShader`/
`glLinkProgram` (`submit_ms` ≈ `compile_ms`), więc partia nie przyspiesza.

| Tryb | A: `first_world_ms` | B: `first_world_ms` | A: `compile_ms` | B: `compile_ms` | B: najdłuższa przerwa okna |
| --- | --- | --- | --- | --- | --- |
| zimny | 3 330 (2 903–4 711) | 3 863 (3 171–5 008) | 1 423 | 1 583 | 240 ms |
| cache Mesy | 1 004 (720–1 256) | 1 334 (761–1 348) | 119 | 157 | 43 ms |

Rozrzut jest większy niż różnica A–B, więc nie wyciągam wniosku o spowolnieniu. Zysk na Mesie to odpowiadające
okno: w A okno stoi przez cały start (ok. 3 s), w B najdłużej 0,24 s, bo funkcja postępu jest wywoływana po każdym
zleconym programie. Na CI (Mesa) ekran postępu jest więc ćwiczony w każdym teście okna, który otwiera tryb GPU,
a `gpu.shader_progress_frames` sprawdza, że rysuje klatki.

## Co się zmieniło

- `ShaderLibrary::submit` zleca kompilację i linkowanie bez pytania o status, `ShaderLibrary::finish` sprawdza
  programy w kolejności zlecenia. `program()` = `submit()` + `finish()`. Gdy sterownik ma
  `GL_KHR_parallel_shader_compile` (albo wariant ARB), biblioteka ustawia maksymalną liczbę wątków kompilatora
  i odpytuje `GL_COMPLETION_STATUS`, wywołując między odpytaniami funkcję postępu. Bez rozszerzenia każdy program
  kompiluje się i jest sprawdzany od razu, jak dotąd.
- Zestaw kerneli świata przeszedł z `world.cpp` do `src/gpu/compute_kernels.cpp`: najpierw zlecenie wszystkich
  programów, potem `finish()`, potem lokalizacje uniformów. Kolejność programów i definicje są te same co bajt
  (te same źródła i klucze cache). Renderer, oświetlenie i VFX zlecają swoje programy razem i sprawdzają je jednym
  `finish()` przed odczytem uniformów; termika (domyślnie wyłączona) nadal używa `program()`.
- `app::ShaderProgressScreen` (`src/app/gpu_startup.cpp`) rysuje „Kompilacja shaderów… n/m” najwyżej co 100 ms
  (pierwszą klatkę też dopiero po 100 ms, więc start z cache zwykle nie rysuje żadnej), obsługuje zdarzenia okna
  i po każdej klatce unieważnia pamięć stanu GL urządzenia. Licznik m rośnie wraz ze zleceniami kolejnych grup
  (VFX, kernele, renderer, oświetlenie), więc pasek może cofnąć się przy przejściu do następnej grupy. Zamknięcie
  okna podczas kompilacji przerywa otwieranie świata błędem i kończy pętlę aplikacji: w trakcie kerneli lub
  renderera od razu, w trakcie VFX (budowanych w konstruktorze `RenderDevice`, którego nie da się przerwać) po ich
  zakończeniu, przy pierwszym programie kerneli.
- Błąd kompilacji podaje plik, etap (obliczeniowy, wierzchołków, fragmentów), definicje wstawione po pierwszej
  linii (z liczbą linii, o które przesuwają numery w logu sterownika), specjalizację restrict i pełny log. Błąd
  linkowania podaje pliki i etapy. Po błędzie (także wyjątku z funkcji postępu, czyli zamknięciu okna) biblioteka
  usuwa całą partię programów; binarki cache zapisuje dopiero po udanej całej partii.
- `gpu-startup.txt` ma nowe pola: `parallel_compile`, `submit_ms`, `progress_ms`, `progress_gap_ms` (najdłuższy
  czas bez wywołania funkcji postępu), `progress_frames` (narysowane klatki ekranu). `compile_ms` nie obejmuje
  czasu rysowania klatek postępu.
- Testy: `gpu.shader_cache` sprawdza partię równoległą (wymaganą, gdy sterownik zgłasza rozszerzenie),
  sekwencyjną, treść błędu i przerwanie partii z funkcji postępu (bez programów, liczników i binarek po nim).
  `gpu.mission_ui_quick` startuje z pustym cache sterownika (`gpu.mission_ui_quick_clean`), a
  `gpu.shader_progress_frames` wymaga co najmniej jednej klatki ekranu, gdy kompilacja trwała ponad 500 ms dłużej
  niż najdłuższa przerwa między wywołaniami postępu (lokalnie na GTX 1650: 45 klatek przy 6,7 s kompilacji).

## Dlaczego okno stoi nadal do ok. 0,5 s

Przerwy biorą się z `GL_LINK_STATUS` sprawdzanego zaraz po tym, jak `GL_COMPLETION_STATUS` zwróci prawdę: na tym
sterowniku zapytanie czeka jeszcze 50–480 ms na kilku programach (pomiar diagnostyczny, nie wchodzi do kodu).
Raport postępu przy każdym zleceniu tego nie zmienił. Obejście wymagałoby sprawdzania statusu linkowania poza
wątkiem GL albo odkładania go, czego rozszerzenie nie przewiduje. Zostawiam to tak: przerwa jest ok. 20× krótsza
niż wcześniej.

## Granice i ryzyka

- Te same programy i ta sama symulacja: zmienia się tylko kolejność wywołań GL (wszystkie `glCompileShader`/
  `glLinkProgram` partii przed pierwszym zapytaniem o status) i czas sprawdzania. Golden master CPU bez zmian,
  testy GPU z porównaniem bitowym przechodzą (lista w opisie PR).
- Klatki postępu rysuje ImGui w środku `ParticleWorld::create`; backend OpenGL ImGui odtwarza zmieniony stan,
  a biblioteka sama nie trzyma żadnego programu ani wiązania między wywołaniami.
- Limit 15 opcji StrefaZero bez zmian; nie dodano żadnej opcji.

## Dane

Surowe dane w [z06-shader-startup-2026-11-14](https://github.com/maershal/strefa-zero-measurements/tree/079ac28fc1f30ac3e7276aa17b96acfc23ccc044/z06-shader-startup-2026-11-14) (manifest `SHA256SUMS.txt`): seria z telemetrią
(36 przebiegów, `telemetry.csv`), serie bez telemetrii (36 + 8 przebiegów), Mesa (16 przebiegów), skrypty
i sprawdzenie narzutu telemetrii. Powtórzenie pomiaru (domyślnie 300 s bezczynności, przerwa 15 s, telemetria co
100 ms; `-TelemetryIntervalMs 500` dla bezwzględnych czasów ciepłego startu):

```powershell
pwsh scripts/profiling/shader-startup.ps1 -Baseline <main>\build\release\StrefaQA.exe -Candidate build\release\StrefaQA.exe -Output shader-startup
```
