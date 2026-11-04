# ADR 0002 — narzędzia profilowania: Tracy (dev), RenderDoc w StrefaQA, Nsight

- Status: przyjęta (2026-11-04, zadanie K07 właściciela)
- Dotyczy: `cmake/Dependencies.cmake`, `src/core/profiling.hpp`, `src/platform/gpu_zone.hpp`,
  `src/tools/renderdoc_capture.*`, presety `dev` i `linux-dev`, `docs/maintenance/GPU_CHECK.md`

## Kontekst

Projekt mierzy czas GPU sześcioma własnymi pulami kwerend (`ParticleWorld` z `ElapsedQueryGuard`, termika, oświetlenie,
VFX, renderer sceny, ślad przebiegów `DiagnosticTrace`), a każdy PR z pomiarami dokłada kolejną. Każda pula daje
liczby do CSV, żadna nie pokazuje osi czasu CPU i GPU razem. Decyzje GPU blokuje niewyjaśniony „wolny reżim”
(×2,4–4,1 czasu ticku). Bez osi czasu nie widać, czy to GPU czeka na CPU, sterownik szereguje pracę inaczej, czy
zegar karty spada. Review v2 (R §10, A §4, D7) zaleca Tracy w buildach deweloperskich i RenderDoc jako skrót
w StrefaQA.

## Decyzja

1. **Tracy v0.14.1** (BSD-3, `licenses/Tracy-LICENSE.txt`) przez `strefa_cached_archive` z SHA-256. Opcja CMake
   `STREFA_TRACY` jest **domyślnie OFF**. Włączają ją presety `dev` (Windows) i `linux-dev` (RelWithDebInfo, cel
   `strefa_qa`). Klient działa w trybie `TRACY_ON_DEMAND` i nasłuchuje tylko na localhost
   (`TRACY_ONLY_LOCALHOST`). Bez podłączonego profilera strefa kosztuje jedno sprawdzenie flagi.
2. **Jedno miejsce na makra.** `STREFA_ZONE(nazwa)`, `STREFA_PLOT(nazwa, wartość)` i `STREFA_FRAME_MARK()`
   są w `core/profiling.hpp` (model CPU może ich używać bez zależności od SDL/GL), a `STREFA_GPU_ZONE(nazwa)` jest
   w `platform/gpu_zone.hpp`. Przy OFF wszystkie makra rozwijają się do `static_cast<void>(…)`, więc w kodzie nie
   ma `#ifdef`. Strefy GPU (`TracyOpenGL.hpp`) korzystają z własnej tabeli wskaźników GL, a nie z `gpu::detail::Api`,
   więc kwerendy Tracy nie zmieniają liczników wywołań GL w profilach.
3. **Strefy stoją przy istniejących punktach pomiaru, nie obok nich.** CPU: klatka `Application`,
   `SimulationWorld::advance`, `ParticleWorld::step` z etapami (przygotowanie misji, podkrok solvera),
   `ThermalSystem::tick`, `LightingSystem::tick`, `sim::World::step`. GPU: każdy przebieg z pomocników `dispatch`,
   `liquid_dispatch`, `neighbor_dispatch` i `mission_dispatch` (nazwy jak w śladzie przebiegów, np. `liquids 4`), cały
   tick fizyki, render, każdy kernel termiki (`thermal <op>`), kondensacja, prezentacja termiki, tick oświetlenia
   i VFX. `FrameMark` i zebranie wyników GPU są w `Window::present()` po zamianie buforów. Wykresy `physics_ms`
   i `gpu_ms` biorą te same wartości co HUD (pule `ParticleWorld`). Gdy powstanie `PassRunner` (A §3.5), strefy
   przejdą tam z pomocników dispatchu.
4. **RenderDoc** (API in-app, MIT, `licenses/RenderDoc-API-LICENSE.txt`). Nagłówek `renderdoc_app.h` z v1.47 jest
   pobierany z SHA-256, a bibliotekę ładujemy dynamicznie wyłącznie w StrefaQA. RenderDoc działa, gdy program
   uruchomiono z RenderDoc albo gdy `STREFA_RENDERDOC` wskazuje bibliotekę (`1` oznacza `renderdoc.dll` lub
   `librenderdoc.so` ze ścieżki wyszukiwania). F11 przechwytuje następną klatkę do `<profil>/renderdoc/`. Bez
   RenderDoc StrefaQA działa jak dotąd. Nie ładujemy go automatycznie, bo hook OpenGL zmienia czasy klatek.
5. **Nsight Graphics** to narzędzie zewnętrzne bez kodu w repozytorium. Procedura na GTX 1650 jest w `GPU_CHECK.md`.

## Konsekwencje

- `StrefaZero` w buildzie domyślnym nie ma żadnego symbolu Tracy. Pilnuje tego miara
  `strefa_zero_tracy_symbols = 0` w `tools/architecture_budget.json`, liczona przez `nm -C` w jobie
  `build-and-test`, tak jak symbole QA. Job `tracy` buduje StrefaQA z presetem `linux-dev` (ostrzeżenia jako błędy)
  i sprawdza, że `nm` widzi wtedy symbole Tracy, więc wzorzec nie może przestać działać po cichu.
- W buildzie deweloperskim `StrefaZero` też ma klienta Tracy, bo biblioteki są wspólne. Dlatego `cmake --install`
  z `STREFA_TRACY=ON` kończy się błędem i pakiet zawsze powstaje z OFF.
- Liczby z Tracy nie zastępują protokołu pomiaru (`CLAUDE.md`, „Protokół pomiaru”) ani pul kwerend: decyzje nadal
  wymagają ABBA i telemetrii. Oś czasu służy do znalezienia przyczyny, a liczba do raportu pochodzi z benchmarku.
- Kwerendy znaczników czasu Tracy (2 na strefę GPU, ok. 470 na klatkę misji) zmieniają czas klatki. Pierwszy zapis
  na GTX 1650 (mission UI smoke test, 421 ticków) pokazał, że `tracy::GpuCtx::Collect` po `present` zajmuje średnio
  9,6 ms CPU na klatkę (maksimum 35 ms), bo odczytuje każdą kwerendę osobno. Długości stref GPU są wtedy wiarygodne,
  ale oś czasu CPU i tempo klatek już nie. Pomiar z podłączonym profilerem nie jest pomiarem bazowym. Bez
  połączenia (`TRACY_ON_DEMAND`) `Collect` kończy się od razu.
- Nowe miejsca pomiaru dostają strefę z tego ADR zamiast nowej puli kwerend, chyba że liczba ma trafić do CSV testu.
