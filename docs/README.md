# Dokumentacja

## Przewodniki

- [ARCHITECTURE.md](ARCHITECTURE.md) — własność świata, backend GPU, model CPU, kolejność kroku, testy.
- [PARTICLES.md](PARTICLES.md) — model fizyczny CPU (materiały, energia, ciecze, gazy).
- [GAMEPLAY.md](GAMEPLAY.md) — poligon, animacja i IK.
- [RESEARCH.md](RESEARCH.md) — źródła metod i granice inspiracji.
- [LINUX_WSL.md](LINUX_WSL.md) — budowanie i uruchamianie na Linuksie i WSL2 (instalowane jako README paczki Linux).
- [QUICKSTART.md](QUICKSTART.md) — instrukcja dołączana do paczki Windows.
- [QA_IMAGES.md](QA_IMAGES.md) — pobranie surowych obrazów QA z wydania.
- [CHANGELOG.md](CHANGELOG.md) — historia wydań i etapów I00–I11.

## Raporty — [reports/](reports/)

Raport każdego etapu: cel, kontrakt, walidacja, pomiary i otwarte ograniczenia.

| Etap | Raport |
| --- | --- |
| 0.4–0.6 | [REPORT](reports/REPORT.md), [PERFORMANCE_AUDIT](reports/PERFORMANCE_AUDIT.md), [LAB_PERFORMANCE](reports/LAB_PERFORMANCE.md), [GPU_MIGRATION](reports/GPU_MIGRATION.md), [GPU_LIQUIDS](reports/GPU_LIQUIDS.md), [PHYSICS_REVIEW_061](reports/PHYSICS_REVIEW_061.md) |
| 0.7 | [GPU_MISSION_070](reports/GPU_MISSION_070.md) |
| I00–I04 | [I00](reports/I00_GPU_PROFILING.md), [I01](reports/I01_GPU_READBACK.md), [I02](reports/I02_GPU_EXECUTION.md), [I03](reports/I03_GPU_NEIGHBORS.md), [I04](reports/I04_GPU_COUPLING.md) |
| I04.5 | [A/F](reports/I04_5_GPU_STABILIZATION.md), [B](reports/I04_5B_GPU_TRANSPORT.md), [C](reports/I04_5C_GPU_WORK.md), [D](reports/I04_5D_GPU_NEIGHBOR_READS.md), [E0](reports/I04_5E0_GPU_COSTS.md), [Q](reports/I04_5Q_GPU_COUPLING_QA.md), [G1](reports/I04_5G_GPU_PBF_GROUPS.md), [G2](reports/I04_5G2_GPU_PBF_RESTRICT.md), [G3-A](reports/I04_5G3_GPU_VARIANCE.md), [G3-B](reports/I04_5G3B_GPU_REPLAY.md) |
| I05–I11 | [I05](reports/I05_SHARED_WORLD.md), [I06](reports/I06_GAMEPLAY_PROTOCOL.md), [I07](reports/I07_FLUID_QUALITY.md), [I08](reports/I08_SCENE_RENDERER.md), [I09](reports/I09_GPU_VFX.md), [I10](reports/I10_GPU_THERMAL.md), [I11](reports/I11_LIGHTING.md) |
| Linux | [LINUX_WSL_QA](reports/LINUX_WSL_QA.md) |

## Utrzymanie — [maintenance/](maintenance/)

- [INVENTORY.md](maintenance/INVENTORY.md) — flagi, kod QA i testy CTest przed podziałem StrefaZero / StrefaQA.

## Plany — [plans/](plans/)

- [PLAN_ROZWOJU_GPU.md](plans/PLAN_ROZWOJU_GPU.md) — plan rozwoju i rejestr postępu.
- [PLAN_I10_DOMKNIECIE.md](plans/PLAN_I10_DOMKNIECIE.md) — domknięcie termiki I10.
- [REVIEW_I04_ROZSTRZYGNIECIA.md](plans/REVIEW_I04_ROZSTRZYGNIECIA.md) — rozstrzygnięcia review v0.7.0-i04.
- [reviews/](plans/reviews/) — oryginalne dokumenty review (bez zmian).

## Surowe pomiary — [strefa-zero-measurements](https://github.com/maershal/strefa-zero-measurements)

Logi, CSV, JSON, JUnit, migawki i manifesty `SHA256SUMS.txt` z każdego etapu leżą w osobnym
repozytorium, w katalogach odpowiadających raportom (`gpu-i00` … `gpu-i04-5g3b`, `i05` … `i11`,
`gpu050` … `gpu070`, `benchmarks`, `linux-wsl`, `legacy`). Pliki są przechowywane bajt w bajt
i nie powinny być edytowane — manifest w każdym katalogu je weryfikuje. Raporty cytują
potrzebne liczby i linkują do danych permalinkami do commita
[`88ba160`](https://github.com/maershal/strefa-zero-measurements/tree/88ba1601a3c6ab19b0e97a3ed8e679a593e09539); nie trzeba ich pobierać, aby pracować z kodem.
Nowe pomiary nie trafiają do tego repozytorium: raport i opis PR zawierają tabelę podsumowania,
a surowe dane idą do repozytorium pomiarów albo jako artefakt CI.
