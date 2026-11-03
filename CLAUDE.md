# Strefa Zero

C++23 2D game prototype with material simulation: GPU liquids (PBF) and grains (PBD) on
OpenGL 4.3 compute, a CPU reference thermochemical model, a mission and
two laboratories. SDL 3.4.18 + Dear ImGui 1.93.0 (fetched by CMake with pinned SHA-256).
Targets Windows (MSVC) and Linux/WSL2 (GCC >= 13). User-facing docs are in **Polish**;
keep new docs, UI strings and error messages in Polish; code identifiers, comments, commit messages and PR titles and
descriptions in English.

## Build and test

```bash
./scripts/build.sh --test-suite cpu   # configure + build build/linux-release, CPU tests (~1 min after first build)
./scripts/build.sh                    # + window/GPU smoke tests (needs a display; WSLg works)
./scripts/build.sh --test-suite all   # full QA, ~70 tests, many minutes, needs GL 4.3
./scripts/build.sh --core-only        # no SDL/OpenGL
./scripts/build.sh --core-only --configuration RelWithDebInfo --build-dir build/asan -- -DSTREFA_SANITIZE=address,undefined
ctest --test-dir build/linux-release -R '^gpu\.mission$' --output-on-failure   # one test
./scripts/format.sh                   # clang-format all C++ (CI runs --check)
./scripts/lint.sh [files...]          # clang-tidy, advisory
```

- Warnings are errors (`STREFA_WARNINGS_AS_ERRORS=ON` in scripts and presets).
- CPU tests run under `ctest -j`; every `graphics` test has `RESOURCE_LOCK gpu`, so GL tests stay serial. Configure
  fails if a test lacks `LABELS` or `TIMEOUT`: CPU tests use `strefa_add_cpu_test` (`unit`/`physics`/`determinism`/
  `golden`, `slow` = 300 s instead of 120 s, `PROCESSORS` = worker threads). CI window tests carry the `ci-smoke` label
  (`ci-gl33` = rerun under a GL 3.3 override); add the label instead of editing a regex. Tests whose verdict includes
  a wall-clock threshold carry `perf-wallclock`; `junit-summary.py` lists their failures apart from functional ones.
- GPU tests exit **77** (skipped) without an OpenGL 4.3 context; with `STREFA_REQUIRE_GL43=1` (set in CI) they exit 1
  instead (`platform::missing_gl43_exit_code()`; use it in new GPU test binaries). CI also fails on any skipped window
  test (`junit-summary.py --fail-on-skipped`). Software Mesa:
  `./scripts/run.sh --software` or `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`.
- Window tests rewrite `<profile>/progress.txt` (frames, ticks, stage) once per second, so a TIMEOUT kill leaves the
  last state. `-DSTREFA_GPU_TIMEOUT_SCALE=N` multiplies the TIMEOUT of `graphics` tests (nightly llvmpipe run only)
  and, through the environment variable of the same name that CTest sets, in-app deadlines (`gpu.mission_ui`).
- Most GPU/app tests are `StrefaQA --<mode>-check --profile <dir>` invocations registered in
  `CMakeLists.txt`; they write reports into `<dir>` and exit. Only `app.opengl_smoke`,
  `app.opengl33_smoke` and `gpu.mission_ui` run the shipped `StrefaZero` (`--smoke-test`, `--mission-smoke-test`).
  `gpu.mission_ui_quick` and `app.mode_faults` are the short StrefaQA window checks used in CI.
