# Zegar i tempo dema

## Problem

Oba widoki GPU używały `FixedStepClock<120, 4, INT_MAX>`: dług czasu bez limitu, a `dropped_seconds()` zawsze równe 0. Delta klatki trafiała do zegara bez przycięcia. Po przestoju (np. pierwsza kompilacja świata GPU w `perform()`, przeniesienie okna) symulacja biegła do 2× szybciej, aż spłaciła dług. W przeciążeniu trwało slow-motion: w I11 zaległość wynosiła 31,87 s. Przy VSync 60 Hz jitter pomiaru klatki dawał 1, 2 lub 3 ticki na klatkę, a p95 klatki wynosiło 16,73–17,30 ms (I08).

## Decyzja

Decyzję I06 („zachowaj dług”) zmieniamy jawnie, tylko w trybie interaktywnym (`sim::interactive_clock` w `src/sim/fixed_clock.hpp`):

| Reguła | Wartość | Powód |
| --- | --- | --- |
| Clamp delty klatki | 0,1 s | przestój nie wchodzi do symulacji |
| Dług po przyciętej klatce | 0 | spłata przestoju byłaby właśnie przyspieszeniem |
| Limit długu z przeciążenia | 0,25 s | po przeciążeniu najwyżej krótka spłata |
| Kroki w klatce | nominalne + 1, maks. 4 | 30/60/144 FPS w czasie rzeczywistym; pomiar niżej |
| VSync | delta przyciągana do okresu odświeżania (±12,5 %), reszta przenoszona | stałe 2 ticki przy 60 Hz, bez dryfu |

Odrzucony czas trafia do `dropped_seconds()` i jest widoczny w HUD. Odtwarzanie nagrań i tryby walidacji zostają przy `sim::exact_clock` (pełny dług). Wynik kroku zależy od jego numeru, nie od czasu ściany, więc polityka zmienia tylko tempo.

## Pomiar limitu kroków

Misja (`--mission-smoke-test`, 1700 ticków), GTX 1650 przez WSLg/D3D12, 2 listopada 2026. Warianty uruchomiono tymczasową łatką, której nie ma w kodzie. Surowe dane: [demo-clock](https://github.com/maershal/strefa-zero-measurements/tree/3330547b0c23e8f43a717329b9ce5f0f01261293/demo-clock). Tempo = ticki × 1/120 s / czas podany zegarowi.

| Limit kroków w klatce | Klatki | Mediana | p95 | GPU/krok | Tempo |
| --- | --- | --- | --- | --- | --- |
| stały 2 | 851 | 41,4 ms | 52,7 ms | 6,81 ms | ×0,34 |
| stały 3 | 568 | 57,9 ms | 60,7 ms | 7,25 ms | ×0,38 |
| stały 4 | 426 | 76,9 ms | 80,9 ms | 6,87 ms | ×0,39 |
| nominalne + 1, maks. 5 | 341 | 96,4 ms | 125,6 ms | 6,84 ms | ×0,37 |

Gdy ogranicza GPU, każdy dodatkowy tick wydłuża klatkę. Tempo prawie się nie zmienia, rośnie tylko opóźnienie. Maks. 5 dodaje 45 ms do p95 bez zysku tempa, dlatego limit to 4. Stały limit 2 lub 3 dawałby trwałe slow-motion przy klatce ograniczonej czymś innym niż symulacja (limit 30 FPS: ×0,5 lub ×0,75). Limit względny zachowuje czas rzeczywisty przy 30, 60 i 144 FPS (test `game.clock`).

## Testy

- `game.clock`: przestój 5 s → utracone 4,75–5 s, brak długu, kolejne 2 s przy 60 FPS w czasie rzeczywistym (≤ 1,1×). Polityka `exact_clock` zachowuje dług. Przeciążenie: dług ≤ 0,25 s, a podany czas = wykonane + utracone + oczekujące. VSync 60/59,94/120/144 Hz z jitterem ±1 ms: stała liczba ticków i dryf ≤ 12,5 % okresu po 10 min. Testy obejmują też monitor spowolnienia GPU i miernik tempa.
- `gpu.mission_ui_quick`: wstrzyknięty przestój 5 s w misji. Wynik lokalny: utracone 5,36 s, tempo po powrocie ×0,67 (WSLg, GPU-bound), test zaliczony.
- `gpu.mission_ui`: koniec po 1700 tickach niezależnie od FPS, limit bezpieczeństwa 55 s z zapisem `mission-progress.txt`, `gpu-startup.txt` od razu po otwarciu świata. Kryterium czasu: zawsze księgowanie (podany czas = wykonane + utracone + oczekujące, ± 1 tick), a na sprzętowym GPU dodatkowo ≤ 0,5 s utraconych po pierwszej sekundzie. Lokalnie (WSLg, ~13 FPS) księgowanie się zgadza (błąd 1e-13 s), ale utracono 19,6 s, więc test nie przechodzi. To prawdziwy wynik: ten GPU nie utrzymuje misji w czasie rzeczywistym. Przed zmianą ten sam przebieg przekraczał TIMEOUT 60 s (900 klatek × ~78 ms).

## HUD

Misja i Laboratorium GPU pokazują „Tempo ×1.00 | utracony czas 0.00 s | GPU/krok 1.23 ms”. Ostrzeżenie „GPU zwolnił – zasilanie lub temperatura?” pojawia się, gdy mediana czasu GPU na krok z ostatnich 2 s przekracza 2× medianę z pierwszych 10 s sceny.

## Do zmierzenia na Windows

p95 klatki z VSync przed (`main`) i po. Cel: ≤ 16,7 ms. Wynik jest w `mission-ui.txt` (`p95_ms`):

```powershell
.\StrefaQA.exe --mission-smoke-test --test-vsync on --profile pomiar-vsync-przed
.\StrefaQA.exe --mission-smoke-test --test-vsync on --profile pomiar-vsync-po
```
