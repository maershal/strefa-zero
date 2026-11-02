## Goal

<!-- Why this change: the problem, decision or measurement behind it. One topic per PR. PR text and commits are in English. -->

## Changes

<!-- What exactly changes. Name every change of behaviour, default value or setting semantics explicitly. -->

## Tests

<!-- Tests run: platform, compiler, GPU driver, result (e.g. `./scripts/build.sh --test-suite cpu` 15/15). -->

## Risks

<!-- What can break, on which hardware, how to detect it and roll it back. -->

## Out of scope

<!-- What is deliberately left for later. -->

## Contracts touched?

<!-- Check every contract touched (CLAUDE.md › Contracts) and give a link to or quote of the owner's approval. If none, check the last item. -->

- [ ] SSBO 0-7 (bindings)
- [ ] 64-byte particle ABI
- [ ] `WorldCommand` / `WorldEvent` (64 B, align 16)
- [ ] Recordings v1-v3 (reading)
- [ ] GL 3.3 fallback
- [ ] GPU default options (thermal, lighting, phase gating, PBF restrict, …)
- [ ] Test thresholds and tolerances
- [ ] 15-option limit of StrefaZero
- [ ] None

## GPU tests not run locally

<!-- CTest names (e.g. `gpu.mission`, `app.opengl33_smoke`) or "none". -->

## Pre-merge checks

<!-- Check right before merging. The `pr-hygiene` workflow warns about unchecked items. -->

- [ ] CI finished and green (Linux, Windows, pr-hygiene)
- [ ] `/review-pr <number>` run, blockers and "major" findings resolved or explained
- [ ] Description matches the diff, one topic
- [ ] `./scripts/build.sh --test-suite cpu` and `./scripts/format.sh --check` green
- [ ] Raw measurements in strefa-zero-measurements, not in this repository
