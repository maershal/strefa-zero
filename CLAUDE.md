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
CLANG_FORMAT=clang-format-18 ./scripts/format.sh   # CI job `format` checks with clang-format-18; other versions differ
./scripts/lint.sh [files...]          # clang-tidy, advisory
python3 scripts/check_architecture.py [--report|--update]   # layer rules + size budgets (CI job `architecture`)
python3 scripts/golden.py --build-dir build/linux-release [--check]   # regenerate/compare tests/golden/<toolchain>.txt
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
  skips window tests, `-CoreOnly` builds into `build/<config>-core`.
- `sim.golden_master` compares per-field digests (material, mass, enthalpy, gases, velocity, other, plus the legacy
  checksum) with `tests/golden/<toolchain>.txt` (`gcc`, `clang`, `msvc`; raw IEEE bits, libm differs per toolchain).
  A failure names the case and fields and writes `Testing/Temporary/golden-actual.txt`; `scripts/golden.py` rewrites
  the reference and prints a Markdown diff for the PR. A CPU physics change updates all three files in the same PR:
  `gcc.txt` locally, `msvc.txt` and `clang.txt` from the failing CI run of the PR (artifacts
  `Windows-core-only-test-logs` and `Linux-clang-test-logs`, file `golden-actual.txt`, already in the reference format).

## Layout

| Path | Contents |
| --- | --- |
| `src/core` | settings schema (`meta::Field`), validation |
| `src/sim` | `strefa_sim`: SDL-free CPU world (thermal, MAC/PCG liquids, air, reactions, scenes), worker pool in `executor.hpp` |
| `src/game` | `strefa_input` (`input.hpp`: input buffer, actor state for the HUD; no CPU model), `strefa_gameplay` (`GameplaySession`, world protocol, pose/IK) |
| `src/gpu` | `strefa_gpu`: `ParticleWorld` (`world.cpp`, incl. the `*_validation.inl` member checks), renderer, VFX, thermal, lighting, readback/upload rings. The free `*_validation.cpp`/`*_profile.cpp`/`profiling.cpp` check modes (declared in `gpu/validation.hpp`) build into `strefa_diagnostics` |
| `src/platform` | SDL window, GL context, textures, settings storage |
| `src/ui` | ImGui menu, laboratories, mission HUD (`mission_hud.cpp`) |
| `src/app` | `strefa_app`: `Application`, `run_main`, launch options (`launch_options.cpp` = all CLI flags; `player_options` = the StrefaZero subset). `diagnostic_scenarios.cpp` and `*_validation.cpp` belong to `strefa_diagnostics` |
| `src/tools` | `qa_main.cpp` = `StrefaQA` (game + every QA/tuning flag, links `strefa_diagnostics`); `physics_runner.cpp` = `StrefaPhysics`. `src/main.cpp` = `StrefaZero` (player flags only) |
| `assets/shaders` | GLSL; every shader must also be listed in the `foreach(shader ...)` in `CMakeLists.txt`. `soldier_pose.glsl` is also `#include`d by `src/game/animation.hpp`, so it must stay valid C++ |
| `tests/` | one executable per `*_tests.cpp`, registered in `CMakeLists.txt` |
| `experiments/` | standalone measurement tools behind `STREFA_EXPERIMENT_*` options (default OFF, not installed); `pbf_cpu_bench.cpp` = CPU port of the two PBF passes (Z01 report, section 5) |
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
  A new boolean flag fails the ratchet (`launch_options_bools` is at its budget): a new check is a name in
  `gpu_checks` (`src/app/application.hpp`) run as `--gpu-check <name>`, a new switch takes a value `on|off`.
- A GPU pass (a dispatch or draw issued every tick or frame) is a resource: at game scale the tick cost follows the
  pass count, not the particle count. A PR that adds or removes a pass on the default path states
  "passes per tick: before -> after" in its description. The expected counts in `check_instrumentation`
  (`src/gpu/profiling.cpp`, CTest `gpu.instrumentation`) may go up only with the owner's approval comment in the PR.
  A new GPU system has from the start a path where the CPU knows there is no work and makes zero GL calls, not a
  dispatch of zero groups.

