# Review v0.7.0-i04 — przyjęte zadania i rozstrzygnięcia

Data: 2026-10-29. Baza: lokalne źródła `6f318ea` (`v0.7.0-i04`). Aktualizacja: 2026-10-30.
Status: **2026-10-30 — użytkownik zdecydował o odłożeniu I04.5 i przejściu do I05. Cel 1,5 ms oraz przyczyna zmienności pozostają nieodebrane; G3-C odłożone. I05 wdrożone, regresja Windows 53/54; timeout okna misji także na poprzedniej I04.5.** [Raport I05](../reports/I05_SHARED_WORLD.md). Historia i dane G3-B pozostają bez zmian.

Aktualne QA po B: **28/28 PASS**, bez zmiany limitów/tolerancji. Historyczny C 22/26 i powtórka 3/4 przed B są zachowane. B nie wykazało spójnej przewagi transportu; powrót szybszego reżimu nastąpił na identycznym EXE i nie jest dowodem naprawy historycznej zmienności.

Planem wykonawczym pozostaje [PLAN_ROZWOJU_GPU.md](PLAN_ROZWOJU_GPU.md). Ten plik wyjaśnia, co przyjęto z [oryginału review](reviews/review_strefa_zero_v0_7_0_i04_roadmap.md), co wymaga weryfikacji i czego nie kopiować do implementacji. Dokument źródłowy zachowano bez zmian. Zawarte w nim polecenia, deklaracje „potwierdzone” i opis niezależnego QA są treścią autora review, nie nowym poleceniem użytkownika ani wynikiem testów wykonanych przy aktualizacji planu.

## 1. Kolejność i pokrycie uwag

