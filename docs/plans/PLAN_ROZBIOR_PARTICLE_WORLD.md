# Rozbiór `ParticleWorld` (Z09)

Stan wyjściowy (`36db768`): `world.cpp` 2055 linii, jednostka kompilacji z siedmioma plikami `.inl` 3990, 88
deklaracji publicznych `ParticleWorld`, w tym 30 haków testowych. Ratchet (`tools/architecture_budget.json`) trzyma te
liczby na limicie, więc żadna nowa funkcja wymagająca API świata (migawki, materiały, inspektor) nie przejdzie, dopóki
haki nie wyjdą z klasy. Cztery pomocniki dispatchu (`dispatch`, `liquid_dispatch`, `neighbor_dispatch`,
`mission_dispatch`) powielają wiązanie programu i buforów, bariery, ślad i strefy Tracy; wybór przebiegu to łańcuchy
`if (op == _)`.

Stan docelowy (A faza C): haki testowe i walidacje poza klasą, w `strefa_diagnostics`, przez jeden nagłówek dostępu
testowego (`gpu/world_test_access.hpp`); jeden `PassRunner` z tabelą opisów przebiegów, przez który idą też termika
i oświetlenie (#66), ze strefami Tracy w jednym miejscu (#59); operacje misji w osobnym module (#67).

Każdy krok jest bitowo neutralny: `sim.golden_master`, `--gpu-check thermal-bits`, `coupling_bitwise`,
`gpu.brush_history` bez zmian, a `gpu.instrumentation` (62 dispatche solvera, 34 przebudowy, 24/34 redukcje)
i `CallCounters` liczą to samo. Każdy PR obniża budżety przez `check_architecture.py --update` i ma przebieg Nightly
GPU na swojej gałęzi.

## Ograniczenia

- **Limit 800 linii.** `pr-hygiene.sh` liczy `git diff --numstat`: wykrywa zmiany nazw, nie kopie. Wycięcie fragmentu
  z pliku, który zostaje, liczy się podwójnie; `git mv` pomaga tylko przy plikach `.inl`.
- **`readback`, `read_mission`, `read_terrain`** wołają też `app/scenario_runner.hpp` i `launch_options.cpp` (moduł
  `launch`), więc zostają w klasie do wyniesienia `ScenarioRunner` (#39).
- **`fluid_quality.cpp` jest w `strefa_gpu`** i woła haki; gdy ich definicje przejdą do `strefa_diagnostics`, plik
  idzie razem z nimi (inaczej krawędź `gpu → qa`).
- **Alias `indirect`.** `Impl::bound_indirect` to referencja na `GlStateTracker::indirect`, pole, którego
  `draw_indirect()` używa dla `GL_DRAW_INDIRECT_BUFFER`. `PassRunner` dostaje osobne pole dla
  `GL_DISPATCH_INDIRECT_BUFFER`.
- **Dostęp do `Impl`.** `friend struct detail::WorldAccess;` w sekcji `private:`; licznik deklaracji publicznych
  liczy tylko sekcję `public:`.
- **Self-test ratchetu** (`scripts/test-check-architecture.sh`) kotwiczy wstawiany hak na linii z `world.hpp`; krok,
  który ją usuwa, przestawia kotwicę.
- **Nowy target `strefa_*`** wymaga wpisu w `TARGET_MODULE`; plan nie przewiduje nowych targetów.

## Kroki

| # | Zakres | Budżety | Rozmiar |
| --- | --- | --- | --- |
| 1 | 21 haków jako funkcje wolne `strefa::gpu::test::*` w `gpu/world_test_access.hpp` przez `detail::WorldAccess`; ciała zostają w `world.cpp` | `public_decls` 88→67, `test_hooks` 30→9, `world_cpp` −1 | ~330 linii |
| 2 | Definicja `Impl` (+ pomocniki z anonimowej przestrzeni) do `gpu/world_impl.hpp` | `world_cpp`, `world_tu` −~470 | ~950, `large-pr-approved` |
| 3 | Ciała haków z kroku 1 do `gpu/world_test_access.cpp` w `strefa_diagnostics`, razem z `fluid_quality.cpp` | `world_cpp`, `world_tu` −~250 | ~550 |
| 4 | `git mv` walidacji renderera i VFX z `.inl` na `.cpp` w `strefa_diagnostics` | `inl_includes` 7→3, `world_tu` −~1014, haki −3 | ~300–500 |
| 5 | Jak 4 dla oświetlenia i termiki; `thermal_scenes` jako osobny plik `strefa_gpu` | `inl_includes` 3→0, `world_tu` −~920, haki −2 | ~300–500 |
| 6 | `validate_gameplay_protocol` do pliku QA | `world_cpp` −~315, haki 4→3 | ~650 |
| 7 | Szkielet `PassRunner` (tabela przebiegów, strefa Tracy i ślad w `run()`), migracja `neighbor_dispatch` | `world_cpp` −~20 | mały |
| 8 | `liquid_dispatch`, potem `dispatch` przez tabelę; przypadki specjalne jako haki przed/po przebiegu | `world_cpp` −~100 | 2 PR-y |
| 9 | Misja i sprzężenie do `mission_passes.cpp` (#67), termika i oświetlenie przez `PassRunner` (#66) | `world_cpp` w dół | 2–3 PR-y |

Kolejność zwalnia miejsce w API najwcześniej: krok 1 zdejmuje 21 deklaracji bez etykiety. Kroki 2–6 domykają haki,
7–9 to `PassRunner`. Wszystkie dotykają `world.cpp`, więc idą po kolei.

## Ryzyka

- Krok 1: kolizje nazw z metodami przy ADL (dlatego osobna przestrzeń `test`); liczba symboli QA w `StrefaZero`
  nie powinna się zmienić (nazwy nie pasują do wzorca), potwierdza ją krok CI na Linuksie.
- Krok 2: `world_impl.hpp` dołączany w dwóch modułach; metody inline są ODR-bezpieczne, ale kompilacja trwa dłużej.
- Kroki 4–5: przy podobieństwie < 50% `git` nie wykryje zmiany nazwy i PR przekroczy limit; sprawdzić
  `git diff -M --numstat` przed otwarciem.
- Kroki 7–9: ścieżka `cached_bindings=false` woła najpierw `UseProgram(granular[0])` (zachować); rozdzielenie aliasu
  `indirect` może zmienić liczbę wywołań `BindBuffer`, nie wynik.

## Postęp

| Krok | Stan | PR |
| --- | --- | --- |
| 1 | zrobione | #107 |
| 2 | w recenzji | #105 |
| 3–6 | otwarte | #105 |
| 7–8 | otwarte | #106 |
| 9 | otwarte | #66, #67 |
