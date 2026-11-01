# Strefa Zero

C++23 2D game prototype with material simulation: GPU liquids (PBF) and grains (PBD) on
OpenGL 4.3 compute, a CPU reference thermochemical model, a mission, a training arena and
two laboratories. SDL 3.4.18 + Dear ImGui 1.93.0 (fetched by CMake with pinned SHA-256).
Targets Windows (MSVC) and Linux/WSL2 (GCC >= 13). User-facing docs are in **Polish**;
keep new docs, UI strings and error messages in Polish, code identifiers and comments in English.

## Build and test

```bash
./scripts/build.sh --test-suite cpu   # configure + build build/linux-release, CPU tests (~1 min after first build)
./scripts/build.sh                    # + window/GPU smoke tests (needs a display; WSLg works)
./scripts/build.sh --test-suite all   # full QA, ~70 tests, many minutes, needs GL 4.3
./scripts/build.sh --core-only        # no SDL/OpenGL
ctest --test-dir build/linux-release -R '^gpu\.mission$' --output-on-failure   # one test
./scripts/format.sh                   # clang-format all C++ (CI runs --check)
./scripts/lint.sh [files...]          # clang-tidy, advisory
```

- Warnings are errors (`STREFA_WARNINGS_AS_ERRORS=ON` in scripts and presets).
- GPU tests exit **77** (skipped) without an OpenGL 4.3 context. Software Mesa:
  `./scripts/run.sh --software` or `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`.
- Most GPU/app tests are `StrefaQA --<mode>-check --profile <dir>` invocations registered in
  `CMakeLists.txt`; they write reports into `<dir>` and exit. Only `app.opengl_smoke`,
  `app.opengl33_smoke` and `gpu.mission_ui` run the shipped `StrefaZero` (`--smoke-test`, `--mission-smoke-test`).
- Windows: `./scripts/build.ps1 -Configuration Release` (PowerShell 7, VS Build Tools, Ninja).

## Layout

| Path | Contents |
| --- | --- |
| `src/core` | settings schema (`meta::Field`), validation |
| `src/sim` | `strefa_sim`: SDL-free CPU world (thermal, MAC/PCG liquids, air, reactions, scenes), worker pool in `executor.hpp` |
| `src/game` | `strefa_game` (CPU arena session), `strefa_gameplay` (`GameplaySession`, world protocol, pose/IK) |
| `src/gpu` | `strefa_gpu`: `ParticleWorld` (`world.cpp`, incl. the `*_validation.inl` member checks), renderer, VFX, thermal, lighting, readback/upload rings. The free `*_validation.cpp`/`*_profile.cpp`/`profiling.cpp` check modes (declared in `gpu/validation.hpp`) build into `strefa_diagnostics` |
| `src/platform` | SDL window, GL context, textures, settings storage |
| `src/ui` | ImGui menu, laboratories, arena |
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

## Git

- Default branch `main`; CI is `.github/workflows/linux.yml` (format check, build, CPU tests,
  Mesa smoke tests, Linux package). Docs-only changes skip CI.
- Do not commit `build/`, `dist/` (ignored) or raw measurement data (see above).
- To hide the bulk-format commit in `git blame`: `git config blame.ignoreRevsFile .git-blame-ignore-revs`.

## Workflow

- One change = one new branch = one PR, described as Goal / Changes / Tests / Risks / Out of scope.
  Do not merge before CI is green. Add new `*.sh` files with `git update-index --chmod=+x`
  (CI rejects tracked `.sh` files that are not 100755, except the sourced `scripts/linux-env.sh`).

## Definition of done

- `./scripts/build.sh --test-suite cpu` is green (Windows: `pwsh ./scripts/build.ps1 -Configuration Release -CoreOnly`).
- `./scripts/format.sh --check` is clean.
- The PR description lists the GPU tests that were not run locally.

## Contracts

- SSBO bindings 0-7, the 64-byte particle ABI, `WorldCommand`/`WorldEvent` (64 B, align 16);
  recordings v1-v3 must stay readable. The GL 3.3 fallback changes only with explicit approval.
- Never relax test thresholds or tolerances to make a test pass; report the failure instead.