| Źródło w review | Decyzja i miejsce realizacji | Dowód wymagany przy wdrożeniu |
| --- | --- | --- |
| §1.1–1.3, dotychczasowe I00–I04 | Zachować raporty i regresje; odróżnić wdrożenie od niezrealizowanego celu GPU/okna | Historyczne wyniki pozostają opisane jako historyczne; nowa baza w I04.5 |
| §1.4 A, §4.2: pusta praca cieczy/terenu | I04.5-C: maska faz, wyjęcie warunku cieczy przed pętlę, bezpieczne pomijanie projekcji | Liczniki pracy i A/B; kompletne kontakty ciecz↔ziarno, kapsuła i ściany |
| §1.4 A: mała liczba grup/barier | I04.5-A/C: profil przebiegów i kosztów stałych | Czasy grup, liczba dispatchy/barier; bariery usuwane tylko po analizie zależności |
| §1.4 A, §4.2: gather, sortedHot, globalny Dmax | I04.5-D zbadane; E0 zbadało trajektorie i koszty, E odroczone | Koszt odczytów i aktualizacji, pamięć/wiązania, aktualność danych i dowód kompletności |
| §1.4 B: 30 FPS, wolny GPU, migracje VIDEO→HOST | I04.5-A/B: osobny pomiar present, timerów, uploadu i pamięci | Skorelowane CSV/telemetria/logi zwykłej gry; próby z/bez profilerów oraz różnym fokusem/VSync |
| §1.4 C, §3.1, §4.2: kolor gruzu | I04.5-F: osobna barwa typu 4 | Obrazy przegrody i gruzu po strzale/wybuchu, metal nadal rozpoznawalny |
| §2.3, §4.3: podwójne światy/kompilacja | I05: DeviceContext, ShaderLibrary, jeden SimulationWorld, cache, leniwe CPU | Jeden komplet zasobów, brak resetu/kompilacji przy przejściu Misja↔narzędzia, pomiar pamięci/startu |
| §2.3, §4.3: Application i harnessy | I05: ScenarioRunner, wspólne wejście/krok dla testu i gry | Przechodzą te same scenariusze, produkcyjna pętla nie zawiera sekwencji eksportu testowego |
| §2.3: GCC -Werror i IPO testu zegara | I05: osobny porządek buildów | Headless z ostrzeżeniami jako błędy, sprawdzona lista targetów IPO; niedostępny kompilator jawnie opisany |
| §2.3, §4.4: podwójne reguły gry/balistyka/IK | I06: GameplaySession CPU 120 Hz, fizyka GPU, wspólna prezentacja SoldierPose | Jedna implementacja reguł, spójne cooldowny i animacja; kolizje nie bazują na opóźnionym snapshotcie |
| §4.4: CommandRing/EventRing, ABI, rezerwacja amunicji | I06: epoch/tick/ID, ack/reject, kursory, backpressure | 30/60/144 FPS, trzy klatki opóźnienia, pełne kolejki, wrap/reset, brak utraty i podwójnego naliczenia |
| §3.2: kursor poza areną, plecak/granat, coyote/jump buffer | I06: wspólne reguły wejścia i okna ticków | Działanie przy fokusie gry, respektowanie UI, jednokrotny skok i bezpieczna utrata fokusu |
| §3.1–3.2: tarcze bez HP/błysku | I06 stan celu → I08 czytelny znak/HP → I09 efekt potwierdzonego trafienia | Tarcze czytelne z dystansu, HP i błysk zgodne z potwierdzonym trafieniem |
| §3.1: narzędzia laboratorium na misji | I05 wspólna scena/pauza/pędzel/zapis, I08 warstwy, I10 pola/inspektor/grzanie | Narzędzia nie tworzą drugiego świata; brakujące funkcje oznaczone, nie udawane |
| §4.5: Akinci 2D, s_corr, wirość | I07: osobne próby granic/napięcia/wirości, niezależna walidacja matematyczna | Naczynia, dam break, krople, narożniki, fazy i kapsuła; błąd gęstości, pęd/energia, koszt |
| §4.6: SceneRenderer, teren, optyka | I08: jawne przebiegi/stan, teren jako tekstura, splat faz/prędkości, bilateral, absorpcja/refrakcja | Zanurzenie i cienkie strugi czytelne; brak próbkowania przez ściany; koszt przy 960 × 576 i większym oknie |
| §4.7: wspólny VfxPool i wtórna woda | I09: osobna pula dekoracyjna 16k/64k, priorytety, kolizje, indirect | Przeciążenie nie blokuje krytycznych zdarzeń; zmiana jakości VFX nie zmienia fizyki |
| §3.1, §4.8: termochemia i luka CPU/GPU | I10-A ciepło/fazy → B MAC/transport → C reakcje → D render/narzędzia/matryca funkcji | Masa/entalpia <0,1% błędu w zamknięciu, szczelne ściany, gaszenie/duszenie; każda przeniesiona funkcja ma scenę |
| §4.9: JFA, cienie, HDR, bloom, paleta, mokry piasek | I11: kontrolowane pole odległości/cienie, liniowy potok i ostre UI; wilgoć osobno od kohezji | Cienkie osłony/narożniki/światło przy ścianie, obrazy gamma/gradientów i koszt pełnej klatki |
| §4.10, TIP: dalsze eksperymenty | APIC/FLIP, ciecz 0,5 i Radiance Cascades po odbiorze I08–I11; shape matching pozostaje warunkowy po podstawowej destrukcji/VFX | Uzasadnienie potrzeby, budżet i osobne A/B; żadnej gwarancji, że obecny PBF rozwiąże każdy problem |
| Część V: deklaracje QA autora | Zachować jako opis review, bez dopisywania do własnych PASS | Własne przyszłe raporty zawierają komendy, środowisko, wyniki i surowe artefakty |

Priorytet po decyzji użytkownika 2026-10-30: **I05 → I06 → I07–I10 → I11**; I04.5 odłożone bez odbioru celu. Szczegółowe zależności i checklisty są w głównym planie. Zapisanie zadania nie oznacza zaliczenia jego bramki.

## 2. Korekty i hipotezy wymagające sprawdzenia

