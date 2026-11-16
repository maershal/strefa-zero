# Ogień v2: dwustopniowe spalanie drewna w termice GPU

Stan z 2026-11-16 (#112, krok 4 z #117). Termika GPU pozostaje domyślnie wyłączona, więc wydana gra tej zmiany nie
widzi; model działa w laboratorium GPU i w trybach sprawdzających z włączoną termiką.

## Odpowiedź w skrócie

- Rozgrzane drewno nie pali się już w miejscu. Najpierw **pirolizuje**: bez udziału tlenu oddaje gaz palny do komórki
  gazu obok, a w komórce zostaje **węgiel drzewny**. Gaz spala istniejąca reakcja gazu palnego tam, gdzie jest tlen,
  więc płomień siedzi nad drewnem, płynie z gazem i gaśnie bez tlenu.
- Węgiel drzewny utlenia się powoli i gorąco, a na końcu zostaje popiół i przeszkoda znika, jak dotąd.
- Liczba przebiegów GPU na tick: **bez zmian** (wszystko w istniejących operacjach 17 i 18 `thermal.comp`).
- Bilanse masy i energii trzymają się w nowym sprawdzeniu z błędem względnym ≤ 3·10⁻⁷ (próg 10⁻³).
- Po drodze znaleziony i naprawiony **błąd bilansu energii w spalaniu gazu palnego**, obecny od I10 i dotąd bez
  testu: reakcja odejmowała od gazu entalpię zużytego tlenu, choć jego masa zostawała w komórce jako produkty.
  Przed poprawką nowe sprawdzenie traciło 0,8% (komora zamknięta) i 3,4% (otwarta) energii.

## Model

Dane są w rejestrze (`[[pyrolysis]]` w `src/sim/materials.toml`), a `scripts/gen_materials.py` generuje z nich makra
`WOOD_PYROLYSIS_*` w `assets/shaders/materials.glsl`. Model CPU z nich nie korzysta (decyzja z #111: bez odpowiednika
w wyroczni CPU, weryfikacja bilansami i scenami).

| Parametr | Wartość | Znaczenie |
| --- | --- | --- |
| `from` → `to` + `gas` | drewno → węgiel + gaz | produkt stały to istniejący materiał `coal`, gaz to `gas` |
| `temperature` | 500 K | początek pirolizy |
| `rate`, `span` | 0,065 1/s, 100 K | tempo osiągane 100 K powyżej progu; rośnie liniowo z temperaturą |
| `max_rate` | 0,5 1/s | górne ograniczenie tempa |
| `volatile_fraction` | 0,7 | część przetworzonej masy, która odchodzi jako gaz |

Zasady:

- **Stan.** Rekord drewna (`matter`: masa, entalpia, masa surowego drewna, materiał) jest w części surowym drewnem
  (`z`), w części węglem drzewnym (`masa − z`). Gdy całe drewno się przetworzy, rekord zmienia materiał na węgiel.
  Poniżej 2% masy rekordu reszta przetwarza się od razu, żeby zmiana nastąpiła w skończonym czasie.
- **Energia chemiczna.** Liczy się z obu części rekordu. Ciepło pirolizy to różnica między energią chemiczną drewna
  a energią jego produktów (600 kJ na jednostkę drewna przy obecnych stałych); generator odrzuca rejestr, w którym
  byłaby ujemna.
- **Temperatura.** Pojemność cieplna rekordu jest sumą obu części, a gaz zabiera tyle entalpii, żeby przemiana sama
  nie zmieniała temperatury drewna. Bez tego węgiel (c = 1,0) dziedziczyłby entalpię drewna (c = 1,7) i skakał
  o 70% w górę.
- **Spalanie.** Stałe jak dotąd w modelu GPU (decyzja z #111): 1 paliwo + 2 O₂, 10% dymu. Węgiel pali się istniejącą
  ścieżką `fuelRequest` ze stałymi materiału `coal` (zapłon 650 K, 0,03 1/s, 24 000 kJ, 10% popiołu).
- **Cząstki.** Gruz z drewnianej ściany przenosi rekord drewna i pirolizuje tak samo jak teren.

## Sprawdzenie: mielerz (`--gpu-check thermal-kiln`)

`src/gpu/kiln_validation.cpp`, CTest `gpu.thermal_kiln`. Blok drewna 12 × 2 komórki w kamiennej komorze 18 × 10 jest
grzany pędzlem 300 kJ/s przez 480 ticków, potem 1920 ticków bez wejścia. Dwa przebiegi: komora zamknięta i z otwartym
stropem. Warunki zaliczenia (progi zapisane przed pierwszym uruchomieniem):

| Warunek | Próg |
| --- | --- |
| bilans masy i energii, oba przebiegi | błąd względny < 10⁻³ |
| zamknięta: tlen w komorze na końcu | < 2% początkowego |
| zamknięta: ciepło spalania w ostatnim ticku | ≤ 10⁻⁶ szczytu |
| zamknięta: zostaje niespalony gaz palny; ogrzewane komórki są węglem; brak popiołu, ściana stoi | tak |
| otwarta: płomień pali się na końcu, w komorze jest więcej tlenu niż w zamkniętej | tak |

Wynik na GTX 1650 (sterownik 610.47), tick 2400:

| Przebieg | Tlen (z 0,78) | Gaz palny | Ciepło spalania / tick | Drewno / węgiel / popiół | Błąd masy | Błąd energii |
| --- | --- | --- | --- | --- | --- | --- |
| zamknięta | 0,00057 (0,07%) | 9,66 | 0 (szczyt 9,99) | 4 / 20 / 0 | 4,3·10⁻⁸ | 2,7·10⁻⁷ |
| otwarta | 0,43 | 0,001 | 18,0 | 2 / 22 / 0 | 2,5·10⁻⁷ | 1,9·10⁻⁷ |

W komorze zamkniętej tlen kończy się około ticku 720; płomień dogasa, a węgiel stygnie bez utleniania. W otwartej
gaz spala się na bieżąco, a po przemianie węgiel żarzy się i sam się nagrzewa.

Jedna zmiana po pierwszym uruchomieniu: w pierwotnej komorze 37 × 27 tlen spadał do 1,2%, ale resztka dyfundowała
z narożników i warunek zgaśnięcia płomienia nie był spełniony w 2400 tickach. Zmniejszona została komora, progi
zostały te same.

## Ograniczenia

- **Tylko powierzchnia.** Drewno bez wolnej komórki obok nie pirolizuje, bo gaz nie ma dokąd ujść. Tak samo
  działało dotąd spalanie (wnętrze bez tlenu się nie paliło). Środek grubej belki zwęgla się dopiero po wypaleniu
  warstwy zewnętrznej.
- **Węgiel drzewny to materiał `coal`.** Osobny materiał pojawiłby się w palecie laboratorium CPU bez zachowania
  w modelu CPU (ADR 0001). Decyzja do zmiany przy #114 (węgiel jako materiał cząstek GPU).
- **Mniej popiołu.** Z drewna zostaje 3% masy jako popiół (30% węgla × 10%), wcześniej 12%.
- **Tempo końca.** Wypalanie węgla jest wykładnicze jak dotychczasowe spalanie drewna i oleju, więc zniknięcie
  przeszkody trwa minuty. Sprawdzenie mielerza nie obejmuje przejścia węgiel → popiół.
- **Prezentacja.** Dodany jest tylko kolor zwęglania i węgla w nakładce termiki. Rampa barw płomienia, iskry
  i żar to #115.
- **Koszt.** Bez pomiaru wydajności: liczba przebiegów się nie zmienia, a dodane instrukcje działają w dwóch
  istniejących kernelach reakcji. Pomiar według protokołu należy do pracy nad budżetem termiki (#96).
- **Skróty bitowe.** Scena `fire` w `--gpu-check thermal-bits` ma po tej zmianie inne skróty niż `main` (zmiana
  zachowania); nadal powtarza się bit w bit w jednym procesie.
