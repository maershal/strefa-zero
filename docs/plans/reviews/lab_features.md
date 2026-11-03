# Laboratorium materiałów i gra: ocena funkcji i backlog (GPU-first)

`maershal/strefa-zero` @ `e12c39a`, 2026-11-03. Ścieżki `plik:linia` liczone od korzenia repozytorium.

> [!IMPORTANT]
> **Migawka na `e12c39a`**, raport cząstkowy do [review_strefa_zero_v2.md](review_strefa_zero_v2.md) (§7–§9 streszczają
> go). Aktualny status: [REVIEW_V2_STATUS.md](../REVIEW_V2_STATUS.md). Numery linii odnoszą się do `e12c39a`.
> Zalecenia są treścią autora review, nie poleceniem właściciela.
>
> **Plik jest niepełny:** urywa się w §3.1. Kandydaci na materiały, wybuchy, UI i gra (wymienione w R §13) są
> w repozytorium tylko w postaci streszczenia w R §8–§9.

**Metoda.** Przejrzałem kod i dokumentację. Błędy CPU zmierzyłem ponownie własnym harnessem, który kompiluje
niezmienione `src/sim/*.cpp` (`scratch/lab_features_harness/`, poza repozytorium). GUI ani GPU nie uruchamiałem, więc
oceny UX GPU wynikają z kodu.

**Oznaczenia.**

- „szac.” to szacunek do potwierdzenia pomiarami.
- Wysiłek: S ≤ 2 dni, M ≤ 2 tygodnie, L > 2 tygodnie.
- **[M]** zależy od rejestru materiałów (P26/P27), **[R]** od przeprojektowania termiki i gazów GPU (analizuje je inny
  agent).