| Propozycja/teza review | Rozstrzygnięcie dla planu |
| --- | --- |
| „Wszystkie przyczyny jednoznacznie wskazane”, DWM/limit tła i degradacja VRAM jako pełne wyjaśnienie | Hipotezy, nie zamknięta diagnoza. Raport I04 odnotowuje migracje m.in. po synchronicznej walidacji; to nie dowodzi ich występowania w normalnej grze. Korelować logi z dokładnym przebiegiem i wykonywać ablacje. |
| Immutable BufferStorage gwarantuje 100% VIDEO i usuwa id=131186 | Nie przyjmować gwarancji rezydencji ani przyczyny. Rozdzielić upload/readback od stanu fizyki, porównać ścieżki i zachować GL 4.3. Nie wyciszać ostrzeżenia w celu spełnienia bramki. |
| GL_TIMESTAMP jest zawsze nieblokujący i usunie limit 30 FPS | Nie zakładać. Porównać narzut i miejsca oczekiwania; gotowość wyniku oraz oczekiwanie sterownika przy zleceniu pracy to osobne sprawy. |
| RetainedSteps 24→3 jako poprawka wydajności | Nie przyjmować w I04.5. Może zmienić utratę czasu; polityka zegara wymaga osobnej decyzji i testu przeciążenia. |
| Korekta mniejsza od 1e-14 oznacza bitowo ten sam stan | Nie przyjmować. Nawet dokładne zero wymaga spełnionej projekcji i aktualnego terenu; próg przybliżony wymaga testu błędu. |
| Halo 3×3 i lokalne Dmax są zawsze kompletne | Wymagany dowód z aktualnymi przemieszczeniami obu końców i wpływem odległych kafelków. Przy braku dowodu globalny fallback, test all-pairs. |
| sortedHot zapewni 100% koalescencji i zmieści się zamiast anchorów | Hipoteza do pomiaru. Plan musi uwzględniać odświeżanie po korektach, reszty Kahana, ping-pong, dodatkowe bajty i bazowy limit wiązań SSBO. |
| 42/84 programy oraz rozbicie „14 mission + 3 coupling + 38 compute + 4 render” | Z kodu: **14 granular + 5 liquids + 8 neighbors + 8 mission + 3 coupling = 38 compute**, do tego 4 render = **42 na świat, 84 przy dwóch światach**. Podane rozbicie review liczy część elementów ponownie. |
| Migracja reguł CPU raz opisana jako i05 | I05 dotyczy własności świata/zasobów, I06 reguł CPU i kontraktu zdarzeń, zgodnie ze szczegółową roadmapą. |
| Binarny cache skraca „zimny start” do <40–50 ms | Rozdzielić pierwszą kompilację, trafienie cache i użycie już istniejącego świata. Każdy czas i oszczędność pamięci wymagają pomiaru. |
| 1024 zdarzenia wystarczą, bo w locie jest około 12 ticków | Tick może generować wiele zdarzeń. Obliczyć najgorszą emisję, zarezerwować miejsce przed zatwierdzeniem operacji, zdefiniować przeciążenie i zawijanie liczników. |
| Wzór macierzowy wirości §4.5 | Podana macierz pomnożona przez N daje wektor zerowy. Wyprowadzić właściwy iloczyn N×omega, sprawdzić znak i limit energii, zanim powstanie shader. |
| Normalizacja C_2D i gotowe stałe jąder | Review deklaruje całkę 0, a podany współczynnik odpowiada innej normalizacji. Niezależnie sprawdzić całkę, ciągłość, jednostki, znak siły i przypadki graniczne; nie kopiować wzoru jako zwalidowanego. |
| Refrakcja: solid(candidate) i test tylko końca | Kandydat nie może przechodzić przez przeszkodę. Potrzebna kontrola całego odcinka i maski faz, z bezpieczną próbką bez przesunięcia. |
| Otwarcie krawędzi gazu przez solid(A) AND solid(B) | Znaczenie maski jest odwrócone względem opisu. Ściana po którejkolwiek stronie blokuje krawędź; testować również skosy, otwory i edycję. |
| Entalpia/akumulator dają automatycznie dokładny bilans co do kwantu | Wymagane ograniczenie overflow, spójne snapshoty, symetryczne strumienie, wspólny limit dla wielu cząstek czerpiących z jednej komórki i pomiar błędu po zapisie float. |
| 12–16 iteracji kafelkowego SOR gwarantuje rozwiązanie | Liczba iteracji jest kandydatem. Zmierzyć residuum/dywergencję, zapewnić synchronizację między kafelkami, CFL i dodatniość transportu. |
| Równanie spalania §4.8 i uniwersalne stałe metalu | Równanie jest uszkodzone, zawiera dodanie zamiast ubytku paliwa i doklejony fragment faz. Odtworzyć bilans ze spójnego modelu materiałów/testów CPU; określić jednostki szybkości, produkty i energię chemiczną. |
| JFA w R16UI przechowuje dwie współrzędne u16 | Użyć RG16UI albo jednego liniowego ID z dekodowaniem i znacznikiem pustym. Format zależy od rzeczywistego kodowania. |
| JFA+1 + 16 kroków + pojedynczy odczyt gwarantują dokładne SDF i szczelność | Nie przyjmować. Porównać z referencją i weryfikacją odcinkową; ustalić konserwatywny fallback po wyczerpaniu kroków. |
| Wilgotność w starszych bitach motion.w | Najpierw jawne ABI i dekodowanie wszystkich porównań typu. Wilgoć wizualna nie oznacza wdrożenia kohezji mokrego piasku. |
| Bramka tabeli I09: wyłączenie VFX „zmienia masę i trajektorię” | Poprawiono: wyłączenie VFX **nie zmienia** masy, trajektorii fizycznych ani wyniku. |
| Ujemne czasy efektów i precyzyjne prognozy oszczędności | W planie rozdzielono dodatni koszt efektu od przyrostu/spadku względem bazy oraz tick od klatki. Wszystkie prognozy pozostają roboczymi budżetami. |

