# Model fizyczny 0.4.2

Zmiany warunków stabilności, MIC(0), zgodności szczelnego Poissona, granicy prędkości cieczy i wielowątkowości opisuje [audyt matematyczny i wydajnościowy](reports/PERFORMANCE_AUDIT.md). Aktualne domyślne CFL: 0,45 dla akustyki/adwekcji gazów i 0,9 dla sumy adwekcji i lepkości cieczy. Liczba podkroków cieczy jest ustalana ponownie po każdej projekcji.

## Stan, jednostki i bilans

To model do gry na 320 × 192 komórkach, z deterministycznym podziałem wybranych etapów na 1–4 wątki; jedna jednostka długości odpowiada pikselowi świata. Czas zewnętrzny wynosi 1/60 s. Gęstość jest znormalizowana tak, że pełna komórka wody ma masę 1. Temperatury są w kelwinach; pojemności cieplne i ciepła utajone zachowują proporcje fizyczne, ale przewodności i tempo procesów są dobrane do skali gry. Ciśnienia nie należy odczytywać jako paskali.

`PhysicalProperties` w `material.hpp` zawiera gęstość, cp, przewodność, temperaturę startową/zapłonu, tempo spalania, energię reakcji, pozostałość i wytrzymałość. Osobne tablice SoA przechowują tożsamość, masę, entalpię, paliwo, zdolność kwasu do reakcji, produkty rozpuszczone oraz prędkości i przesunięcia fragmentów. Ciecze mają sześć niezależnych kanałów objętości i entalpii. Powietrze ma sześć kanałów masy: gaz obojętny, tlen, para, dym, paliwo i produkty spalania.

Budżet rozróżnia stan początkowy, edycje pędzlem, źródła masy/energii, dostarczone ciepło, ciepło chemiczne, dopływ/ujście górnej granicy i promieniowanie. `mass_error` i `energy_error` porównują obecną sumę z tym budżetem. Wymazanie pędzlem jest jawną edycją. Nadmiar cieczy i małe udziały nie są po cichu usuwane. Budżet energii dotyczy entalpii; tłumienie prędkości, akustyki i zderzeń nie wraca jako ciepło. Brak pełnego bilansu energii kinetycznej/potencjalnej jest świadomym ograniczeniem modelu.

## Ciepło i fazy

Dla każdej substancji h(T)=cp*T+b. Stałe b zapewniają zgodne odniesienie entalpii w parach lód/woda/para, metal/stopiony metal i kamień/lawa. Temperatura mieszaniny wynika z sum H, cp*m i przesunięć fazowych.

Przewodzenie przez ścianę komórek ma postać Q=G*(Tj-Ti)*dt, G=1/(0.5/ki+0.5/kj+Rkontakt). Q jest ograniczone przez pojemności obu komórek. Wszystkie pary korzystają z jednego obrazu temperatur; jedna otrzymuje dokładnie tyle energii, ile traci druga. Oddzielne pary wymieniają energię z gazem. Transport przenosi entalpię razem z masą.

Rozwiązanie entalpii pary faz jest analityczne: poniżej m*hsolid(Tm) pozostaje ciało stałe, powyżej m*hliquid(Tm) ciecz, a między tymi wartościami udział ciekły wynosi (H-m*hsolid)/L. Obie fazy są wtedy na temperaturze przejścia. Lód topnieje przy 273,15 K (L=334), metal przy 1811 K (L=270), kamień/lawa przy 1400 K (L=400). Ciepło parowania wody wynosi 2256 przy 373,15 K. To progi modelu; nie identyfikacja wszystkich realnych stopów i skał. Rozpuszczone substancje nie mają jeszcze krzywej obniżenia temperatury krzepnięcia.

Parowanie odbiera cieczy entalpię powstałej pary. Skraplanie wynika z niedoboru energii gazu względem progu i uwalnia ciepło utajone. Para nie ma TTL. Wygląd żaru zależy od temperatury; renderer nie wykonuje przemian.

## Ciecze

Pola MAC przechowują u na pionowych, v na poziomych ścianach komórek. Prędkość przewidywana zawiera grawitację, upwind adwekcję i jawny laplasjan lepkości (woda 0,06, olej 1,2, kwas 0,1, lawa 3, nitrogliceryna 0,18, metal 0,4 w jednostkach siatki²/s). Podkroki wynikają z CFL; zakres prędkości do gry jest ograniczony.

Projekcja rozwiązuje div((1/rho)*grad(p))=div(u*)/dt. Pięciopunktowy operator pozostaje bez jawnej macierzy. PCG korzysta z poprzedniego ciśnienia i incomplete Cholesky o tej samej strukturze sąsiedztwa. Każda szczelna składowa ma jedną kotwicę ciśnienia. Na powierzchni swobodnej jest lokalne ciśnienie gazu; normalne prędkości ścian wynoszą zero. Domyślna tolerancja względna: 1e-4, limit 200 iteracji na podkrok. Raport zawiera residuum, sumę iteracji, zbieżność, dywergencję i nadmiar objętości.

Skończone objętości transportują oddzielne udziały cieczy, ich energię, pojemność reakcyjną kwasu i solut. Odpływ jest ograniczony masą dawcy. Nadmiar objętości działa jako źródło korekty projekcji; nie jest kasowany. Dodatkowa lokalna wymiana równych objętości cięższej cieczy nad lżejszą pobudza nierozdzieloną wyporność idealnie płaskiej granicy. Zachowuje masę każdego składnika i entalpię, ale jest przybliżeniem do gry.

