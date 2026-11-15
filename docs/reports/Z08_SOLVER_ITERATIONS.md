# Z08 — ile iteracji solvera naprawdę trzeba

Pomiar z 2026-11-14/15 na GTX 1650 (laptop, i5-9300HF, sterownik 610.47, zasilanie sieciowe, plan „Zrównoważony”),
build Release MSVC z `main` f85caeb i narzędziem z tego PR. Surowe dane:
[z08-iteration-sweep-2026-11-15](https://github.com/maershal/strefa-zero-measurements/tree/eee3c6d4f7d84edd77470eef02de554af75e62e9/z08-iteration-sweep-2026-11-15).
Ten raport przedstawia dane. Ustawienia domyślne (2 podkroki × 8 iteracji PBF / 12 kontaktów) i shadery się nie
zmieniają.

## Wniosek

Najtańsza konfiguracja, która przeszła wszystkie bramki we wszystkich powtórzeniach, to **5/10**: 94 zamiast 114
dispatchy na tick misji (**−20 przebiegów, −0,72 ms, −26%**), ale w bramce ziaren ma tylko 0,009 komórki zapasu,
a ta sama liczba kontaktów zawiodła w 6/10 i 4/10; z zapasem jest **5/11** (−16 przebiegów, −0,66 ms, −24%).

## Co zmierzono

`StrefaQA --gpu-check iteration-sweep` (`src/gpu/iteration_sweep.cpp`, procedura w `docs/maintenance/GPU_CHECK.md`,
sekcja 4e) tworzy świat z `FluidOptions` o zadanej liczbie iteracji PBF i kontaktu (2 podkroki, profil cieczy
domyślny `legacy`, reszta opcji domyślna) i uruchamia na nim bez zmian istniejące walidacje. Potem mierzy koszt ticka
na skryptowej trasie misji i w przerwaniu tamy. `FluidOptions::valid()` wymaga, żeby kontaktów było co najmniej tyle
co iteracji PBF.

| Bramka | Walidacja (CTest) | Metryki i progi, bez zmian | Źródło |
| --- | --- | --- | --- |
| `fluid_quality` | `--gpu-fluid-check` (`gpu.fluid_quality`) | tożsamość i masa per slot, wartości skończone, orakle CPU granic (1e-5), szczelność kanału, kompresja < 5% w ticku 1200 w 7 scenach; miary I07 (amplituda, skupianie, energia) tylko zapisywane | `src/gpu/fluid_quality.cpp` |
| `liquids` | `--gpu-liquid-test` (`gpu.pbf_*_liquid`) | gęstość GPU wobec CPU < 1e-5, bez tunelowania przez ścianę 1 px, błąd gęstości scen < 5%, tama (> 500 cząstek w prawo), warstwy olej/woda (+5), piasek tonie (+2), naczynia (różnica < 3), niezależność od FPS | `src/gpu/liquid_validation.cpp` |
| `granular` | `--gpu-test` | balistyka < 0,006, ściana 1 px, para, **penetracja < 0,46 komórki po 480 tickach** w scenach lawina, deszcz, mieszana; tożsamość materiałów | `src/gpu/validation.cpp` |
| `flow` | `--gpu-flow-test` | klepsydra (opróżniona > 98%, 0 poza obrysem), olej/woda (olej < 132, zanurzenie > 0,8, powierzchnia > 0,95), pęd, mały błąd | `src/gpu/flow_validation.cpp` |
| `coupling` | `--gpu-coupling-check` (`gpu.pbf_*_coupling`) | redukcje sprzężenia Q, porównanie geometrii z odtworzeniem referencyjnym (wymaga lokalnych reakcji) | `src/gpu/coupling_validation.cpp` |
| `mission` | `--mission-test` (`gpu.mission`) | trasa misji (cele, wybuchy, gruz, wyporność, piasek), niezależność od FPS, szczelność | `src/gpu/mission_validation.cpp` |

Dwa podsprawdzenia nie zależą od siatki: `fields_checks` w `fluid_quality` (orakle napięcia, adhezji i wirowości)
tworzą własne światy z `FluidOptions{}`, a test zbieżności w `liquids` wywołuje rzutowanie z 80 iteracjami. Profil
`balanced` (z nim działa CTest `gpu.fluid_quality`) nie był przeglądany: gra używa `legacy`.

Koszt: plan `timing`, 4 bloki, w każdym losowa kolejność 18 odcinków (9 konfiguracji × misja/tama), każdy odcinek ze
świeżego świata, 120 ticków rozgrzewki i 240 mierzonych (para `GL_TIMESTAMP` wokół `step()`), sondy kalibracyjne Z01
z bramką stygnięcia przed i po odcinku, dispatche z 16 ticków po pomiarze. 300 s bezczynności przed startem (GPU 57 °C),
telemetria co 100 ms: 0 z 72 odcinków zdyskwalifikowanych (`hw_*`, `sw_thermal_slowdown`, zegar < 90% maksimum
1920 MHz, sondy > 15% od najszybszej), `sw_power_cap` w 16. Δ to mediana z 4 bloków różnicy wobec 8/12 w tym samym
bloku. Zamiast kolejności ABBA każdy blok losuje kolejność wszystkich 18 odcinków, a Δ porównuje odcinki z tego samego
bloku: przy 9 konfiguracjach ABBA dla każdej pary wymagałoby wielokrotnie dłuższej sesji, a losowa kolejność tak samo
rozkłada dryf termiczny między konfiguracje. Tryb `--gpu-check` działa w ukrytym oknie (domyślny rozmiar z ustawień,
okno nigdy nie jest pokazywane) i pomiar nie wywołuje `SDL_GL_SwapWindow`, więc VSync (ustawienie domyślne, włączone)
ani rozmiar okna nie wchodzą do zmierzonego czasu `step()`; nie dotyczy go też problem zminimalizowanego okna z #44.

## Wyniki

Bramki: „PASS” bez liczby = jeden przebieg; „k/n” = k zaliczonych z n procesów. Bramki ziaren i sprzężenia powtarzano,
bo solver cząstek jest niedeterministyczny między przebiegami (atomowe dopisywanie, `GPU_CHECK.md` sekcja 4c).

| PBF/kontakt | fluid_quality | liquids | granular | flow | coupling | mission | Dispatche/tick misji | Misja [ms/tick] | Δ misji [ms] | Δ tamy [ms] |
| --- | --- | --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| 8/12 (domyślne) | PASS 2/2 | PASS 2/2 | PASS 7/7 | PASS 2/2 | PASS 7/7 | PASS 2/2 | 114 | 2,724 | 0 | 0 |
| 7/11 | PASS | PASS | PASS 6/6 | PASS | PASS 6/6 | PASS | 106 | 2,484 | −0,228 | −0,271 |
| 6/10 | PASS | PASS | **FAIL 5/6** | PASS | PASS 6/6 | PASS | 98 | 2,209 | −0,527 | −0,503 |
| 5/11 | PASS | PASS | PASS 6/6 | PASS | PASS 6/6 | PASS | 98 | 2,056 | −0,662 | −0,812 |
| 5/10 | PASS | PASS | PASS 6/6 | PASS | PASS 6/6 | PASS | 94 | 2,010 | −0,717 | −0,830 |
| 4/11 | PASS | PASS | PASS 6/6 | PASS | **FAIL 5/6** | PASS | 94 | 1,861 | −0,865 | −1,130 |
| 6/8 | PASS 2/2 | PASS 2/2 | **FAIL 0/7** | PASS 2/2 | **FAIL 1/7** | PASS 2/2 | 90 | 2,091 | −0,621 | −0,554 |
| 5/8 | PASS | PASS | **FAIL** | PASS | PASS | PASS | 86 | 1,907 | −0,809 | −0,848 |
| 4/6 | PASS | PASS | **FAIL** | PASS | PASS | PASS | 74 | 1,590 | −1,134 | −1,131 |

Poza tabelą kosztu (tylko bramki): 6/9 ziarna 3/6, 5/9 2/6, 4/9 2/6, 4/10 5/6, 4/8 0/1; sprzężenie 6/6 w każdej z nich.
Zakres Δ misji między blokami: najwyżej 0,11 ms (np. 5/10: −0,74…−0,69). Pełne liczby: `analysis.json`, `summary.md`.

### Co decyduje

- **Ciecze nie są wąskim gardłem.** Wszystkie bramki cieczy, przepływu i misji przechodzą już przy 4 iteracjach PBF.
  Kompresja w ticku 1200 rośnie z 1,4% (8/12) do 2,6% (4/6), wciąż poniżej progu 5%.
- **Wiąże penetracja ziaren w scenie mieszanej** (próg 0,46 komórki, `validation.cpp`). Maksimum z powtórzeń:
  12 kontaktów 0,383; 11 kontaktów 0,422–0,433; 10 kontaktów 0,451 (5/10), z porażką raz na sześć w 6/10 i 4/10;
  9 kontaktów porażka w połowie przebiegów lub częściej; 8 i mniej porażka zawsze. Zależy głównie od kontaktów, nie od
  PBF.
- **Sprzężenie** zawodzi komunikatem „Geometry comparison exercised no local reactions”: w porównaniu geometrii żadna
  cząstka nie oddała reakcji kapsule, więc test nie ma czego porównać. W 6/8 tak jest prawie zawsze (6/7), w 4/11 raz
  na sześć, w 8/12 nigdy (0/7). To warunek wstępny testu, nie błąd zmierzonej siły; raport go nie interpretuje dalej.

### Przebiegi

Każda iteracja PBF i każda iteracja kontaktu to 4 dispatche na tick (2 na podkrok): dispatche misji = 34 + 4 × (PBF +
kontakty), od 114 (8/12) do 74 (4/6). Raport [Z01](Z01_GPU_COST_MODEL.md) liczył 140 przebiegów na tick misji; od Z05
jest ich 114 (`docs/CHANGELOG.md`, wpis o jednym dispatchu redukcji sprzężenia; `gpu.instrumentation`). Koszt w misji: ok. 0,028 ms na dispatch ((2,724 − 1,590) / 40), zgodnie
z obserwacją Z01, że koszt idzie za liczbą przebiegów. Żadna konfiguracja nie osiąga celu 1,5 ms na tick misji
(4/6: 1,59 ms, bez bramki ziaren).

### Miary jakości I07 (jeden przebieg, okno 600–1200 ticków)

Bramki tych miar nie oceniają (I07 nie przyjęło dla nich progów); pokazują, co zmienia mniejsza liczba iteracji.

| PBF/kontakt | Maks. kompresja (7 scen) | Amplituda zbiornika | Skupione w zbiorniku | Amplituda naczyń | Skupione w kropli oleju | Energia kin. zbiornika |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 8/12 | 0,0136 | 0,0172 | 0,0055 | 0,329 | 0,0014 | 1007 |
| 7/11 | 0,0200 | 0,0242 | 0,0087 | 0,567 | 0,0020 | 1184 |
| 6/10 | 0,0178 | 0,0236 | 0,0106 | 0,588 | 0,0024 | 1546 |
| 5/11 | 0,0213 | 0,0474 | 0,0144 | 0,523 | 0,0036 | 2404 |
| 5/10 | 0,0231 | 0,0214 | 0,0136 | 0,599 | 0,0030 | 2755 |
| 4/11 | 0,0263 | 0,0574 | 0,0146 | 0,608 | 0,0036 | 3414 |
| 4/6 | 0,0264 | 0,0380 | 0,0148 | 0,302 | 0,0047 | 3101 |

Mniej iteracji PBF podwaja–potraja energię kinetyczną spoczywającego zbiornika i udział skupionych cząstek; 5/10 i 5/11
mają 2,4–2,7 razy więcej energii i 2,5 razy więcej skupień niż 8/12. Amplituda naczyń (0,30–0,61) mieści się
w rozrzucie, jaki I07 zmierzyło dla dwóch przebiegów `legacy` (0,558–0,577), więc jej pojedynczych wartości nie
porównujemy.

## Ograniczenia

- Jedna maszyna (GTX 1650) i jeden sterownik; koszt i rozrzut bramek na innych GPU nie były mierzone.
- Bramki inne niż ziarna i sprzężenie mają jeden przebieg (8/12 i 6/8: dwa). Niedeterminizm solvera może dać
  pojedynczą porażkę także tam; 6/10 przeszło ziarna w pierwszym przebiegu i zawiodło w czwartym powtórzeniu.
- `fluid_quality` sprawdza profil `legacy`; profil `balanced` przy mniejszej liczbie iteracji nie był mierzony.
- Zmiana domyślnych iteracji zmienia fizykę misji i nagrania (zapisują konfigurację cieczy, `fluid_configuration`) oraz
  wartości w `gpu.instrumentation`. To osobna decyzja właściciela; ten PR jej nie podejmuje.

## Odtworzenie

```powershell
$env:STREFA_ITERATION_SWEEP_PLAN = 'gates'
./build/release/StrefaQA.exe --gpu-check iteration-sweep --profile gpu-check/iteration-sweep-gates
$env:STREFA_ITERATION_SWEEP_CONFIGS = '6/9,5/10,4/10,5/9,4/9,4/8,8/12,6/8'   # potem '4/11,5/11'
./build/release/StrefaQA.exe --gpu-check iteration-sweep --profile gpu-check/iteration-sweep-gates-2
# powtórzenia bramek ziaren i sprzężenia, 5 procesów
$env:STREFA_ITERATION_SWEEP_GATES = 'granular,coupling'
$env:STREFA_ITERATION_SWEEP_CONFIGS = '8/12,7/11,6/10,6/9,5/11,5/10,5/9,4/11,4/10,4/9,6/8'
foreach ($n in 1..5) {
    ./build/release/StrefaQA.exe --gpu-check iteration-sweep --profile gpu-check/iteration-sweep-repeat-$n
}
Remove-Item Env:STREFA_ITERATION_SWEEP_GATES, Env:STREFA_ITERATION_SWEEP_CONFIGS, Env:STREFA_ITERATION_SWEEP_PLAN
pwsh ./scripts/profiling/profile-iteration-sweep.ps1 -Executable build/release/StrefaQA.exe -Output gpu-check/iteration-sweep-timing -Plan timing
# PowerShell nie rozwija `*` w argumentach programów natywnych: ścieżki trzeba rozwinąć samemu
$gates = (Get-ChildItem gpu-check/iteration-sweep-gates*/iteration-sweep-gates.csv, gpu-check/iteration-sweep-repeat-*/iteration-sweep-gates.csv).FullName
python ./scripts/profiling/analyze-iteration-sweep.py gpu-check/iteration-sweep-timing --gates @gates
```

W surowych danych powtórzenia 4/11 i 5/11 są w osobnych katalogach `iteration-sweep-repeat-11-<n>` (dołożone po
pierwszej serii); powyższa pętla zbiera je razem z resztą.