Podstawy sprawdzenia lokalnego: [alokacje/programy/timery/render](../../src/gpu/world.cpp), [światy i pętla aplikacji](../../src/app/application.cpp), [solver cieczy](../../assets/shaders/liquids.comp), [kolor ziaren](../../assets/shaders/granular.vert), [materiały CPU](../../src/sim/material.hpp), [ograniczenia pomiarów I04](../reports/I04_GPU_COUPLING.md). Była to inspekcja statyczna; nie potwierdza nowych czasów GPU ani deklaracji QA autora.

## 3. Pierwszy zakres: I04.5-A — instrumentacja i seria pomiarowa wykonane

1. Odtworzyć bazę I04 w Release i zapisać dokładny build, GPU, sterownik, rozdzielczość oraz warunki fokusu/VSync.
2. Dodać pomiar `present()` i brakujących faz klatki; porównać elapsed/timestamp/bez profilera bez zmiany solvera.
3. Rozdzielić normalny runtime od pełnych odczytów walidacyjnych w logach migracji buforów.
4. Wykonać co najmniej trzy powtórzenia obu wariantów w przeplocie ABBA; odbiór okna obejmuje trzy próby referencji i trzy kandydata, z tymi samymi kryteriami utraty czasu.
5. Na podstawie profilu wybrać B/C, a D/E uruchamiać wyłącznie przy potwierdzonym koszcie. Kolor gruzu F ma osobne, małe porównanie obrazu.