Transport stosuje ograniczoną rekonstrukcję liniową minmod w obu kierunkach, z zachowawczym strumieniem i limitem odpływu dawcy. Przy zerowym nachyleniu jest to donor-cell. Usunięto pionowe założenie o dolnym przedziale cieczy, które bez rekonstrukcji granicy powodowało zależność toru od liczby podkroków. Prędkości MAC są przedłużane o dwie ściany poza powierzchnię, bez przenoszenia masy/energii i bez przechodzenia przez osłony. Nie ma pełnej rekonstrukcji VOF/PLIC. Powierzchnie mogą się rozmywać, a bardzo małe udziały są nadal przechowywane mimo braku widocznego piksela. Nie ma napięcia powierzchniowego ani mechaniki pęcherzyków.

## Gazy i fala

Powietrze działa na pełnej siatce, z osobnym MAC, p', temperaturą i składem. Model liniowej akustyki: dp/dt=-rho0*c²*div(u)+Sp, du/dt=-grad(p)/rho0+f. c=40 komórek/s jest celowo spowolnioną prędkością gry. Jawne podkroki akustyczne zależą od CFL; transport składu ma niezależne podkroki od prędkości przepływu.

Wyporność zależy od temperatury i składu gazu. Ruch nie ma reguły „zawsze w górę”. Transport i dyfuzja zachowują masy wszystkich gatunków i entalpię. Podmuch tłumi się, a prędkości mają ograniczenie ±40; nie jest to solver fali detonacyjnej ani pełne równania gazu doskonałego. Duże źródło ciśnienia jest dostarczane porcjami; pozostała całka zostaje w kolejce.

Góra domyślnie otwarta, z ciśnieniem otoczenia i warstwą tłumiącą; jawnie księgujemy opuszczającą i napływającą masę oraz energię. Ściany i niemal pełna ciecz blokują przepływ. Gaz uwięziony przez edycję lub ruch materiału zachowuje masę, ale nie ma jeszcze rozwiązywania objętości zamkniętych bąbli.

## Reakcje i fragmenty

Spalanie zużywa paliwo oraz tlen (umowna stechiometria 0,7 jednostki tlenu na jednostkę paliwa), przenosi entalpię substratów do produktów, dodaje zadeklarowane ciepło reakcji i emituje dym. Drewno pozostaje konstrukcją podczas spalania, następnie zostawia popiół. Model nie opisuje pełnej chemii pierwiastkowej. Termit ma wewnętrzny utleniacz i pozostawia metal; topienie sąsiada wymaga entalpii. Jawne promieniowanie usuwa nadwyżkę energii produktów ponad zakres wizualny, ze śladem w budżecie.

Kwas ma skończoną pojemność reakcji. Rozpuszczona masa i jej energia przechodzą do solutu, który płynie z nośnikiem. Wyczerpany kwas przechodzi w nośnik wodny; nie ma cichego kasowania podłoża.

Reakcje ciągłe mają tempo przemnożone przez dt. Obecnie nie ma losowych reakcji kinetycznych; przy ich dodawaniu należy używać prawdopodobieństwa 1-exp(-k*dt), zamiast prawdopodobieństwa na klatkę. Losowy wybór strony usypywania jest deterministycznym hashem pozycji i kroku, a nie kinetyką chemiczną.

Eksplozja ma identyfikator, ciepło, gaz i impuls. Jednokrokowe pole udaru przekazuje obciążenie również fazom skondensowanym; proch i nitrogliceryna mogą od niego rozpocząć reakcję łańcuchową. To pole jest oddzielone od hydrostatyki i od ciśnienia powietrza, które nie istnieje wewnątrz pełnej komórki cieczy. Osłona tłumi udar przed oceną zapłonu. Widoczność/pochłanianie osłon obliczamy przed zniszczeniem komórek, by ta sama eksplozja nie otwierała sobie drogi w kolejności skanowania. Naprężenie powyżej wytrzymałości odrywa materiał jako piksele zachowujące tożsamość, masę i ciepło. Metal ma skończoną wytrzymałość. Jest to model osłony dla gry, bez dyfrakcji i odkształcania blach.

Proszki i oderwane piksele integrują grawitację, opór gazu i przesunięcia podkomórkowe. Każde przekroczenie komórki jest sprawdzane; kolizje mają tarcie i tłumione odbicie. Ciecze są wypierane z zachowaniem masy. Nie ma połączonych sztywnych brył.

## Referencja i pomiary

Jedna deterministyczna implementacja jednowątkowa, dwa tryby skanowania ruchu: aktywne chunki i cała siatka. Usypianie ruchu nie wyłącza przewodzenia ani reakcji. Spokojne powietrze i termika mogą pomijać pracę niezależnie od ruchu. Bufory pól i PCG są trwałe. Progi numeryczne usypiania wynoszą 1e-4 K od temperatury otoczenia i 1e-9 dla śladowych gatunków gazu; przemiany par o masie poniżej 1e-8 są odroczone. Masa i entalpia tych śladów pozostają w tablicach oraz bilansie. Release używa LTO tam, gdzie wspiera je kompilator.

`StrefaPhysics` generuje sceny, CSV i BMP co trzy kroki. Czasy są mierzone wyłącznie wokół kroku świata/logiki gry. Eksport i rendering są poza pomiarem. Raporty podają percentyle i maksimum; deklarację 60 FPS należy oceniać na bieżącym sprzęcie, szczególnie podczas rozległych reakcji i po włączeniu eksportu.


