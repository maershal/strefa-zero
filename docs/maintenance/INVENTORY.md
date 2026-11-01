# Inwentarz przed podziałem StrefaZero / StrefaQA

Stan na `origin/main` (e304e2b), przed refaktoryzacją `refactor/qa-split`. Numery linii odnoszą się do tego commitu.
Ścieżki są względne wobec `src/`. Skróty: `app.cpp` = `app/application.cpp`, `diag` = `app/diagnostic_scenarios.cpp`,
`world` = `gpu/world.cpp`.

## a) Flagi `src/app/launch_options.cpp` (90)

Struktura `LaunchOptions` jest w `app/application.hpp:15-49`, a parser w `app/launch_options.cpp:12-440`. Nie było
`--help`, `--version` ani rozmiaru okna dla gracza. Nieznana flaga daje błąd „Nieznany albo niepełny argument: …”
(`launch_options.cpp:394`), który `main.cpp:12` pokazuje w `SDL_ShowSimpleMessageBox`.

| flaga | grupa | domyślna | gdzie używana (plik:linia) |
|---|---|---|---|
| `--gpu-lighting-check` | QA | false | diag:21,33; main.cpp:20; app.cpp:852 |
| `--gpu-lighting-benchmark` | QA | false | diag:21,34; main.cpp:21 |
| `--gpu-lighting-quality off\|low\|high` | pokrętło | off; bez flagi z `settings.lighting_quality` (app.cpp:869-870) | app.cpp:868-870, 82; gpu/lighting_system.cpp:316; diag:32 |
| `--gpu-thermal-check` | QA | false | diag:10,15; main.cpp:19 |
| `--gpu-thermal-benchmark` | QA | false | diag:10,13; main.cpp:20 |
| `--gpu-thermal-scene fire\|oil\|quench\|phases` | pokrętło | 0 (brak); włącza thermal i start w GPU | app.cpp:978, 74, 698-700 |
| `--gpu-thermal on\|off` | pokrętło | off | app.cpp:977, 76, 319; diag:102 |
| `--gpu-vfx-check` | QA | false | diag:21,35,39; main.cpp:20 |
| `--gpu-vfx-benchmark` | QA | false | diag:21,36; main.cpp:20 |
| `--gpu-vfx-quality off\|low\|high` | pokrętło | low | app.cpp:975, 78; diag:31; gpu/vfx_pool.cpp:112 |
| `--gpu-render-check` | QA | false | diag:22,37; main.cpp:21 |
| `--gpu-render-benchmark` | QA | false | diag:22,37; main.cpp:21 |
| `--gpu-render-quality basic\|standard\|high` | pokrętło | basic | app.cpp:868,974, 80; diag:32 |
| `--session-smoke-test` | QA | false | app.cpp:982, 107, 314, 453; main.cpp:25 |
| `--shader-cache-check` | QA | false | diag:51; main.cpp:25 |
| `--shader-cache on\|off` | pokrętło | off | app.cpp:973, 517, 684; gpu/shader_library.cpp:186 |
| `--gpu-fluid-check` | QA | false | diag:45; main.cpp:21 |
| `--gpu-fluid-benchmark` | QA | false | diag:45-47; main.cpp:22 |
| `--gpu-fluid-profile legacy\|balanced` | pokrętło | legacy | world:116-123; gpu/fluid_options.hpp:31 |
| `--gpu-fluid-boundary unit\|volume` | pokrętło | unit | world:116,578; gpu/fluid_quality.cpp:122 |
| `--gpu-fluid-surface legacy\|akinci2d` | pokrętło | legacy | world:117; gpu/fluid_quality.cpp:248 |
| `--gpu-fluid-substeps 2\|4` | pokrętło | 2 | world:119, 918-923 |
| `--gpu-fluid-s-corr <f>` | pokrętło | 0.0001 | world:120 |
| `--gpu-fluid-tension-scale <f>` | pokrętło | 1 | world:121 |
| `--gpu-fluid-curvature-scale <f>` | pokrętło | 1 | world:122 |
| `--gpu-fluid-adhesion <f>` | pokrętło | 0 | world:123 |
| `--gpu-fluid-vorticity <f>` | pokrętło | 0 | gpu/fluid_options.hpp:22; gpu/fluid_quality.cpp:293 |
| `--smoke-test` | QA | false | app.cpp:980, 88, 450; main.cpp:16 |
| `--lab-smoke-test` | QA | false | app.cpp:981, 93-96, 451; main.cpp:16 |
| `--laboratory` | gracz | false | app.cpp:980, 94 |
| `--arena` | gracz | false | app.cpp:980, 95 |
| `--gpu-laboratory` | gracz | false | app.cpp:981, 100 |
| `--mission` | gracz | false | app.cpp:982, 107 |
| `--gameplay-check` | QA | false (ustawia mission_test i gameplay_test) | diag:113 |
| `--mission-test` | QA | false | diag:95,114; main.cpp:17 |
| `--mission-smoke-test` | QA | false | app.cpp:982, 343, 380, 415; main.cpp:17 |
| `--gpu-test` | QA | false | diag:95,117; main.cpp:16 |
| `--gpu-liquid-test` | QA | false | diag:95,116; main.cpp:16 |
| `--gpu-flow-test` | QA | false | diag:95,115; main.cpp:17 |
| `--gpu-smoke-test` | QA | false | app.cpp:981, 103, 409, 452; main.cpp:17 |
| `--opengl33` | gracz (zgodność) | false | app.cpp:892; platform/window.cpp:37 |
| `--gpu-work-check` | QA | false | diag:52-53; main.cpp:18 |
| `--gpu-transport-check` | QA | false | diag:57-58; main.cpp:18 |
| `--gpu-hot-check` | QA | false | diag:67-68; main.cpp:19 |
| `--gpu-cost-profile` | QA | false | diag:62-63; main.cpp:19 |
| `--gpu-cost-check` | QA | false | diag:62-63; main.cpp:19 |
| `--gpu-cost-mode off\|passes\|bounds\|snapshots` | QA (trace) | off | diag:63; gpu/cost_profile.cpp:299 |
| `--gpu-neighbor-reads full\|narrow\|sorted` | pokrętło | full | world:110; app/scenario_runner.hpp:112 |
| `--gpu-storage direct\|staged\|immutable\|auto` | pokrętło | direct | world:545; gpu/upload_ring.cpp:14 |
| `--gpu-phase-gating on\|off` | pokrętło | off | world:133; gpu/stabilization_profile.cpp:104 |
| `--gpu-zero-projection on\|off` | pokrętło | off | world:134; app/scenario_runner.hpp:118 |
| `--gpu-coupling-check` | QA | false | diag:72-75; main.cpp:24 |
| `--gpu-coupling-control` | QA | false (coupling_test + reference_control) | diag:74 |
| `--gpu-coupling-legacy-check` | QA | false (coupling_test + legacy_check) | diag:75 |
| `--gpu-coupling-profile` | QA | false | diag:72-74; main.cpp:25 |
| `--gpu-coupling reference\|local\|combined` | pokrętło | combined | world:131,206,372 |
| `--gpu-neighbor-check` | QA | false | diag:79-80; main.cpp:24 |
| `--gpu-neighbor-profile` | QA | false | diag:79-80; main.cpp:24 |
| `--gpu-neighbors linked\|sorted\|reused` | pokrętło | reused | world:129-130,204,400 |
| `--gpu-layout aos\|soa` | pokrętło | soa | world:128,377 |
| `--gpu-execution-check` | QA | false | diag:84-86; main.cpp:23 |
| `--gpu-pbf-restrict-check` | QA | false (execution_test + pbf_restrict_test) | diag:85 |
| `--gpu-execution-profile` | QA | false | diag:84-86; main.cpp:23 |
| `--gpu-execution reference\|optimized` | pokrętło | optimized; nadpisuje całe `gpu_execution` z fluid (kolejność flag ma znaczenie) | app.cpp:971, 683; diag:11 |
| `--gpu-pbf-restrict on\|off` | pokrętło | off | world:176; app/session_recording.cpp:19 |
| `--gpu-local-size 64\|128\|256` | pokrętło | 256 | world:160,166 |
| `--gpu-reduction-size 64\|128\|256` | pokrętło | 256 | world:132,164-165 |
| `--gpu-pbf-size 64\|128\|256` | pokrętło | 0 (= local_size) | world:162,170 |
| `--gpu-readback-check` | QA | false | diag:90-91; main.cpp:22 |
| `--gpu-readback-profile` | QA | false | diag:90-91; main.cpp:23 |
| `--gpu-readback-mode auto\|legacy\|stream\|staging\|persistent` | pokrętło | auto | world:618,1272; gpu/stabilization_profile.cpp:94 |
| `--gpu-profile` | QA | false | diag:108-109; main.cpp:18 |
| `--gpu-stabilization-profile` | QA | false | diag:103-104; main.cpp:18 |
| `--gpu-profile-check` | QA | false | diag:108-109; main.cpp:22 |
| `--gl-debug` | QA | false (w Debug zawsze) | app.cpp:892,898; platform/window.cpp:42-44 |
| `--show-test-window` | QA | false | app.cpp:894 |
| `--gpu-counters` | QA | false | app.cpp:114,259,532 |
| `--gpu-timer-mode elapsed\|timestamp\|off` | QA | elapsed | world:620,863; app.cpp:345 |
| `--test-unpaced` | QA | false | app.cpp:976, 145, 456 |
| `--test-window-size W H` | QA | brak | app.cpp:888-890 |
| `--test-vsync on\|off` | QA | brak (testy wymuszają false, app.cpp:883) | app.cpp:886 |
| `--test-frame-limit 30-240` | QA | brak (testy wymuszają 60, app.cpp:884) | app.cpp:887 |
| `--gpu-group-timers` | QA | false (włącza też collect) | world:867,543 |
| `--gpu-no-timers` | QA | gpu_timers=true | gpu/profiling.hpp:100; app/scenario_runner.hpp:109 |
| `--gpu-no-coupling` | pokrętło (ablacja) | body_coupling=true | world:444; app/scenario_runner.hpp:224 |
| `--gpu-no-diagnostics` | pokrętło (ablacja) | periodic_diagnostics=true | world:960 |
| `--gpu-readback-period 0\|1\|2\|4` | pokrętło | 2 | world:515-516; app/scenario_runner.hpp:117 |
| `--physics-threads 1-4` | pokrętło | 0 (auto) | app.cpp:958; sim/physics.hpp:58; app/scenario_runner.hpp:158 |
| `--test-seconds 1-120` | QA | 0 | app.cpp:979, 75, 379-380 |
| `--profile <dir>` | gracz (wymagany w testach) | `SDL_GetPrefPath("StrefaZero","Prototype")` | app.cpp:844-851; main.cpp:27-28 |

