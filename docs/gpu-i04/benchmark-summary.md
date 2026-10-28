## Pomiary kontrolowane

GTX 1650, NVIDIA 610.47, GL 4.3, Release MSVC `/W4 /WX`. Pięć scen, trzy warianty, trzy powtórzenia. Świeży świat, 120 ticków rozgrzewki, 240 pomiarowych; odwrócona kolejność wariantów w drugim powtórzeniu. Grupy timerów włączone we wszystkich wariantach. Odczyty diagnostyki przez kopię staging poza czasem kroku. `glFinish` i odbiór HUD co 16 ticków są granicą harnessu.

| Scena | Wariant | Mediany GPU [ms], 3 powtórzenia | p95 GPU [ms], 3 powtórzenia |
| --- | --- | --- | --- |
| mission | i03_reference | 3.052 / 3.224 / 3.133 | 3.769 / 4.787 / 3.328 |
| mission | local | 3.090 / 3.150 / 3.103 | 4.626 / 3.792 / 3.295 |
| mission | combined | 3.090 / 3.093 / 2.968 | 4.665 / 4.643 / 3.238 |
| stress_16k | i03_reference | 2.838 / 2.949 / 2.863 | 3.412 / 3.612 / 3.035 |
| stress_16k | local | 1.988 / 2.086 / 2.046 | 2.439 / 2.655 / 2.204 |
| stress_16k | combined | 2.191 / 2.031 / 1.991 | 3.387 / 2.692 / 2.225 |
| stress_32k | i03_reference | 5.675 / 5.825 / 5.644 | 6.584 / 6.655 / 5.894 |
| stress_32k | local | 3.889 / 3.836 / 3.758 | 5.611 / 4.907 / 4.094 |
| stress_32k | combined | 4.194 / 3.788 / 3.722 | 5.783 / 4.825 / 4.082 |
| mission_route | i03_reference | 3.174 / 3.200 / 3.180 | 3.864 / 3.980 / 3.252 |
| mission_route | local | 3.160 / 3.236 / 3.168 | 3.864 / 4.067 / 3.295 |
| mission_route | combined | 3.010 / 3.098 / 3.126 | 4.145 / 3.769 / 3.215 |
| coupled_tank | i03_reference | 4.504 / 4.001 / 3.736 | 6.045 / 4.530 / 4.184 |
| coupled_tank | local | 3.981 / 4.278 / 4.217 | 6.103 / 4.940 / 4.348 |
| coupled_tank | combined | 3.796 / 3.844 / 3.836 | 4.606 / 4.739 / 4.020 |

P95 CPU submit domyślnego wariantu combined:
- mission: 0.029 / 0.027 / 0.078 ms; p99 0.121 / 0.181 / 0.147 ms.
- stress_16k: 0.049 / 0.038 / 0.024 ms; p99 0.183 / 0.216 / 0.146 ms.
- stress_32k: 0.078 / 0.031 / 0.019 ms; p99 0.216 / 0.127 / 0.096 ms.
- coupled_tank: 0.051 / 0.080 / 0.072 ms; p99 0.202 / 0.131 / 0.116 ms.

Przykładowy zwykły tick ma 130 dispatchy API w reference, 172 w local i 140 w combined. To odpowiednio 42, 42 i 26 logicznych redukcji; nowe warianty mają dwa zlecenia indirect na redukcję, z zerami dla pustej lub zbędnej pracy. Nie przedstawiamy kompakcji jako zmniejszenia liczby wywołań API. GPU zlicza rzeczywiście przetworzone reakcje, grupy oraz partie, osobno od żądań harmonogramu.

Koszt 1,5 ms GPU/tick nie został osiągnięty w bieżących warunkach pomiaru. Już niezmieniony I03 osiągał w pomiarze wstępnym około 13–16 ms. Surowe dane zawierają także szerokie ogony opóźnień. Nie przypisujemy ich automatycznie jednemu odczytowi ani nie uznajemy problemu z I00–I03 za naprawiony.

Pozostały koszt obejmuje globalne przebiegi PBF/PBD oraz sposób wykonywania zleceń sterownika. Następny eksperyment wydajności: ta sama trasa przy widocznym i ukrytym oknie, wraz z wyłączonymi timerami grup, oraz osobny pomiar producentów PBF i kontaktu. Umożliwi to oddzielenie kosztu solvera od reżimu okresowych spowolnień. I05 pozostaje etapem wspólnego silnika i zasobów.
