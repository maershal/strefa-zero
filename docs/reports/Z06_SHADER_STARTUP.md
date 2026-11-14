# Z06 — start trybu GPU: kompilacja shaderów

Pomiar z 2026-11-14 na GTX 1650 (laptop, i5-9300HF, sterownik 610.47, zasilanie sieciowe, plan „Zrównoważony”,
GPU 52–56 °C przed każdym przebiegiem). Narzędzie: `scripts/profiling/shader-startup.ps1` (opis w nagłówku
skryptu), które uruchamia `StrefaQA --mission-ui-smoke-test` w świeżym profilu i czyta `gpu-startup.txt`.
A = `main` (`fdd1e26`), B = obie części Z06 (część 1: równoległa kompilacja i błędy, część 2: ekran postępu
i raport); kolejność ABBA, 3 powtórzenia na tryb, czyli 6 przebiegów na wariant,
przerwa 15 s przed każdym.

## Odpowiedź w skrócie

- Pierwsze wejście do trybu GPU na zimno (bez cache sterownika i bez cache binarnego) skraca się z **10,46 s do
  4,69 s** (mediana, −55%). Programów jest dziś 69, nie 42 jak w I05.
- Okno odpowiada podczas kompilacji: klatka „Kompilacja shaderów… n/m” co 100 ms. Najdłuższa przerwa bez obsługi
  zdarzeń wynosi 0,47–0,54 s (wcześniej całe ~10 s). Przyczyna przerw opisana niżej.
- Drugie i kolejne uruchomienia na tym samym sterowniku trwają ok. 80 ms przed zmianą i po niej, bo działa wtedy
  własny cache dyskowy sterownika NVIDIA.
- **Rekomendacja: nie włączać domyślnie cache binarnego (`--shader-cache`) w StrefaZero.** Na GTX 1650 wczytanie
  69 binariów (ok. 260 ms) jest ok. 3× wolniejsze niż cache sterownika (ok. 82 ms), a pierwszego startu nie skraca,
  bo na zimno cache binarny też jest pusty.

## Pomiar (GTX 1650, mediana z 6 przebiegów)

`first_world_ms` mierzy od wyboru trybu do otwartego świata (z kompilacją VFX, kerneli świata, renderera
i oświetlenia). `compile_ms` to czas biblioteki shaderów bez rysowania klatek postępu.

| Tryb | A: `first_world_ms` | B: `first_world_ms` | A: `compile_ms` | B: `compile_ms` | B: najdłuższa przerwa okna |
| --- | --- | --- | --- | --- | --- |
| zimny (cache sterownika wyłączony, bez binariów) | 10 460 (10 355–10 498) | 4 686 (4 644–4 726) | 10 381 | 4 577 | 515 ms |
| cache sterownika (drugi start) | 82,1 (80,8–84,3) | 84,8 (81,1–86,1) | 30,6 | 33,3 | 9 ms |
| cache binarny `--shader-cache on`, sterownik bez cache | 259 (255–267) | 266 (258–276) | 0 (69 trafień) | 0 (69 trafień) | 11 ms |

Czas na program na zimno: A 10 381 / 69 = 150 ms, kompilacja po kolei. W B programy kompilują się równolegle,
więc czas pojedynczego programu nie ma sensu; 4 577 ms to 66 ms na program w przeliczeniu. Na zimno w B część
`submit_ms` (zlecenia, ok. 350–370 ms) to głównie czekanie `glCreateProgram` na zajęte wątki kompilatora
sterownika, ok. 5 ms na program.

B jest wolniejsze o ok. 2,7 ms w trybie z cache sterownika i ok. 7 ms z cache binarnym (różnice w granicach
rozrzutu pojedynczych przebiegów, ale w tę samą stronę w obu trybach). Prawdopodobna przyczyna to uruchamianie
wątków kompilatora sterownika (`glMaxShaderCompilerThreadsKHR`); nie mierzyłem tego osobno.

Przebieg kontrolny po ostatniej poprawce (raportowanie postępu także przy zlecaniu): na zimno A 10 714 ms, B 4 829 ms
(mediany z 4 przebiegów ABBA), najdłuższa przerwa 474–543 ms. Ta poprawka nie skróciła przerwy; patrz niżej.

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
zleconym programie. Na CI (Mesa) ekran postępu jest więc ćwiczony w każdym teście okna, który otwiera tryb GPU.

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
  okna w trakcie kompilacji kerneli lub renderera przerywa otwieranie świata błędem i kończy pętlę aplikacji;
  podczas kompilacji VFX tylko zgłasza wyjście, które następuje po otwarciu świata.
- Błąd kompilacji podaje plik, etap (obliczeniowy, wierzchołków, fragmentów), definicje wstawione po pierwszej
  linii (z liczbą linii, o które przesuwają numery w logu sterownika), specjalizację restrict i pełny log. Błąd
  linkowania podaje pliki i etapy. Po błędzie biblioteka usuwa całą niesprawdzoną partię programów.
- `gpu-startup.txt` ma nowe pola: `parallel_compile`, `submit_ms`, `progress_ms`, `progress_gap_ms` (najdłuższy
  czas bez wywołania funkcji postępu). `compile_ms` nie obejmuje czasu rysowania klatek postępu.

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

Surowe CSV (GTX 1650: 36 + 8 przebiegów, Mesa: 16) i log: do repozytorium pomiarów (folder `z06-shader-startup-2026-11-14`), link
w opisie PR. Powtórzenie pomiaru:

```powershell
pwsh scripts/profiling/shader-startup.ps1 -Baseline <main>\build\release\StrefaQA.exe -Candidate build\release\StrefaQA.exe -Output shader-startup
```