Konstruktor `Application` (`app.cpp:50-55`) miał 21 parametrów, a `Application::run` (`app.cpp:85-87`) 9 booli
(`smoke_test`, `start_in_laboratory`, `start_in_arena`, `lab_smoke`, `start_gpu`, `gpu_smoke`, `start_mission`,
`mission_smoke`, `session_smoke`). Lista ~36 booli trybów testowych była powtórzona w `launch_options.cpp:397-405`
i `424-432`, `main.cpp:16-25` oraz `app.cpp:852-862`, `875-882`, `908-916`.

## b) Kod walidacji i profilowania linkowany do StrefaZero (~8,3 tys. linii)

| plik | linie | target | kto wywołuje (plik:linia) |
|---|---|---|---|
| gpu/validation.cpp (`validate_granular`) | 213 | strefa_gpu | diag:117 |
| gpu/liquid_validation.cpp (`validate_liquids`) | 245 | strefa_gpu | diag:116 |
| gpu/flow_validation.cpp (`validate_flow`) | 245 | strefa_gpu | diag:115 |
| gpu/mission_validation.cpp (`validate_mission`, `scripted_mission_input`) | 333 | strefa_gpu | diag:114; `scripted_mission_input`: **app/simulation_world.cpp:90**, gpu/coupling_validation.cpp:343, gpu/vfx_validation.inl:274,293 |
| gpu/profiling.cpp (`profile_gpu`) | 441 | strefa_gpu | diag:109 |
| gpu/profiling.hpp (typy runtime: `ExecutionOptions`, `ProfileOptions`, tryby) | 170 | nagłówek | gpu/world.hpp:5, gpu/gl_api.hpp:2, app/application.hpp:3 i dalej |
| gpu/stabilization_profile.cpp (`profile_stabilization`) | 217 | strefa_gpu | diag:104 |
| gpu/cost_profile.cpp (`profile_cost`) | 457 | strefa_gpu | diag:63 |
| gpu/work_validation.cpp (`validate_work`) | 226 | strefa_gpu | diag:53 |
| gpu/hot_validation.cpp (`validate_hot_reads`) | 206 | strefa_gpu | diag:68 |
| gpu/transport_validation.cpp (`validate_transport`) | 218 | strefa_gpu | diag:58 |
| gpu/readback_validation.cpp (`validate_readback`, `profile_readback`) | 337 | strefa_gpu | diag:91 |
| gpu/execution_validation.cpp (`validate_execution`) | 370 | strefa_gpu | diag:86 |
| gpu/neighbor_validation.cpp (`validate_neighbors`) | 334 | strefa_gpu | diag:80 |
| gpu/coupling_validation.cpp (`validate_coupling`) | 414 | strefa_gpu | diag:74 |
| gpu/coupling_replay.cpp (`validate_coupling_replay`) | 182 | strefa_gpu | gpu/coupling_validation.cpp:282 |
| gpu/pbf_access_validation.cpp (`validate_pbf_restrict`) | 142 | strefa_gpu | diag:85 |
| gpu/render_validation.inl (`ParticleWorld::validate_renderer`) | 425 | strefa_gpu (world.cpp:2017) | diag:37 |
| gpu/vfx_benchmark.inl (`run_vfx_benchmark`) | 164 | strefa_gpu (world.cpp:2018) | gpu/vfx_validation.inl:308 |
| gpu/vfx_replay.inl (`validate_vfx_replay`) | 108 | strefa_gpu (world.cpp:2019) | gpu/vfx_validation.inl:301 |
| gpu/vfx_validation.inl (`validate_vfx`) | 313 | strefa_gpu (world.cpp:2020) | diag:36 |
| gpu/lighting_validation.inl (`validate_lighting`) | 526 | strefa_gpu (world.cpp:2091) | diag:34 |
| gpu/thermal_benchmark.inl (`run_thermal_benchmark`) | 128 | strefa_gpu (world.cpp:2092) | gpu/thermal_validation.inl:5 |
| gpu/thermal_validation.inl (`validate_thermal`) | 336 | strefa_gpu (world.cpp:2094) | diag:13; tests/thermal_gpu_tests.cpp:30 |
| gpu/diagnostic_trace.hpp (`detail::DiagnosticTrace`) | 226 | nagłówek, tylko world.cpp | pole w `ParticleWorld::Impl` (world.cpp:74), `trace.begin` w każdym kroku; API wołane z cost_profile.cpp |
| gpu/diagnostics.hpp (`CostMode`, `TraceConfig`, …) | 54 | nagłówek | gpu/world.hpp:3, app/application.hpp:2 |
| app/diagnostic_scenarios.cpp/.hpp (`run_diagnostic_scenario`) | 139 | strefa_zero | app.cpp:931 |
| app/thermal_validation.cpp (`validate_thermal_session`) | 96 | strefa_zero | diag:16 |
| app/cpu_vfx_validation.cpp (`validate_cpu_vfx`) | 75 | strefa_zero | diag:40 |
| app/shader_cache_validation.cpp (`validate_shader_cache`) | 73 | strefa_zero | diag:51 |
| app/scenario_runner.hpp (`ScenarioRunner`, smoke testy UI) | 393 | strefa_zero (nagłówek) | app.cpp:6, 47 (friend), 114, 180, 315-316, 465, 475 |
| app/frame_profile.hpp (`FrameProfile`) | 56 | strefa_zero (nagłówek) | app.cpp:5, 121-463 (pętla klatki) |

