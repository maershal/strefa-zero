# Termika GPU: dokładna bramka etapu gazu

Pomiar z 2026-11-14 na GTX 1650 (laptop, i5-9300HF, sterownik 610.47, zasilanie sieciowe, plan „Zrównoważony”).
Kandydat 1 z planu bitowo neutralnych optymalizacji termiki (#96), po narzędziu `--gpu-check thermal-bits` (#97).
Surowe dane: [thermal-gas-gate-2026-11-14](https://github.com/maershal/strefa-zero-measurements/tree/34ce4d02dbea73ed1f9d3e1fb5659722720de4f9/thermal-gas-gate-2026-11-14).

## Odpowiedź w skrócie

- Gdy gaz jest w spoczynku, etap gazu (37 z 51 dispatchy ticku termiki, w tym 32 iteracje Jacobiego) jest pomijany
  przez komendy indirect z zerem grup. Wynik jest bitowo identyczny z `main`.
- Zysk: **−1,17 ms na tick w scenie zimnej (−17,1%)** i **−1,18 ms w pustym świecie (−33,3%)**.
- Koszt w scenach aktywnych, gdzie gaz nigdy nie stoi: **+0,15 ms w scenie ognia (+4,3%)** i **+0,16 ms
  w gaszeniu (+2,7%)**.
- Pominięcie całego ticku termiki (pierwotna wersja kandydata 1) nie jest dokładne w scenach z cząstkami. Pominięcie
  samego etapu gazu jest dokładne zawsze.
- Termika pozostaje domyślnie wyłączona. Bramka (`ThermalOptions::gas_gate`) jest domyślnie włączona, gdy termika
  działa.

## Dlaczego nie cały tick

`thermal-bits` (sekcja 4c w `GPU_CHECK.md`) pokazuje dwie rzeczy.

1. **Tick termiki w równowadze jest dokładnym punktem stałym.** W pustym świecie z termiką kolejny tick nie zmienia
   ani jednego słowa przez 3600 ticków. W scenie zimnej (sand_water) cząstki termiki i pola energii, gazu i przepływu
   komórek nie zmieniają się. Zmienia się tylko pole `extra` (temperatura i materiał cząstki w komórce), bo cząstki
   wciąż się ruszają.
2. **Cząstki świata nie stają nigdy.** Po 3600 tickach (30 s) jeden tick zmienia ok. 63 tys. słów cząstek świata
   w scenie zimnej. Ticki termiki kopiują całą pulę cząstek świata do drugiego banku (op 6, 17, kondensacja), a CPU
   przełącza bank bezwarunkowo. Dispatch z zerem grup pominąłby kopię, a świat czytałby nieaktualny bank. Pominięcie
   całego ticku byłoby więc dokładne tylko w świecie bez ruchu, czyli w praktyce tylko w pustym.

Etap gazu (ops 9, 14, 10 × 32, 11, 12, 13 w `thermal.comp`) czyta tylko `gas0`, `gas1`, `flow` i ściany, nie używa
atomików i nie zależy od cząstek. W scenie zimnej jego wejście jest od pierwszego ticku stałe.

## Jak działa bramka

Etap jest pomijany tylko wtedy, gdy jednocześnie:

- jego wejście jest bitowo równe wejściu ostatniego uruchomienia (op 8 porównuje je z migawką i ją odświeża;
  migawka to osobny bufor, 64 B na komórkę, przypięty do bindingu 3 tylko dla op 8, który nie czyta cząstek termiki),
- op 2 nie widział zmiany terenu,
- ostatnie uruchomienie nie zmieniło żadnego słowa (ops 9, 11, 12, 13 zgłaszają każdą zmianę),
- op 8 nie zmienił żadnego słowa rekordu komórki. Oba banki komórek mają wtedy te same rekordy, więc zamiana banków
  po pominiętym etapie czyta te same dane,
- CPU nie wymusił uruchomienia (zmiana opcji).

Decyzję podejmuje jeden wątek (`thermal_gate.comp`), który zapisuje komendę indirect dla 37 kerneli gazu. Na tick
dochodzi jeden dispatch (51 → 52). Przy gazie w spoczynku 37 z nich ma zero grup. Domyślna ścieżka gry (termika
wyłączona) bez zmian. Zgodnie z decyzją właściciela ([komentarz w PR](https://github.com/maershal/strefa-zero/pull/98#issuecomment-6880001851)) to wyjątek od reguły „bez dispatchy z zerem grup” w `CLAUDE.md`.

## Dowód bitowy

| Sprawdzenie | Wynik |
| --- | --- |
| `thermal-bits.txt` (`fire`, `idle`, 600 ticków), `main` 5a00e2c wobec bramki | identyczne, 0 z 26 wierszy różnych |
| Ten sam proces: przebieg z bramką wobec przebiegu audytu, w którym etap liczy się zawsze (`fire`, `idle`) | identyczne |
| Pominięte etapy gazu w `idle` / `cold` / `fire` / `oil` / `quench` / `phases` | 600/601, 600/731, 0, 0, 0, 0 |
| Audyt (`gate_audit`: etap liczy się zawsze, bramka liczy słowa zmienione przez etap, który by pominęła) | 0 we wszystkich 7 scenach |
| Mutacja: bramka pomija zawsze | audyt: 7–27 mln zmienionych słów w scenach aktywnych, 0 w `idle`, test odrzuca |
| Mutacja: bramka nie pomija nigdy | test odrzuca (brak pominięć w pustym świecie) |
| Mesa llvmpipe (WSL2, 120 ticków) | test przechodzi; maksymalny RSS 1,08 GB, 328 s |

Sceny z cząstkami nie powtarzają się bitowo między przebiegami nawet na `main` (#95), więc dla nich dowodem jest
audyt, a nie porównanie skrótów.

## Pomiar ABBA

`StrefaQA --gpu-check thermal-gate` przez `scripts/profiling/profile-thermal-gate.ps1` (sekcja 4d w `GPU_CHECK.md`):
4 bloki z losową kolejnością scen, w każdej scenie kwadrat ABBA (bramka wyłączona, włączona, włączona, wyłączona),
świeży świat, 120 ticków rozgrzewki, 240 mierzonych, para `GL_TIMESTAMP` wokół `step()`. Przed każdym odcinkiem
i po nim działały sondy kalibracyjne Z01 z bramką stygnięcia. 300 s bezczynności przed startem (GPU 45 °C, maksimum
66 °C). Telemetria co 100 ms: 0 z 64 odcinków zdyskwalifikowanych. `sw_power_cap` był aktywny w 60 odcinkach, co
jest normalne na limicie mocy laptopa (obie wersje w tych samych warunkach). Wersja końcowa 438243f (seria `v3`
w danych). Wartości to mediany median odcinków.
Efekt to mediana z 4 kwadratów: średnia (włączona) minus średnia (wyłączona).

| Scena | Tick bez bramki [ms] | Tick z bramką [ms] | Efekt [ms] | Efekt [%] | Zakres efektu w 4 kwadratach [ms] |
| --- | --- | --- | --- | --- | --- |
| cold | 6,847 | 5,671 | −1,172 | −17,1 | −1,188…−1,158 |
| idle | 3,541 | 2,376 | −1,178 | −33,3 | −1,181…−1,163 |
| fire | 3,475 | 3,626 | +0,148 | +4,3 | +0,121…+0,157 |
| quench | 5,795 | 5,938 | +0,155 | +2,7 | +0,140…+0,161 |

Dwie wcześniejsze wersje, w tym samym pomiarze:

| Wersja | cold [ms] | idle [ms] | fire [ms] | quench [ms] |
| --- | --- | --- | --- | --- |
| v1: osobne pełne przebiegi porównania przed etapem i po nim | −1,122 | −1,142 | +0,190 | +0,215 |
| v2: porównania w op 8 i kernelach gazu, migawka 48 B w bloku `ThermalAux` | −1,215 | −1,204 | +0,115 | +0,147 |
| v3 (końcowa): migawka 64 B w osobnym buforze | −1,172 | −1,178 | +0,148 | +0,155 |

v2 była najszybsza, ale na Mesa llvmpipe sama deklaracja tablicy migawki w bloku `ThermalAux` (także w programach,
które jej nie używają) podnosiła pamięć procesu o gigabajty. W nocnym CI runnery się wyłączały, a w WSL2
`thermal-bits` kończył się OOM przy 7,6 GB. v3 przenosi migawkę do osobnego bufora i ma pamięć jak `main`
(pomiar llvmpipe w danych: `bits/llvmpipe-438243f`).

Koszt w scenach aktywnych nie jest rozłożony na składniki. Kandydaci: odczyt i zapis migawki w op 8 (64 B na
komórkę), ponowne odczyty pól w ops 9, 11, 12, 13 i 37 dispatchy indirect zamiast bezpośrednich.

## Ograniczenia

- Scena ognia po 30 s wciąż ma ruch gazu w każdej komórce, więc bramka tam nie działa. Zysk mają światy, w których gaz
  się uspokoił. Pierwszy tick po każdej zmianie (pędzel, wybuch, kondensacja, edycja terenu) uruchamia etap.
- Bramka dotyczy tylko etapu gazu. Etapy ciepła i reakcji, integracja w `ParticleWorld::step` (dwa `rebuild_active()`,
  teren, kondensacja) i przebiegi po wszystkich 32 768 slotach zostają; to kandydaci 2 i 3 z #96, zablokowani dla
  dowodu bitowego przez #95.
- Benchmark termiki I10 (`--gpu-thermal-benchmark`) mierzy etap gazu bez bramki (`gas_gate = false`), bo jego
  analizator traktuje `gas_ms` jako sondę zegara niezależną od sceny. Jego liczby pozostają porównywalne z I10.
- Pomiar w jednym procesie: A i B to ten sam plik wykonywalny z opcją wyłączoną i włączoną. Przy wyłączonej bramce
  shader omija wszystkie jej porównania (`gasGate == 0`), ale zostają nieużywane rozgałęzienia.