- Prompty P12–P58: `prompty_claude_code.md` z review 2026-11-02 (poza repozytorium).
- E1–E9 to mechanizmy silnika z [R §8.2](review_strefa_zero_v2.md#82-mechanizmy-silnika-które-odblokowują-najwięcej);
  A1–A13 i B1–B9 to pozycje laboratorium z review 2026-11-02 (nie problemy architektury A1–A12).

## 0. Wnioski

1. **Laboratorium GPU to dziś piaskownica mechaniczna:** 3 ziarna, 2 ciecze i ściana (`src/gpu/world.hpp:43`).
   Brakuje chemii, wybuchów i narzędzi badacza. Cała chemia GPU siedzi w termice I10, która jest domyślnie wyłączona
   i droga: dokłada 3,2–9,7 ms na tick przy budżecie 0,05/0,70 ms (`docs/reports/I10_GPU_THERMAL.md:135-140`). Spala
   tylko olej, drewno i gaz palny (`assets/shaders/thermal.comp:26, 72`).
2. **Najważniejsza decyzja:** oddzielić chemię kontaktową od termiki. Większość materiałów z wizji nie potrzebuje pola
   temperatury: kwas, ziemia, błoto, sól, ług, obsydian (lawa + woda), proch, C4, lont i emitery. Wystarczą cztery
   mechanizmy:
   - identyfikator materiału w cząstce (E1);
   - materiały komórek terenu (E2);
   - reakcje parami (E3);
   - wybuchy v2 (E6).

   Termika [R] dokłada później przemiany zależne od temperatury i gazu.
3. **Zalążki tych mechanizmów już są w kodzie** i wystarczy je uogólnić:
   - przełączanie rodzaju cząstki przy przemianie fazowej (`thermal.comp:54`);
   - zamiana komórki terenu w gruz (`assets/shaders/mission.comp:191-199`);
   - HP terenu (`src/gpu/world.cpp:730`);
   - tablice GLSL generowane z `sim::physical` (`src/gpu/thermal_system.cpp:16-43`).
4. **Wszystkie 5 błędów laboratorium CPU z §6.2 review nadal występuje** (pomiar na `e12c39a`, §1.2). Model CPU ma
   być wyrocznią, więc P13, P18 i P19 są warunkiem testów zgodności (P58).
5. **Kolejność faz:**
   - F0: szybkie wygrane;
   - F1: rejestr i materiały bez termiki;
   - F2: reakcje kontaktowe, wybuchy v2 i kodeks odkryć;
   - F3: ciepło i gazy (po [R]);
   - F4: zawalanie się konstrukcji, elektryczność, zapis i PNG, eksperymenty.

   To fazy tego raportu; roadmapa F0–F7 w [R §11](review_strefa_zero_v2.md#11-kolejność-prac-roadmapa) jest inna.
6. **Biblioteki:**
   - TAK: ImPlot; gałąź docking ImGui (bez multi-viewport); PNG przez `SDL_LoadPNG` / `SDL_SavePNG` (SDL 3.4 już jest
     w projekcie); dźwięk przez SDL3;
   - NIE teraz: Box2D, Lua;
   - opcjonalnie: toml++, FastNoiseLite.

## 1. Stan obecny

### 1.1 Inwentarz i luki parytetu

| Obszar | Laboratorium CPU (`src/ui/laboratory.cpp`) | Laboratorium GPU (`src/ui/gpu_laboratory.cpp`) | Luka |
| --- | --- | --- | --- |
| Materiały | 19 + gumka (`src/sim/material.hpp:8-30`), płaska lista (`:126-135`) | 6 + gumka (`:131`). Z termiką narzędzia zastępcze (`:215-226`): drewno = ściana HP 3, lód = drobiny metalu, lawa = olej | GPU nie ma kwasu, węgla, prochu, termitu ani NG. Zastępniki dziedziczą masę nośnika (`thermal.comp:54`: `p.motion.z = 1/a.matter.x`; masy w `particle_common.glsl:208`). Dlatego lawa ma masę cząstki oleju i pływa po wodzie (na CPU ρ 2,4), a lód ma masę drobiny metalu i tonie. Woda z roztopionego lodu jest 7,6 raza cięższa od zwykłej cząstki wody |
| Reakcje | 5 paliw, termit, kwas, wybuchy (`src/sim/reactions.cpp:225-334`) | Tylko z termiką: olej, drewno, gaz palny. Spalanie zużywa 2,0 O₂ i daje 10% dymu, CPU 0,7 O₂ i 15% | Literały zamiast tabeli po obu stronach |
| Wybuchy | `ExplosionCommand`, łańcuch przez udar (`reactions.cpp:232-234`), do 16 na tick (`src/sim/world.cpp:350`) | Tylko przy aktywnej misji (`src/gpu/world.cpp:930`), 1 na tick (`:936-945`, `mission.comp:126`), r = 20, impuls 70 | W laboratorium GPU nie da się niczego wysadzić |
| Teren | Komórki z wytrzymałością, odrywanie fragmentów (`world.cpp:730`, `scene_terrain.frag:10-12`) | Dwa typy terenu zamiast palety materiałów | `wall`: 1 = stała ściana, ≥ 2 = krucha, HP 3 dla wszystkich |
| Sceny | 15 (`src/sim/world.hpp:12-29`) | 12 + misja + 4 termiczne (`:13-17`). Świat jest wspólny z misją (`:34`) | Brak wspólnego opisu scen (P49) |
| Pędzel | Koło 1–16, tylko w puste komórki (`src/sim/world.cpp:252`) | Koło 1–16, tylko w wolne miejsce (`granular.comp:62, 69-72`) | Brak trybu „zastąp”, kształtów i temperatury |
| Mapy, inspektor | 6 map bez legendy (`:90-94`), inspektor w dymku (`:64-69`) | 6 map tylko z termiką (`:103-104`). Inspektor tylko z termiką, bez nazwy materiału (`:183-198`) | GPU nie mówi, co jest pod kursorem |
| Czas | Pauza, krok, tempo 1–4× (`:142`) | Pauza, krok (`:64-67`) | Brak slow-mo |
| Zapis, undo | Brak; nagrania BMP i CSV | Brak; log poleceń v1–v3, materiały 0–6 (`src/app/session_recording.cpp:137`) | README: zapis świata „poza wydaniem” (`README.md:160-162`) |
| Koszt | `dam-break` 31,8 ms, `extinguish` 49,9 ms na krok (2 wątki, pomiar z 2026-11-03) | Fizyka 32 768 cząstek: 8,07 ms na tick 1/120 s (`GPU_MISSION_070.md:55`) | Sceny CPU z cieczami nie mieszczą się w 16,7 ms. GPU ma zapas na nowe przebiegi przy ≤ 16k cząstek (szac.) |

### 1.2 Błędy z review §6.2 na `e12c39a`

Wszystkie pięć nadal występuje. Pomiar: `verify_bugs_e12c39a.txt` (poza repozytorium).

| Błąd | Kod | Pomiar |
| --- | --- | --- |
| **Wisząca skorupa.** Ślad fazy stałej robi z komórki ścianę | `src/sim/thermal.cpp:28` (`solid_mass > 1e-12` → ciało stałe), `:138-140` (`barrier = masa > 1e-7`), `src/sim/motion.cpp:52-53` (ruszają się tylko proszki i luźne fragmenty) | `quench-metal`: środek masy metalu przesuwa się z 106,50 na 107,38 w ciągu 180 ticków, a woda zaczyna się na y = 135. 138 cienkich komórek skorupy, każda ≤ 0,156 masy pełnej komórki (pełna = 7,8) |
| **Grzałka CPU zależna od FPS** | `ui/laboratory.cpp:51-52`, `src/app/application.cpp:783, 804`, `sim/world.cpp:182-209` (jedna komórka, bez górnego limitu T) | 10 × +100 w pustej komórce daje 50 293 K |
| **Mgiełka śladów cieczy** | `thermal.cpp:143` (próg 1e-5), `sim/world.cpp:479` (alfa ≥ 24/255), `:252` | `extinguish` po 180 tickach: 45% komórek „wody” (8 668 z 19 367) ma < 1% wypełnienia |
| **Wyczerpany kwas daje lód.** Entalpia kanału kwasu (cp 2,2) trafia do kanału wody (cp 4,18) | `reactions.cpp:326-330` | Kwas na kamieniu po 240 tickach: minimalna T cieczy 192,9 K, 112 komórek lodu |
| **Opis mówi, że gaz się unosi, a gaz opada** | `material.hpp:69` vs `src/sim/air.cpp:181-184` (dodatnia wyporność oznacza ruch w dół) | Środek masy zimnego gazu: y 95,00 → 100,68 w 600 tickach |

**Co się zmieniło od `5f22f8c`:**

- **Poligon CPU usunięto (#22).** Nieaktualne są P18 pkt 3, P20 pkt 3 i P54 pkt 5 oraz uwagi z §6.3 review o regułach
  plecaka i granatów.
- **Zrobiono P01–P06.** Funkcje laboratorium się nie zmieniły: A1–A13 i B1–B9 są nadal otwarte.
- **Kurs GPU-first** (P56–P58) obowiązuje. Raport uzupełnia go o to, czego plan nie miał: materiały i reakcje
  projektowane od razu pod GPU.

## 2. Luki względem wizji „trochę Noita, Soldat i TPT + laboratorium”

| Oczekiwanie gracza (źródło) | Strefa Zero dziś | Priorytet |
| --- | --- | --- |
| Teren z wielu materiałów, kopalny i niszczalny co do piksela (Noita, Worms) | Dwa typy terenu, HP 3 (`world.cpp:730`) | P1 |
| Reakcje zapisane jako dane i odkrywanie alchemii (Noita: `<Reaction probability input_cell>` z tagami, receptury losowane z ziarna świata) | Literały w `reactions.cpp` i `thermal.comp` | P1 |
| Ziemia, błoto, mokry piasek (Noita, Terraria) | Brak. Wilgoć piasku na GPU jest tylko wizualna (`presentation_moisture.comp:10`) | P1 |
| Wybuch z gruzem, krater zależny od materiału, reakcja łańcuchowa (Noita, Worms, TPT) | CPU ma łańcuch. GPU: 1 wybuch na tick, gruz rodzi się bez prędkości (`mission.comp:197`), w laboratorium brak wybuchów | P1 |
| Ogień, który się rozprzestrzenia (Noita, TPT) | CPU: drewno spala się w 0,35% po 30 s. GPU: ogień tylko z drogą termiką | P1/P2 |
| Gazy, które widocznie się unoszą lub opadają i płoną (TPT) | GPU: wyporność zależy tylko od T (`thermal.comp:57`); brak H₂ i CO₂ | P2 [R] |
| Przemiana fazowa zmienia mechanikę (TPT: METL → LAVA) | Zastępniki: lód to drobiny metalu, lawa to olej | P2 [R] |
| Wiele cieczy układających się warstwami (TPT) | Dwie ciecze; lepkość to literał (`liquids.comp:159`) | P1 |
| Narzędzia piaskownicy: kategorie, wyszukiwarka, kształty, zapis, undo, dzielenie się (TPT, Sandspiel) | Płaska lista, okrągły pędzel, brak zapisu i undo | P1 |
| Zawalanie się konstrukcji i sztywne bryły (Noita) | Brak; README wyklucza bryły | P3 |
| Elektryczność i urządzenia (TPT: SPRK, METL) | Brak | P3 |
| Soldat: ruch, plecak, wiele broni, obrażenia, ragdolle, boty | Plecak, karabin i granaty są. Postać jest niewrażliwa (`GPU_MISSION_070.md:7`) | P2 |
| Dźwięk | Brak: tylko `SDL_INIT_VIDEO` (`src/platform/window.cpp:19`) | P2 |

## 3. Nowe materiały i interakcje

### 3.1 Model danych: rejestr, reakcje parami (Noita) i przemiany (TPT)

```cpp
// Rejestr materiałów (P26) -> generowany blok GLSL (P27) i tabela w UI (P34).
enum class Mech : std::uint8_t { none, terrain, grain, liquid, gas, energy };

struct MaterialDef {
    std::string_view key, name, hint; Category cat; Color color, glow;       // UI
    Mech mech; float density, friction, drag, viscosity, cohesion, adhesion; // mechanika GPU
    float strength /* HP terenu */, blast_transmission, porosity;
    float cp, k, spawn_K, ignition_K, burn_rate, heat_release, smoke_yield;  // termika: CPU, GPU po [R]
    MaterialId residue, burning_variant; TagSet tags; // acid, base, salt, metal, organic, flammable, explosive...
    std::optional<Explosive> explosive; std::uint8_t legacy_gpu_kind; bool gpu;
};

struct Reaction {                        // Selector to id albo tag, np. [acid], [organic]
    Selector a, b; MaterialId out_a, out_b; float rate_per_s; Cond when;
    float heat_per_mass; GasYield gas; Event event; Mode mode; // Mode: contact | thermal
};

struct Transition {                      // Trigger: T>, T<, p>, age>, wet, dry
    MaterialId from, to; Trigger on; float threshold, latent, hysteresis;
};
```

*Dalsza część oryginału (§3.2 i kolejne) nie została dołączona.*
