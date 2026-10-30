from pathlib import Path
r=Path(__file__).resolve().parent.parent;s=r/'outputs/strefa-zero'
p=s/'docs/PLAN_ROZWOJU_GPU.md';x=p.read_text();lines=x.splitlines()
lines[2]=lines[2].replace('2026-10-29','2026-10-30')
lines[3]='Status: **I04.5-A/F wdrożone; B/C zbadane jako eksperymenty. B: 28/28 QA i 36/36 procesów pomiarowych. Domyślne direct oraz obie opcje C OFF. Cel 1,5 ms i przyczyna historycznej zmienności pozostają otwarte; następne przypisanie kosztu przed decyzją D/E. I05–I11 nadal zaplanowane**.'
for i,line in enumerate(lines):
 if line.startswith('Aktualne QA po C:'):
  lines[i]='Aktualne QA po B: **28/28 PASS**, bez zmiany limitów i tolerancji. Historyczny wynik C 22/26 i późniejsza powtórka 3/4 pozostają opisane w raportach; bieżący sukces nie dowodzi usunięcia dawnej zmienności.'
 if line.startswith('| I04.5 | Diagnoza'):
  lines[i]='| I04.5 | Diagnoza okna/pamięci, ograniczenie pustej pracy, kolor gruzu | I00–I04 | A/F [x], B/C zbadane jako eksperymenty; direct domyślne, opcje C OFF. Cel i stabilność warunków otwarte; D/E warunkowe |'
 if line.startswith('- [ ] **B — transport'):
  lines[i]='- [x] **B — badanie transportu i podziału pamięci:** odtworzono bazę QA bez zmiany limitów/tolerancji. Dodano trzy sloty UploadRing, ścieżki direct/staged/immutable/auto i immutable storage z flags=0 bez bezpośrednich zapisów CPU. Fallback staged używa GL 4.3. Pełne QA 28/28 oraz 36/36 procesów porównawczych; brak spójnej przewagi uzasadniającej zmianę domyślnego direct. Początkowa polityka odczytów obejmuje cały cykl od create(). Szczegóły i odrzucone hipotezy w raporcie B. Miejsce pamięci nadal wybiera sterownik; id=131186 liczono według faz, bez wyciszania callbacku.'
 if line.startswith('Wdrożony i zweryfikowany zakres A/F:'):
  lines[i]='Wdrożony i zweryfikowany zakres A/F: [raport A/F](I04_5_GPU_STABILIZATION.md). [Eksperyment C](I04_5C_GPU_WORK.md) pozostaje OFF. [Badanie B](I04_5B_GPU_TRANSPORT.md): 28/28 QA, 36/36 pomiarów, direct nadal domyślne. Ten sam build pracował w wolnym i szybkim reżimie w różnych sesjach; przyczyna zmienności nie jest zamknięta. Następne jest przypisanie kosztu obecnych przebiegów przed decyzją D/E. Pełny odbiór całego I04.5 pozostaje otwarty.'
 if line.startswith('| I04.5-C |'):
  lines.insert(i+1,'| I04.5-B | Badanie i implementacja opt-in zakończone 2026-10-30; direct domyślne | [Raport B](I04_5B_GPU_TRANSPORT.md): UploadRing, immutable flags=0, fallback GL 4.3, 28/28 QA, 36/36 procesów; brak spójnej przewagi i zmienność tego samego EXE między sesjami | Przypisanie kosztu i decyzja D/E; cel 1,5 ms oraz stabilność warunków nadal otwarte |')
