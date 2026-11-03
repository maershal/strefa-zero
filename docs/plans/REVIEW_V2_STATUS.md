# Review v2 — status, rozstrzygnięcia i zasady dla agentów

Baza review: `e12c39a` (2026-11-03). Status sprawdzony na `9339c1a` (2026-11-04).
Źródła (migawki, numery linii z `e12c39a`): [review_strefa_zero_v2.md](reviews/review_strefa_zero_v2.md) (dalej
**R**), [architecture_v2.md](reviews/architecture_v2.md) (dalej **A**) i [lab_features.md](reviews/lab_features.md)
(dalej **L**, raport cząstkowy o laboratorium).

Ten plik jest ładowany do kontekstu agentów przez `CLAUDE.md`. Trzymaj go krótkim (≤ 150 linii): szczegóły są
w źródłach, tu jest tylko to, co trzeba wiedzieć przed każdą zmianą.

## Jak używać

- **Kierunek, nie polecenie.** Review jest opinią autora. Pozycja staje się zadaniem dopiero wtedy, gdy właściciel
  o nią poprosi albo ma swoje issue. Zasady z `CLAUDE.md` (kontrakty, domyślne opcje, progi testów) mają pierwszeństwo.
- **Sprawdzaj dowody.** Numery `plik:linia` pochodzą z `e12c39a` i się przesuwają; przed powołaniem się na nie
  zweryfikuj je w bieżącym kodzie. Liczby „szac.” to szacunki, nie pomiary.
- **Pracując w danym obszarze**, przeczytaj odpowiednią sekcję (mapa niżej) i nie idź pod prąd kierunkom z tabeli
  „Przyjęte kierunki”. Jeśli zmiana z nimi koliduje, powiedz o tym w opisie PR.
- **Po PR, który zamyka pozycję**, zaktualizuj tabelę „Status” w tym samym PR (z numerem PR albo issue).

## Mapa: obszar → sekcja

| Obszar | R | A | L |
| --- | --- | --- | --- |
| Termika i gazy GPU (koszt, zachowanie, projekt v2) | §3, §5.3 | §2 N8, §3.6 | — |
| Rejestr materiałów, codegen TOML → C++/GLSL | §4.2, §8.1 | §3.3, faza B | §3.1 |
| Stan materiału GPU, przemiany, `BirthDeathPass` | §4.2, §8.2 | §3.4, faza E | §0 |
| Rozbiór `ParticleWorld`, `PassTable`, QA poza produkcją | §4 | §1 A1/A3/A5/A10, §3.5, faza C | — |
| Aplikacja, tryby, laboratorium jedno GPU, GL 3.3 | §4.2, §7 | §3.8, faza D | §1.1 |
| Nowe materiały, reakcje, wybuchy v2 | §8.2–§8.6 | §3.3 | §1.1, §2 |
| Błędy wyroczni CPU (wisząca skorupa, grzałka, mgiełka, kwas, gaz) | §1 | — | §1.2 |
| UI laboratorium | §9 | §3.8 | §1.1 |
| Persystencja, nagrania v4, undo | §9 U9–U10 | §3.9 | — |
| Shadery (podział, `#line`, hot-reload, glslang) | §6 | §1 A11, §3.10 | — |
| Bryły sztywne | §4.2 | §3.7 | — |
| Biblioteki | §10 | §4 | §0 p. 6 |
| Proces, CI, testy | §2, §6 | §2 N7, N10 | — |

## Status (na `9339c1a`)

