## Goal

<!-- Po co ta zmiana: problem, decyzja albo pomiar, który ją uzasadnia. Jeden temat na PR. -->

## Changes

<!-- Co dokładnie się zmienia. Każda zmiana zachowania, wartości domyślnej lub semantyki ustawień wymieniona wprost. -->

## Tests

<!-- Uruchomione testy: platforma, kompilator, sterownik GPU, wynik (np. `./scripts/build.sh --test-suite cpu` 15/15). -->

## Risks

<!-- Co może się zepsuć, na jakim sprzęcie, jak to wykryć i wycofać. -->

## Out of scope

<!-- Co świadomie zostaje na później. -->

## Kontrakty dotknięte?

<!-- Zaznacz każdy dotknięty kontrakt (CLAUDE.md › Contracts) i podaj link lub cytat zgody właściciela. Jeśli żaden, zaznacz ostatnią pozycję. -->

- [ ] SSBO 0-7 (bindingi)
- [ ] ABI cząstki 64 B
- [ ] `WorldCommand` / `WorldEvent` (64 B, align 16)
- [ ] Nagrania v1-v3 (odczyt)
- [ ] Fallback GL 3.3
- [ ] Domyślne opcje GPU (termika, oświetlenie, phase gating, PBF restrict, …)
- [ ] Progi i tolerancje testów
- [ ] Limit 15 opcji StrefaZero
- [ ] Żaden

## Testy GPU nieuruchomione lokalnie

<!-- Nazwy testów CTest (np. `gpu.mission`, `app.opengl33_smoke`) albo „brak”. -->

## Kontrole przed scaleniem

<!-- Odhacz tuż przed merge. Workflow `pr-hygiene` ostrzega o nieodhaczonych pozycjach. -->

- [ ] CI ukończone i zielone (Linux, Windows, pr-hygiene)
- [ ] `/review-pr <numer>` uruchomiony, blokery i uwagi „major” rozwiązane lub opisane
- [ ] Opis zgodny z diffem, jeden temat
- [ ] `./scripts/build.sh --test-suite cpu` i `./scripts/format.sh --check` zielone
- [ ] Surowe pomiary w strefa-zero-measurements, nie w tym repo