x='\n'.join(lines)+'\n'
x=x.replace('a warianty C, pomiary i wyniki QA — [raport I04.5-C](I04_5C_GPU_WORK.md).','warianty C opisuje [raport C](I04_5C_GPU_WORK.md), a transport i aktualne QA — [raport B](I04_5B_GPU_TRANSPORT.md).')
marker='\n## 8. Materiały pomocnicze'
x=x.replace(marker,'\n- **2026-10-30, I04.5-B:** pozostawiamy direct; staged/immutable/auto są eksperymentalne. Pełne QA 28/28 i 36/36 procesów bez błędu. Niewielka przewaga mediany w jednym warunku nie utrzymuje się we wszystkich scenach; nowe ścieżki kosztują dodatkową pamięć. Duża zmiana wydajności między sesjami wystąpiła na tym samym EXE, więc nie przypisujemy jej uploadowi ani nie deklarujemy naprawy warunków systemowych. D/E wymagają najpierw przypisania kosztu; I05 nie rozpoczyna się automatycznie.\n'+marker)
p.write_text(x)
p=s/'README.md';x=p.read_text().replace('**Aktualizacja planu 2026-10-29:**','**Aktualizacja planu 2026-10-30:**')
x=x.replace('Kolejny zakres to **I04.5-B — najpierw odtworzenie i wyjaśnienie bazy QA, potem transport i inicjalizacja**. D/E wymagają uzasadnienia pomiarami.','Badanie transportu opisuje [raport I04.5-B](docs/I04_5B_GPU_TRANSPORT.md); D/E nadal wymagają uzasadnienia pomiarami.')
a=x.index('**Aktualne QA po C:**');b=x.index('\n',a)
x=x[:a]+'**I04.5-B — transport:** dodano eksperymentalny UploadRing i ścieżki staged/immutable/auto z fallbackiem GL 4.3. Pełne QA **28/28 PASS**, porównanie **36/36 procesów**. Brak spójnej przewagi uzasadniającej zmianę domyślnego `direct`; duża zmienność między sesjami wystąpiła na identycznym EXE. Historyczne błędy C i ograniczenia są zachowane w [raporcie B](docs/I04_5B_GPU_TRANSPORT.md).'+x[b:]
x=x.replace('Aktualny zakres po review: I04.5-B — odtworzenie i wyjaśnienie otwartej bazy QA przed dalszymi zmianami transportu i inicjalizacji.','Aktualny zakres po review: przypisanie kosztu przebiegów przed warunkowym D/E. B zakończono jako badanie; direct pozostaje domyślne.')
p.write_text(x)
p=s/'docs/REVIEW_I04_ROZSTRZYGNIECIA.md';lines=p.read_text().splitlines();lines[2]+=' Aktualizacja: 2026-10-30.'
lines[3]='Status: **I04.5-A/F wdrożone; B/C zbadane jako eksperymenty. Domyślne direct i obie opcje C OFF. B: 28/28 QA oraz 36/36 procesów. Cel 1,5 ms i przyczyna zmienności pozostają otwarte; następne przypisanie kosztu przed decyzją D/E**. [Raport A/F](I04_5_GPU_STABILIZATION.md), [C](I04_5C_GPU_WORK.md), [B i aktualne QA](I04_5B_GPU_TRANSPORT.md).'
lines[5]='Aktualne QA po B: **28/28 PASS**, bez zmiany limitów/tolerancji. Historyczny C 22/26 i powtórka 3/4 przed B są zachowane. B nie wykazało spójnej przewagi transportu; powrót szybszego reżimu nastąpił na identycznym EXE i nie jest dowodem naprawy historycznej zmienności.'
x='\n'.join(lines)+'\n';a=x.index('Po C zapisano');x=x[:a]+'''Po C zapisano 16/16 pomiarów bez potwierdzonego przyspieszenia; opcje C są OFF. Badanie B obejmuje UploadRing, immutable flags=0 oraz fallback GL 4.3, z wynikiem 28/28 QA i 36/36 procesów porównawczych. Direct pozostaje domyślne. Przyczyna różnicy warunków między sesjami nie jest zamknięta; dalsze D/E wymagają przypisania kosztu. I05 nie rozpoczyna się automatycznie. Szczegóły: [raport B](I04_5B_GPU_TRANSPORT.md).
''';p.write_text(x)
p=r/'outputs/03_PLAN_PRAC.md';lines=p.read_text().splitlines();lines[2]='Aktualizacja odsyłacza 2026-10-30: bieżący plan techniczny znajduje się w [PLAN_ROZWOJU_GPU.md](strefa-zero/docs/PLAN_ROZWOJU_GPU.md), a [rejestr review](strefa-zero/docs/REVIEW_I04_ROZSTRZYGNIECIA.md) wyjaśnia przyjęte zadania. Badanie [I04.5-B](strefa-zero/docs/I04_5B_GPU_TRANSPORT.md) zakończono: direct pozostaje domyślne, opcje C są OFF. Następne jest przypisanie kosztu przebiegów przed warunkowym D/E. Cel 1,5 ms i cały I04.5 pozostają otwarte; I05–I11 czekają na zależności i odbiór. Poniższy dokument zachowuje ogólny podział produktu.'
lines[4]='Aktualne QA po B: **28/28 PASS**, seria **36/36 procesów**. Historyczny wynik C 22/26 i późniejsza powtórka 3/4 pozostają w raportach; nie zmieniono limitów ani tolerancji. Powrót szybszego reżimu na identycznym EXE nie zamyka diagnozy wcześniejszej niestabilności.';p.write_text('\n'.join(lines)+'\n')
