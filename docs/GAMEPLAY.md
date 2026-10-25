# Poligon i animacja 0.4

Zasady poligonu pozostają: trzy tarcze, karabin 30 nabojów/10 Hz, przeładowanie 1,5 s, trzy granaty z zapalnikiem 2 s i plecak z odnawialnym paliwem. Gracz jest niewrażliwy na obrażenia. Położenie, wyposażenie i cele resetuje F5. Świat jest osobną instancją tego samego modelu używanego w laboratorium.

Kolizja gracza to przesuwany AABB 8 × 18 z podziałem drogi co najwyżej 0,5 komórki. Małe stopnie są pokonywane do dwóch komórek. Osadzenie przez osuwający się materiał uruchamia ograniczone szukanie wolnego miejsca; w ostateczności reset próby jest liczony w diagnostyce. Pociski używają supercover DDA, dlatego nie przeskakują cienkich przeszkód. Przy uszkodzeniu fragment zachowuje tożsamość i ciepło. Trafienie termitu/prochu/nitrogliceryny dostarcza temperaturę zapłonu.

Granat wysyła jedno zdarzenie eksplozji do modelu. Warstwa gry odczytuje zdarzenia, generuje efekt wizualny i ograniczony impuls gracza. Ochrona terenu jest sprawdzana zarówno przy impulsie gracza, jak i zniszczeniu tarczy. Nie ma punktów zdrowia ani ragdolli. Ruch gazu dokłada niewielką siłę aerodynamiczną. Wybuchy materiałów także są widoczne przez zdarzenia świata.

## Animacja

Renderer składa istniejący atlas `parts.bmp`: nogi, miednicę, tułów, kamizelkę, głowę, hełm, plecak i ręce. Atlas `weapons.bmp` dostarcza karabin. PNG przetranskodowano do BMP z zachowaniem pikseli i alfa; źródła Krita pozostają w pakiecie artystycznym. Prostokąty wycięcia części są jawne w rendererze.

`game::pose(PlayerState)` zwraca biodro, bark, głowę, chwyty, lufę i kończyny. Analityczne dwuczłonowe IK ogranicza odległość celu do osiągalnej, przez co nie produkuje NaN przy zbyt dalekim chwycie. Górna i dolna część ramienia podążają za chwytem broni; kierunek celowania jest niezależny od nóg. Lufa efektu strzału pochodzi z tej samej pozy. Pocisk zaczyna sprawdzanie przy barku, aby lufa wystająca przez ścianę nie pozwalała strzelać przez przeszkodę.

Faza chodu rośnie od rzeczywiście przebytej odległości, nie licznika kroków. Prędkość steruje płynnym nasileniem chodu. Skok, lot i spadanie zmieniają położenie nóg; pionowa prędkość sprzed kolizji daje amortyzację lądowania. Odrzut pojawia się wyłącznie po faktycznym zużyciu naboju i zanika wykładniczo. Grzbiet/tułów reaguje na ruch i odrzut. To proceduralny rig pikselowych części, bez animacji ragdoll ani szkód zależnych od kończyny.

## Weryfikacja

Testy obejmują długości kości IK, brak chodu/odrzutu w bezruchu, wzrost fazy po przemieszczeniu i odrzut rzeczywistego strzału. Przebieg rozgrywki z identycznymi wejściami według numeru kroku ma taki sam checksum przy 30, 60 i 144 FPS. Test ukończenia poligonu wymaga zniszczenia wszystkich trzech tarcz, a nie tylko sprawdzenia liczników broni.