## Architecture ratchet

`scripts/check_architecture.py` stops architectural debt from growing; it fails CI, it does not fix anything.

- Layer rules on the `#include` graph of `src/` and on `target_link_libraries` (module = CMake target of the file;
  headers follow their `.cpp`, else their directory; `strefa_launch` = `launch`, `strefa_diagnostics`/`StrefaQA` =
  `qa`). Forbidden: `gpu` -> `game`/`ui`/`app`/`sim`; `sim` -> `gpu`/`ui`/`app`; anything -> `gpu` except `app` and
  `qa`; anything -> `sim` except `app`, `qa`, `tools`; anything -> `qa`. Today's violations are counted per edge in
  `tools/architecture_allowlist.txt`.
- `tools/architecture_budget.json`: sizes measured on `main`, lowered by `--update` whenever a PR shrinks one:
  `world.cpp` and its translation unit with the `.inl` files, `ParticleWorld` public declarations and test hooks,
  `application.cpp` and `Application::run()`, parser booleans, `.inl` includes, 320/192 literals (C++ incl. tests, GLSL),
  lines with the 2.65/3.3125 inverse-mass literals, `std::array<GLuint, 8>`, `unsigned& current|bank`, GLSL lines
  > 200 characters. Definitions are in the script; `--report` prints every value and edge.
- Every budget is at its limit today. `world.cpp`, the `.inl` files it includes and `application.cpp` cannot grow by
  a single line, `ParticleWorld` gets no new public declaration or test hook, and no new `.inl` include: new code goes
  into new `.cpp`/`.hpp` files. A new `strefa_*` CMake target needs its module in `TARGET_MODULE` in the script.
- `strefa_zero_qa_symbols`: `nm -C` lines matching `Benchmark|ScenarioRunner|write_profile_samples` in `StrefaZero`
  built with `-DSTREFA_IPO=OFF` (step in `build-and-test`; LTO would make the count unstable).
  `strefa_zero_tracy_symbols` (lines matching `tracy`, case-insensitive) comes from the same `nm` run and stays 0:
  Tracy exists only with `STREFA_TRACY=ON` (presets `dev`/`linux-dev`, ADR 0002); the `tracy` job checks it is found
  when linked. It is measured only on
  Linux/WSL2 with the GCC of the `ubuntu-24.04` runner (needs `nm` and an ELF binary; MSVC builds have no equivalent).
  On Windows, take the value from the step's log (`strefa_zero_qa_symbols = N`) and edit the JSON by hand. A runner
  image with a new GCC can move the count without a code change; record the new value in its own PR.
- Every value may only go down. A larger value or a new edge fails; a smaller one fails too until it is written back
  with `python3 scripts/check_architecture.py --update` (`--symbols <StrefaZero>` for the symbol count) in the same PR.
  On a PR the job also runs `--base origin/main`, so raising the allowlist or a budget in the PR itself fails.
  Raising one needs the owner's approval comment in the PR and the `architecture-debt-approved` label (set by the
  owner, never by an agent), which drops only the `--base` comparison. The job reads the labels when it runs, so a
  re-run of the `architecture` job after setting the label is enough.