Zależności kodu gry od tych plików: `simulation_world.cpp:90` (trasa skryptowa misji, tylko w smoke testach UI),
typy konfiguracji z `profiling.hpp`/`diagnostics.hpp`, deklaracje `validate_*` w `gpu/world.hpp:181-210`,
pliki `.inl` i `DiagnosticTrace` wewnątrz `world.cpp`, `ScenarioRunner` jako friend `Application`.

## c) Testy CTest (70; 69 bez `STREFA_EXPERIMENTAL_REFLECTION`)

Wszystkie testy StrefaZero dostają też `--profile <build>/<nazwa>-profile`. Rejestracja: bezpośrednie `add_test`
w `CMakeLists.txt:129-305` (pętle `foreach` 207-283), `experiment.reflection` w `cmake/Reflection.cmake:33`.
„UI” = tryb przechodzi przez produkcyjne menu/`Application::run`.

| nazwa | etykiety | timeout | binarka i tryb | UI |
|---|---|---|---|---|
| graphics.lighting_contract | – | domyślny | strefa_lighting_tests | nie |
| game.vfx_contract | – | domyślny | strefa_vfx_tests | nie |
| physics.fluid_reference | – | domyślny | strefa_fluid_tests | nie |
| game.protocol | – | domyślny | strefa_gameplay_tests | nie |
| game.clock | – | domyślny | strefa_clock_tests | nie |
| core.settings | – | domyślny | strefa_core_tests | nie |
| sim.materials | – | domyślny | strefa_sim_tests | nie |
| sim.numerics_parallel | – | domyślny | strefa_numerics_tests | nie |
| game.training | – | domyślny | strefa_game_tests | nie |
| sim.laboratory_worker | – | domyślny | strefa_laboratory_tests | nie |
| physics.thermal_contract | – | domyślny | strefa_thermal_tests | nie |
| experiment.reflection (opcjonalny) | – | domyślny | strefa_reflection_probe | nie |
| gpu.platform_dispatch | integration;graphics;gpu | 60 | strefa_gpu_platform_tests | nie |
| gpu.lighting_pressure | integration;graphics;gpu | 120 | strefa_lighting_pressure_tests | nie |
| gpu.thermal_edges | integration;graphics;gpu | 900 | strefa_thermal_gpu_tests | nie |
| app.opengl_smoke | integration;graphics | 30 | StrefaZero `--smoke-test` | tak |
| app.opengl33_smoke | integration;graphics | 30 | StrefaZero `--smoke-test --opengl33` | tak |
| app.laboratory_responsiveness | integration;graphics;performance | 30 | StrefaZero `--lab-smoke-test` | tak |
| app.laboratory_four_threads | integration;graphics;performance | 30 | StrefaZero `--lab-smoke-test --physics-threads 4` | tak |
| app.shared_session | integration;graphics;gpu | 90 | StrefaZero `--session-smoke-test` | tak |
| gpu.shader_cache | integration;graphics;gpu | 90 | StrefaZero `--shader-cache-check` | nie |
| gpu.thermal_ui | integration;graphics;gpu | 180 | StrefaZero `--gpu-smoke-test --gpu-thermal on --gl-debug` | tak |
| gpu.thermal | integration;graphics;gpu | 900 | StrefaZero `--gpu-thermal-check --gl-debug` | nie |
| gpu.vfx | integration;graphics;gpu | 600 | StrefaZero `--gpu-vfx-check --gl-debug` | nie |
| gpu.lighting | integration;graphics;gpu | 600 | StrefaZero `--gpu-lighting-check --gl-debug` | nie |
| gpu.renderer | integration;graphics;gpu | 180 | StrefaZero `--gpu-render-check --gl-debug` | nie |
| gpu.fluid_quality | integration;graphics;gpu | 600 | StrefaZero `--gpu-fluid-check --gpu-fluid-profile balanced` | nie |
| gpu.gameplay | integration;graphics;gpu | 180 | StrefaZero `--gameplay-check` | nie |
| gpu.mission | integration;graphics;gpu | 600 | StrefaZero `--mission-test` | nie |
| gpu.pbf_64_liquid | integration;graphics;gpu | 600 | StrefaZero `--gpu-liquid-test --gpu-pbf-size 64` | nie |
| gpu.pbf_64_mission | integration;graphics;gpu | 600 | StrefaZero `--mission-test --gpu-pbf-size 64` | nie |
| gpu.pbf_64_coupling | integration;graphics;gpu | 600 | StrefaZero `--gpu-coupling-check --gpu-pbf-size 64` | nie |
| gpu.pbf_128_liquid | integration;graphics;gpu | 600 | StrefaZero `--gpu-liquid-test --gpu-pbf-size 128` | nie |
| gpu.pbf_128_mission | integration;graphics;gpu | 600 | StrefaZero `--mission-test --gpu-pbf-size 128` | nie |
| gpu.pbf_128_coupling | integration;graphics;gpu | 600 | StrefaZero `--gpu-coupling-check --gpu-pbf-size 128` | nie |
| gpu.pbf_restrict_bindings | integration;graphics;gpu | 600 | StrefaZero `--gpu-pbf-restrict-check --gpu-pbf-restrict on` | nie |
| gpu.pbf_restrict_liquid | integration;graphics;gpu | 600 | StrefaZero `--gpu-liquid-test --gpu-pbf-restrict on` | nie |
| gpu.pbf_restrict_mission | integration;graphics;gpu | 600 | StrefaZero `--mission-test --gpu-pbf-restrict on` | nie |
| gpu.pbf_restrict_coupling | integration;graphics;gpu | 600 | StrefaZero `--gpu-coupling-check --gpu-pbf-restrict on` | nie |
| gpu.pbf_restrict_hot_{aos,soa}_{sorted,reused} (4) | integration;graphics;gpu | 600 | StrefaZero `--gpu-hot-check --gpu-pbf-restrict on --gpu-layout … --gpu-neighbors …` | nie |
| gpu.instrumentation | integration;graphics;gpu | 60 | StrefaZero `--gpu-profile-check` | nie |
| gpu.hot_{aos,soa}_{sorted,reused} (4) | integration;graphics;gpu | 600 | StrefaZero `--gpu-hot-check --gpu-layout … --gpu-neighbors …` | nie |
| gpu.hot_mission_narrow | integration;graphics;gpu | 600 | StrefaZero `--mission-test --gpu-neighbor-reads narrow` | nie |
| gpu.hot_mission_sorted | integration;graphics;gpu | 600 | StrefaZero `--mission-test --gpu-neighbor-reads sorted` | nie |
| gpu.hot_immutable | integration;graphics;gpu | 600 | StrefaZero `--gpu-hot-check --gpu-storage auto` | nie |
| gpu.cost_aos | integration;graphics;gpu | 120 | StrefaZero `--gpu-cost-check --gpu-layout aos` | nie |
| gpu.cost_soa | integration;graphics;gpu | 120 | StrefaZero `--gpu-cost-check --gpu-layout soa` | nie |
| gpu.cost_immutable | integration;graphics;gpu | 120 | StrefaZero `--gpu-cost-check --gpu-storage auto` | nie |
| gpu.transport | integration;graphics;gpu | 120 | StrefaZero `--gpu-transport-check` | nie |
| gpu.transport_mission | integration;graphics;gpu | 600 | StrefaZero `--mission-test --gpu-storage auto` | nie |
| gpu.work_gating | integration;graphics;gpu | 600 | StrefaZero `--gpu-work-check` | nie |
| gpu.work_mission | integration;graphics;gpu | 600 | StrefaZero `--mission-test --gpu-phase-gating on --gpu-zero-projection on` | nie |
| gpu.work_gating_{aos_sorted,aos_reused,soa_sorted} (3) | integration;graphics;gpu | 600 | StrefaZero `--gpu-work-check --gpu-layout … --gpu-neighbors …` | nie |
| gpu.coupling | integration;graphics;gpu | 600 | StrefaZero `--gpu-coupling-check` | nie |
| gpu.neighbors | integration;graphics;gpu | 120 | StrefaZero `--gpu-neighbor-check` | nie |
| gpu.execution | integration;graphics;gpu | 120 | StrefaZero `--gpu-execution-check` | nie |
| gpu.readback_ring | integration;graphics;gpu | 60 | StrefaZero `--gpu-readback-check` | nie |
| gpu.mission_ui | integration;graphics;gpu;performance | 60 | StrefaZero `--mission-smoke-test` | tak |
| gpu.granular_physics | integration;graphics;gpu | 180 | StrefaZero `--gpu-test` | nie |
| gpu.liquid_physics | integration;graphics;gpu | 240 | StrefaZero `--gpu-liquid-test` | nie |
| gpu.flow_behaviour | integration;graphics;gpu | 600 | StrefaZero `--gpu-flow-test` | nie |
| gpu.laboratory_ui | integration;graphics;gpu | 45 | StrefaZero `--gpu-smoke-test` | tak |

Wszystkie testy GPU i okienkowe mają `SKIP_RETURN_CODE 77`, z wyjątkiem `app.opengl_smoke`, `app.opengl33_smoke`
i `app.laboratory_*`. `scripts/build.sh`: `cpu` = `-LE graphics` (testy bez etykiet), `smoke` = `cpu` +
`^(app\.opengl(33)?_smoke|gpu\.(platform_dispatch|gameplay|laboratory_ui))$`, `all` = wszystkie testy.