| Pozycja review | Stan | Gdzie |
| --- | --- | --- |
| Recenzja po CI i po każdym pushu, diff przed opisem (R §2.3 p. 2) | zrobione | #36 |
| Pominięte uwagi i „Out of scope” → issues; URL zgody przy kontrakcie (R §2.3 p. 3) | zrobione | #36 (`pr-hygiene.sh`) |
| CHANGELOG obowiązkowy dla `src/` (R §2.3 p. 5) | zrobione; tagi wydań — nie | #36 |
| `ci-changes.sh --no-renames` (R §2.2 #16) | zrobione | #36 |
| ccache zapisywany tylko na `main` (R §5.4) | zrobione; cache `strefa-deps` z PR — otwarte | #36, #35 |
| Nocny GPU: shardy, issue przy porażce, `perf-wallclock` osobno (R §2.3 p. 4, §5.4) | zrobione; liczba shardów w jednym miejscu — otwarte | #24, #25 |
| UI p95 bez oceny na rendererze programowym (R §6, §12 p. 6) | zrobione | #24 |
| `gpu.granular_physics`: pędzel zależny od historii (R §6) | naprawione, test `gpu.brush_history` | #24 |
| Gruz misji ze starymi kotwicami | otwarte | #23 |
| Golden master: „chain”, „chip”, MSVC, referencje per toolchain (R §6) | zrobione | #27 |
| Golden master: pozostałe 9 scen laboratorium CPU; clang z libc++ | otwarte | #28, #30 |
| Ratchet architektury: reguły warstw, budżety, symbole QA (A faza A1–A2) | zrobione | #42 |
| Spłata naruszeń z `tools/architecture_allowlist.txt` (A §1 A4) | otwarte | #40 |
| QA poza `StrefaZero`: benchmark scen, `ScenarioRunner` (A faza A4, N1) | otwarte | #39 |
| Ruleset `main` (R §12 p. 1, R1) | utworzony, `enforcement: disabled` | #31 |
| Recenzja pod tożsamością bota; `large-pr-approved` nie od autora | otwarte | #32, #33 |
| Benchmark scen nie unieważnia pomiaru przy zminimalizowanym oknie na Windows (R §2.2 #13, §5.3) | otwarte, bez issue (`application.cpp:250` nadal robi `continue` przed zapisem klatki) | — |
| Wszystko z F1–F7 (laboratorium, rejestr, termika v2, UI) i fazy A3, A5–A7, B–E | nie rozpoczęte | — |

Budżety ratchetu różnią się od propozycji A §5 (inne definicje liczników): obowiązują wartości
z `tools/architecture_budget.json`, np. `strefa_zero_qa_symbols` = 35, `size_literals_glsl` = 75,
`launch_options_bools` = 55.

## Przyjęte kierunki (stosuj, chyba że właściciel zdecyduje inaczej)

- **CPU to wyrocznia, nie produkt.** Model CPU ma być poprawny, nie szybki: nie optymalizować czasu rzeczywistego
  cieczy CPU (P53 odłożony). Laboratorium CPU: tylko poprawki błędów wyroczni, bez nowych narzędzi.
- **GPU-first, jeden świat.** Stan autorytatywny w buforach GPU; brak odczytów GPU→CPU co tick w gorącej ścieżce.
- **Nowe systemy poza `world.cpp`** (reakcje, wybuchy, przemiany). Nie dokładać haków testowych do `ParticleWorld`.
- **Dane zamiast literałów.** Nowe ID, wymiary i stałe materiałów nie jako kolejne literały (`320`/`192`, `2.65`,
  indeksy materiałów w GLSL); docelowo z rejestru generowanego z TOML, z plikami wygenerowanymi w repo i `--check`
  w CI.
- **Determinizm.** Kompaktowanie (narodziny/śmierć, kafle, reakcje) przez sumę prefiksową po ID, nie atomowe
  dopisywanie; losowość z deterministycznego hasha (seed, tick, ID, reguła); tempo reakcji w 1/s, p = 1 − e^(−k·dt).
- **Najpierw bitowo neutralnie.** Refaktoryzacja i konsolidacja danych bez zmiany golden master i `material_defines()`;
  zmiany zachowania osobnymi PR-ami z raportem `scripts/golden.py`.
- **Chemia kontaktowa przed termiką v2.** Materiały bez pola temperatury (kwas, ziemia, błoto, sól, ług, obsydian,
  proch, C4, lont) nie czekają na termikę.
- **Bryły jako klastry PBD** (shape matching), nie Box2D. Skrypty jako deklaratywny DSL, nie Lua.
- **Biblioteki:** wolno dodać Tracy (dev, `STREFA_TRACY=OFF`), glslang (tylko CI), doctest (tylko nowe testy, bez
  migracji); później ImGui docking, ImPlot, zstd. Nie: Box2D, Lua, Catch2, stb, nlohmann::json, lz4. Każda zależność
  przez `strefa_cached_archive` z SHA-256, licencją i ADR.
- **Pomiary termiki** z losową kolejnością scen i logiem zegara/temperatury GPU (R §5.3); llvmpipe tylko
  informacyjnie.

## Czeka na decyzję właściciela (nie realizować bez niej)

Z R §12 i A §6:

1. Zmiany kontraktów: nagrania v4 (hash rejestru, klucze materiałów), rewizja termiki I10.1 (deterministyczna
   kolejność sum), „≤ 8 SSBO na program, tabele w UBO”.
2. GL 3.3: zamrozić teraz, usunąć po parytecie laboratorium GPU.
3. Ujednolicenie stałych CPU↔GPU (P56): stechiometria O₂ 0,7 vs 2,0; dym 15% vs 10%; piasek 1,6 vs 2,65; wypór gazu
   palnego.
4. Model gazu: akustyka jak w CPU zamiast projekcji (po porównaniu obrazów).
5. Większe, przewijane mapy: teraz czy później (zmienia priorytet `TileGrid` i `WorldDims`).
6. Laboratorium CPU jako „Model referencyjny (CPU)” i przeniesienie „04 Podgląd areny” do Ustawień.

## Następne kroki według review

Ścieżka minimalna do „laboratorium z wizji” (R §11, prompty K nie są w repozytorium): siatka bezpieczeństwa (prawie
domknięta, zostało #31 i benchmark przy zminimalizowanym oknie) → szybkie wygrane w laboratorium GPU (Detonator,
gruz z prędkością, slow-mo, legendy, „Wejdź żołnierzem”, porządek UI) i naprawa 5 błędów wyroczni CPU → rejestr danych
(B1–B3, bitowo neutralnie) → chemia kontaktowa E1–E7 i wybuchy v2 → termika v2. Równolegle, przy dostępie do GTX 1650:
bitowo neutralne przyspieszenie termiki (blokowany Jacobi, pomijanie reakcji bez paliwa, przebiegi tylko po żywych ID).