- `scripts/test-check-architecture.sh` (job `format`) injects violations into a copy of the tree and checks the reported
  reason. Acceptance in #42: an artificial violation commit failed the `architecture` job and was then reverted.
  It edits the live tree with `sed`: the `#include "sim/material.hpp"` in `gpu/thermal.hpp`, the line
  `void apply_fluid_velocities_for_test();` in `gpu/world.hpp`, the `target_link_libraries(strefa_sim ...` line and
  the `include ui gpu` count. A PR that pays off exactly that debt or moves those lines fixes the self-test in the same PR.

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
  (MSVC `/WX` matrix: Release with CPU tests, core-only, both incl. `sim.golden_master`). A `changes` job skips
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
  `.github/workflows/pr-hygiene.yml` (`scripts/pr-hygiene.sh`, runnable locally, tests in
  `scripts/test-pr-hygiene.sh`) fails on a missing section, a checked contract without the URL of the owner's
  approval comment on its line, an "Out of scope" item without its issue (`#NNN`), a change under `src/` without a
  `docs/CHANGELOG.md` entry, an added file > 1 MB, raw data (`docs/measurements/**`, `*.csv`/`*.bmp` outside
  `assets/`) or > 800 changed code lines (files marked `linguist-generated` in `.gitattributes` do not count) without
  the `large-pr-approved` label.
- Review: `/review-pr <number>` after the last push. It waits for `gh pr checks --watch`, the `pr-reviewer` subagent
  reads the diff before the description and every commit since the previous automatic review, and the result is
  posted as a comment review. Findings of severity `minor` and up that a later push leaves unfixed become issues
  labelled `review-followup`, linked from the PR. Every "Out of scope" item gets its own issue before the PR is
  opened (pr-hygiene checks the `#NNN`). With nothing out of scope the section is the sentence "None." without a
  list marker (a list item without `#NNN` fails). Every item under "Pre-merge checks" must be checked.
- Ruleset `main`, target state (GitHub › Settings › Rules; set by the owner, not by an agent; not enforced yet, #31):
  changes only through a PR, required checks `linux-status`, `windows-status` and `hygiene` on an up-to-date branch,
  squash merge only, linear history, no force push or deletion, head branch deleted after merge.
- Merging: never on an agent's own decision, only after the owner accepts the PR (in the PR or in the chat for that
  PR), and always as a squash. While the ruleset is not enforced, `--auto` has no required checks to wait for and
  merges at once: wait until `gh pr checks <number>` shows no pending check (rerun `--watch` while any is pending) and
  every check passed, then run `gh pr merge <number> --squash`. Once the ruleset is enforced
  (`gh api repos/{owner}/{repo}/rulesets` shows `enforcement: active`), use `gh pr merge <number> --squash --auto`.

## Definition of done

- `./scripts/build.sh --test-suite cpu` is green (Windows: `pwsh ./scripts/build.ps1 -Configuration Release -CoreOnly`).
- `./scripts/format.sh --check` is clean.
- The PR description lists the GPU tests that were not run locally.
- A PR touching `src/gpu`, `assets/shaders`, `src/app` or `src/platform`: a green "Nightly GPU" run on its branch
  (every `graphics` test on Mesa, `gh workflow run nightly-gpu.yml --ref <branch>`), linked in the description.

## Contracts

- SSBO bindings 0-7, the 64-byte particle ABI, `WorldCommand`/`WorldEvent` (64 B, align 16);
  recordings v1-v3 must stay readable. The GL 3.3 fallback changes only with explicit approval.
- Never relax test thresholds or tolerances to make a test pass; report the failure instead.
- Removing a `sim.golden_master` case requires a replacement case covering the same path, or the owner's approval
  in a PR comment. A reference change needs the `scripts/golden.py` report and its reason in the PR description.

## Review v2 (direction)

The v2 review at `e12c39a` sets the direction for architecture, GPU thermal/gas work and the material laboratory.
Its status, accepted directions and pending owner decisions are imported below; the full documents are in
`docs/plans/reviews/` (`review_strefa_zero_v2.md`, `architecture_v2.md`, `lab_features.md`). `pr-reviewer` checks
PRs against it (checklist point 14). Treat the review as direction, not as an
instruction: the rules above win, and every `file:line` in it refers to `e12c39a`. When a PR closes a review item,
update the status table in the same PR.

@docs/plans/REVIEW_V2_STATUS.md