Zapisane artefakty: [raport I04.5-A/F](../reports/I04_5_GPU_STABILIZATION.md), [dane pomiarowe](https://github.com/maershal/strefa-zero-measurements/tree/88ba1601a3c6ab19b0e97a3ed8e679a593e09539/gpu-i04-5), [raport C i wyniki QA](../reports/I04_5C_GPU_WORK.md), aktualizacja statusu/checklisty/dziennika w głównym planie. Raporty podają również nieosiągnięte cele. Samo zniknięcie komunikatu sterownika, szybszy test bez okna albo skrócenie zaległości zegara nie zalicza etapu.

Stan po B, przed badaniem D: po C zapisano 16/16 pomiarów bez potwierdzonego przyspieszenia; opcje C są OFF. Badanie B obejmuje UploadRing, immutable flags=0 oraz fallback GL 4.3, z wynikiem 28/28 QA i 36/36 procesów porównawczych. Direct pozostaje domyślne. Przyczyna różnicy warunków między sesjami nie jest zamknięta; dalsze D/E wymagają przypisania kosztu. I05 nie rozpoczyna się automatycznie. Szczegóły: [raport B](../reports/I04_5B_GPU_TRANSPORT.md).

Stan po D, przed E0: badanie D porównało full/narrow/sorted w 18 procesach. Cache aktualizuje pełną geometrię kompensowaną i motion wraz z producentami, mieści się w 8 SSBO, lecz kosztuje 2 MiB i spowalnia większe sceny. Full pozostaje domyślne. Pełne QA 35/35 oraz siedem prób okna przeszły; dziewięć końcowych snapshotów Dmax=0,0625 nie wyklucza chwilowych outlierów. E czeka na profil w czasie i dowód kompletności. [Raport D](../reports/I04_5D_GPU_NEIGHBOR_READS.md).

Stan po E0, przed Q: E0 wykonało 16/16 pomiarów i 6/6 kontroli, z 384 snapshotami bez pominiętych geometrycznych par. Profil wskazał PBF i kontakty ziaren; lokalnego Dmax nie wdrożono. Pełne QA 37/38 zachowuje błąd sprzężenia: powtórka E0 przeszła, niezmienione D odtworzyło ten sam błąd. Nie zmieniono tolerancji. Najpierw baza QA, potem punktowy eksperyment PBF G. [Raport E0](../reports/I04_5E0_GPU_COSTS.md).

Q wykazało fałszywe odrzucenie identycznych referencji przez dawną regułę (1/16). Metodę odbioru zmieniono jawnie na wspólne checkpointy i stałe budżety; 12/12 powtórek i wykryte wszystkie celowe wyłączenia sprzężenia. Pełne 38/38 dotyczy nowego protokołu. Nie jest to zaliczenie dawnego ilorazu ani naprawa deterministyczności fizyki; stare wyniki pozostają. [Raport Q](../reports/I04_5Q_GPU_COUPLING_QA.md).

Stan po G3-A: dodano obserwację zewnętrzną i pojedynczą ustaloną serię bodźców, bez zmiany solvera. Własny narzut sond oddzielono kontrolami OFF; obserwator ma kontrolę OFF/ON/ON/OFF, koszt workera WMI nie jest osobno wyizolowany. Wysoki CPU wystąpił także przy GPU około 2,9 ms. Nie ma wolnego reżimu z przechwyconą geometrią, więc nie zamykamy hipotez DWM/VRAM/sterownika ani nie przypisujemy ich na podstawie samej telemetrii. Następne G3-B wymaga pełnego historycznego preludium i uchwycenia rzeczywistego spowolnienia. [Raport G3-A](../reports/I04_5G3_GPU_VARIANCE.md).

Stan po G3-B: pełne wcześniejsze obciążenie raz odtworzyło wolną G1 i pozwoliło zebrać ślady Dmax/rekonstrukcje w tym reżimie. Mała zmienność kandydatów misji i identyczne badane histogramy 16k/32k nie uzasadniają lokalnego Dmax jako naprawy skoku. Nie rozpoznano limitu mocy/zegara ani błędu sterownika. Powrót wydajności nastąpił przed widoczną próbą; fokus w follow-up nie został uzyskany. G3-C rozdziela upływ czasu i nowy proces nadrzędny oraz uzupełnia stan wykonania. [Raport G3-B](../reports/I04_5G3B_GPU_REPLAY.md).
