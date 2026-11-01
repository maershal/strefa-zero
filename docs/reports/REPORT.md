# Strefa Zero 0.4 — raport wydania

Zaimplementowano termikę z entalpią i ciepłem utajonym, przepływ MAC z PCG/IC(0), powietrze i fale ciśnienia, spalanie z tlenem, kwas z produktami, udar i reakcje łańcuchowe, fragmenty z bezwładnością oraz animację postaci z IK.

Testy Release: **4/4 zaliczone**. Osobno zbudowano model bez SDL/GPU w Debug i wykonano próbę trzech kroków. Przeniesiona paczka Windows przeszła test OpenGL z kodem 0. W oknie sprawdzono poligon, mapę temperatury, pauzę i zapis klatek.

**Cel stałych 60 FPS nie został osiągnięty.** Na tym komputerze poligon miał medianę  14,8201  ms, p95  45,746  ms i maksimum  59,202  ms na krok CPU. Rozległe ciecze i reakcje bywają znacznie wolniejsze. To wydanie funkcjonalnej wersji odniesienia, z jawnym ograniczeniem wydajności.

900 obrazów BMP obejmuje 14 scen i poligon, po 180 kroków (3 s), obraz co 3 kroki. Maksymalny błąd masy:  2,41416E-08 ; entalpii:  5,09014E-06 . Wszystkie kroki PCG w nagraniach zbiegły się przy tolerancji 1e-4. Bilanse uwzględniają źródła, edycje, promieniowanie i górną granicę.

| Scena | Mediana ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|
| falling-water | 30.8728 | 122.011 | 136.502 | 141.682 |
| dam-break | 96.325 | 130.642 | 203.47 | 335.672 |
| connected-vessels | 66.5194 | 75.2735 | 81.6131 | 86.0942 |
| oil-water | 20.5059 | 34.2087 | 44.5603 | 48.429 |
| sand-pile | 8.7171 | 12.2558 | 13.5236 | 14.2389 |
| sand-water | 106.299 | 147.442 | 247.597 | 308.449 |
| smoke-ceiling | 36.5058 | 45.4221 | 47.8258 | 50.3277 |
| smoke-vent | 39.0279 | 45.5336 | 56.0644 | 61.0397 |
| wood-fire | 50.8748 | 65.0229 | 71.7209 | 78.3249 |
| extinguish | 81.2181 | 113.191 | 119.473 | 127.415 |
| melting-metal | 50.199 | 59.6789 | 63.3518 | 68.1787 |
| quench-metal | 73.6037 | 102.037 | 182.469 | 183.123 |
| ice-fire | 38.0155 | 48.8573 | 52.9224 | 53.7979 |
| shielded-explosion | 42.0904 | 57.908 | 69.5181 | 75.61 |
| arena | 14.8201 | 45.746 | 52.2369 | 59.202 |

Pomiary obejmują samą fizykę (dla poligonu także logikę gry), bez renderowania i eksportu. CPU: i5-9300HF; GPU: GTX 1650. Aktywność systemu i taktowanie nie były kontrolowane. Krótkie nagrania pokazują przebieg przejściowy; równowagę cieplną i hydrostatykę dodatkowo sprawdzają testy matematyczne.

Galeria jest w archiwum `StrefaZero-recordings-v0.4.zip`, pod `physics-v0.4/index.html`. `summary.csv` i `steps.csv` zawierają surowe wyniki. Model oraz ograniczenia: `PARTICLES.md` w źródłach. To model do gry: energia mechaniczna jest tłumiona, ciśnienie ma skalę gry, a transport granic cieczy jest uproszczony. Nie ma ragdolli, botów ani multiplayera. 