# Animacja i IK

Poligon CPU (osobna gra na modelu CPU: gracz, pociski, granaty i tarcze w `game::GameSession`) został usunięty; jego
rolę przejęła Misja GPU. Reguły gry są w `game::GameplaySession`, a fizyka postaci, pocisków i trafień na GPU
([I06](reports/I06_GAMEPLAY_PROTOCOL.md)). Ostatni stan poligonu jest w historii Git przed usunięciem.

## Animacja

GPU składa istniejący atlas `parts.bmp`: nogi, miednicę, tułów, kamizelkę, głowę, hełm, plecak i ręce. Atlas `weapons.bmp` dostarcza karabin. PNG przetranskodowano do BMP z zachowaniem pikseli i alfa; źródła Krita pozostają w pakiecie artystycznym.

Równania pozy są w `assets/shaders/soldier_pose.glsl`; `game::pose(PlayerState)` w `src/game/animation.hpp` kompiluje ten sam plik jako C++. Poza zwraca biodro, bark, głowę, chwyty, lufę i kończyny. Analityczne dwuczłonowe IK ogranicza odległość celu do osiągalnej, przez co nie produkuje NaN przy zbyt dalekim chwycie. Górna i dolna część ramienia podążają za chwytem broni; kierunek celowania jest niezależny od nóg.

Faza chodu rośnie od rzeczywiście przebytej odległości, nie licznika kroków. Prędkość steruje płynnym nasileniem chodu. Skok, lot i spadanie zmieniają położenie nóg; pionowa prędkość sprzed kolizji daje amortyzację lądowania. Odrzut zanika wykładniczo. To proceduralny rig pikselowych części, bez animacji ragdoll ani szkód zależnych od kończyny.

## Weryfikacja

`game.input_pose` sprawdza długości kości IK i skończoność pozy w ruchu (wspólny plik GLSL skompilowany jako C++) oraz bufor wejścia: jednorazowe akcje przeżywają klatki bez kroku i są zużywane raz, utrata fokusu czyści sterowanie.