- Windows: `./scripts/build.ps1 -Configuration Release` (PowerShell 7, VS Build Tools, Ninja); `-TestSuite cpu`
  skips window tests, `-CoreOnly` builds into `build/<config>-core`. On MSVC it excludes `sim.golden_master`
  until P11 ([#15](https://github.com/maershal/strefa-zero/issues/15)).

## Layout

| Path | Contents |
| --- | --- |
| `src/core` | settings schema (`meta::Field`), validation |
| `src/sim` | `strefa_sim`: SDL-free CPU world (thermal, MAC/PCG liquids, air, reactions, scenes), worker pool in `executor.hpp` |
| `src/game` | `strefa_game` (input buffer, actor state for the HUD), `strefa_gameplay` (`GameplaySession`, world protocol, pose/IK) |
| `src/gpu` | `strefa_gpu`: `ParticleWorld` (`world.cpp`, incl. the `*_validation.inl` member checks), renderer, VFX, thermal, lighting, readback/upload rings. The free `*_validation.cpp`/`*_profile.cpp`/`profiling.cpp` check modes (declared in `gpu/validation.hpp`) build into `strefa_diagnostics` |
| `src/platform` | SDL window, GL context, textures, settings storage |
| `src/ui` | ImGui menu, laboratories, mission HUD (`arena.cpp`) |
| `src/app` | `strefa_app`: `Application`, `run_main`, launch options (`launch_options.cpp` = all CLI flags; `player_options` = the StrefaZero subset). `diagnostic_scenarios.cpp` and `*_validation.cpp` belong to `strefa_diagnostics` |
| `src/tools` | `qa_main.cpp` = `StrefaQA` (game + every QA/tuning flag, links `strefa_diagnostics`); `physics_runner.cpp` = `StrefaPhysics`. `src/main.cpp` = `StrefaZero` (player flags only) |
| `assets/shaders` | GLSL; every shader must also be listed in the `foreach(shader ...)` in `CMakeLists.txt`. `soldier_pose.glsl` is also `#include`d by `src/game/animation.hpp`, so it must stay valid C++ |
| `tests/` | one executable per `*_tests.cpp`, registered in `CMakeLists.txt` |
| `scripts/` | build/run/package/format/lint; `scripts/profiling/` = Windows-era PowerShell + Python measurement harnesses |
| `docs/` | guides at top level, `reports/` per iteration, `plans/`; raw measurements are in a separate repo (see below); index in `docs/README.md` |

Architecture: `docs/ARCHITECTURE.md`. Stage history: `docs/CHANGELOG.md`.

## Conventions

- Style is enforced by `.clang-format` (4 spaces, 120 columns). Run `./scripts/format.sh` before
  committing. X-macro lists (`GL_FUNCTIONS`, `STREFA_GPU_COUNTERS`) are hand-laid out one entry
  per line inside `// clang-format off` blocks; keep that when adding entries.
- `snake_case` functions/variables, `CamelCase` types, trailing `_` on private members,
  `namespace strefa::<module>`.
- Expected platform failures return `std::expected`; RAII for SDL/GL resources.
- Defaults matter: GPU options with unmet budgets stay **off** by default (thermal, lighting,
  phase gating, PBF restrict, ...). Do not flip a default without new measurements and a report.
- Clock policy (`src/sim/fixed_clock.hpp`): the shipped game and the GPU/CPU tools use
  `sim::interactive_clock` (frame delta clamped to 0.1 s, a stalled frame leaves no backlog, overload
  backlog <= 0.25 s, at most nominal ticks + 1 per frame and never more than 4, VSync deltas snapped to the refresh period);
  discarded time is counted in `dropped_seconds()` and shown in the HUD. Replays and validation modes keep
  `sim::exact_clock` (full backlog, I06). Reason: after a stall or under overload the exact backlog meant a
  2x fast-forward or a long slow motion. Outcomes depend on the tick index, not on wall time, so the policy
  changes pacing only; do not use the interactive policy where a check compares tick counts to wall time.
- Simulation is deterministic across 1/2/4 threads (`sim.numerics_parallel`); keep reductions
  in a fixed order.
- New CLI flags go in `src/app/launch_options.cpp` and are StrefaQA-only unless added to `player_options`
  (keep StrefaZero under 15 options; `app.launch_options` checks it). New check modes go in
  `run_diagnostic_scenario` and need a CTest entry running `strefa_qa` with `SKIP_RETURN_CODE 77` and a `gpu` label.

## Measurement data

Raw QA data (~12k logs/CSV/JSON/BMP files, 679 MB) lives in the separate repository
[maershal/strefa-zero-measurements](https://github.com/maershal/strefa-zero-measurements), byte-identical and
verified by a `SHA256SUMS.txt` manifest in each folder. Reports in `docs/reports/` quote every number needed and
link to it with permalinks pinned to a commit; there is no need to clone it to work on the code.

- New measurements do **not** go into this repository. Put a summary (table of at most 10 rows) in the report
  and the PR description; commit the raw data to the measurements repo (new `<stage>/` folder with its own
  manifest) or attach it as a CI artifact, and link it by permalink.
- `/.ignore` and the `deny` rules in `.claude/settings.json` still exclude `docs/measurements/` as a safeguard
  against a stale local copy.

## Protokół pomiaru

- Tools: `StrefaQA --scene-benchmark <lista|all> [--seconds N] [--vsync on|off] [--repeat N|--loop-minutes M]`, telemetry
  `scripts/profiling/GpuTelemetry.psm1`, soak `scripts/profiling/soak.ps1 [-SingleProcess] [-Baseline <csv>]`; procedure and
  metric definitions (budget share, frames by tick count) in `docs/maintenance/GPU_CHECK.md`.
- Laptop on AC power, balanced/high-performance plan recorded; start with a cooled GPU (idle >= 5 min, temperature noted).
- A/B comparisons: >= 3 repetitions in ABBA order, same build flags, same window size and VSync setting.
- Record telemetry for every run; discard windows with `hw_*` or `sw_thermal_slowdown` active (`soak.ps1` marks them
  `disqualified`) and say so. `sw_power_cap` is normal at a laptop's power limit: report it, compare at the same limit.
- Mesa/llvmpipe numbers (CTest `gpu.scene_benchmark`) are informational only, never a basis for decisions.
- Raw data goes to strefa-zero-measurements as above; the PR description gets a table of at most 10 rows.

## Git

- Default branch `main`; CI is `.github/workflows/linux.yml` (format check, build with ccache, CPU tests,
  Mesa `ci-smoke` tests, Linux package, `core-only` job, core-only `sanitizers`/`tsan`/`clang` (libc++)/`gcc-debug`
  matrix, JUnit summary; full TSan in the nightly `.github/workflows/nightly.yml`, every `graphics` test on Mesa in
  `.github/workflows/nightly-gpu.yml`, both also `workflow_dispatch`, not required) and `.github/workflows/windows.yml`
  (MSVC `/WX` matrix: Release with CPU tests, core-only; `sim.golden_master` excluded, #15). A `changes` job skips
  the builds for docs-only changes (`scripts/ci-changes.sh`; `docs/LINUX_WSL.md` and `docs/QUICKSTART.md` ship in
  the package and still count as code); `linux-status` / `windows-status` always report and are the checks to require.
- Do not commit `build/`, `dist/` (ignored) or raw measurement data (see above).
- To hide the bulk-format commit in `git blame`: `git config blame.ignoreRevsFile .git-blame-ignore-revs`.

## Workflow

- One change = one new branch = one PR, described as Goal / Changes / Tests / Risks / Out of scope.
  Commit messages, PR titles and PR descriptions are in English (older history is in Polish).
  Do not merge before CI is green. Add new `*.sh` files with `git update-index --chmod=+x`
  (CI rejects tracked `.sh` files that are not 100755, except the sourced `scripts/linux-env.sh`).
- Fill in `.github/pull_request_template.md` (all sections, contracts touched, GPU tests not run locally).
  `.github/workflows/pr-hygiene.yml` (`scripts/pr-hygiene.sh`, runnable locally) fails on a missing section, an added
  file > 1 MB, raw data (`docs/measurements/**`, `*.csv`/`*.bmp` outside `assets/`) or > 800 changed code lines
  without the `large-pr-approved` label.
- Before merging run `/review-pr <number>` (subagent `pr-reviewer`, posts a comment review). Do not merge while CI
  is red or not finished. Every item under "Pre-merge checks" in the PR description must be checked.

## Definition of done

- `./scripts/build.sh --test-suite cpu` is green (Windows: `pwsh ./scripts/build.ps1 -Configuration Release -CoreOnly`).
- `./scripts/format.sh --check` is clean.
- The PR description lists the GPU tests that were not run locally.

## Contracts

- SSBO bindings 0-7, the 64-byte particle ABI, `WorldCommand`/`WorldEvent` (64 B, align 16);
  recordings v1-v3 must stay readable. The GL 3.3 fallback changes only with explicit approval.
- Never relax test thresholds or tolerances to make a test pass; report the failure instead.
