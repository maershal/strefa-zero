# ADR 0001 — rola modelu CPU

- Status: przyjęta (2026-11-04, decyzja właściciela w zadaniu K06)
- Dotyczy: `src/sim` (`strefa_sim`), laboratorium CPU (`ui/laboratory.*`, `--laboratory`, `--scene lab:<id>`),
  `StrefaPhysics`, kontekst OpenGL 3.3 (`--opengl33`)

## Kontekst

Strefa Zero ma dwa modele materiałów. Model GPU (`gpu::ParticleWorld`: ciecze PBF, ziarna PBD, termika i gazy) jest
produktem: na nim działają Misja GPU i Laboratorium GPU. Model CPU (`sim::World`: termika, ciecze MAC/PCG, powietrze,
reakcje) jest starszy, wolniejszy i dokładniejszy. Po usunięciu Poligonu CPU (#22) nie zależy od niego żadna
rozgrywka, a review v2 (R §1, §11; A §6) proponuje, żeby został wyłącznie wzorcem. Bez zapisanej decyzji każdy PR
mógłby rozwijać laboratorium CPU albo usuwać ścieżkę OpenGL 3.3, na której ono działa.

## Decyzja

1. **Model CPU jest wyrocznią, nie produktem.** Służy do:
   - testów: `sim.golden_master`, `physics.*`, `sim.*`, zgodność termiki GPU z CPU (`--gpu-thermal-conformance`);
   - prototypowania reguł materiałów i reakcji, zanim trafią na GPU;
   - pomiarów i scen wsadowych w `StrefaPhysics`.

   Ma być poprawny i deterministyczny (ten sam wynik przy 1/2/4 wątkach), a nie szybki: nie optymalizujemy go pod
   czas rzeczywisty.
2. **Laboratorium CPU jest funkcjonalnie zamrożone.** W menu nazywa się „Model referencyjny (CPU)”. Dopuszczalne są
   poprawki błędów wyroczni, zmiany potrzebne testom i porządki bez zmiany zachowania. Nowe narzędzia, materiały
   i sceny trafiają do Laboratorium GPU.
3. **OpenGL 3.3 jest zamrożony.** Kontekst 3.3 (`--opengl33`, `app.opengl33_smoke`) zostaje w obecnym zakresie:
   menu, ustawienia i model referencyjny (CPU); tryby GPU są na nim wyszarzone. Nie rozwijamy go, a jego usunięcie
   wymaga zgody właściciela (komentarz w PR), tak jak każda zmiana kontraktu GL 3.3 w `CLAUDE.md`.

## Konsekwencje

- PR dodający funkcję do laboratorium CPU albo do ścieżki GL 3.3 jest odrzucany, chyba że właściciel zdecyduje
  inaczej; recenzent (`pr-reviewer`) powołuje się na ten ADR.
- Kod CPU nadal podlega regułom determinizmu i `sim.golden_master`; zmiana zachowania wyroczni wymaga raportu
  `scripts/golden.py` w opisie PR.
- Warstwy zostają bez zmian: model CPU nie zależy od GPU, UI ani aplikacji (`scripts/check_architecture.py`), a gra
  nie potrzebuje go do działania (HUD misji i `strefa_input` nie linkują `strefa_sim` od K06).
- Usunięcie laboratorium CPU albo GL 3.3 to osobna decyzja, najwcześniej po parytecie Laboratorium GPU (R §11).

## Źródła

- [Review v2 — status](../plans/REVIEW_V2_STATUS.md), decyzje właściciela 2 i 6.
- [review_strefa_zero_v2.md](../plans/reviews/review_strefa_zero_v2.md) §1, §11; [architecture_v2.md](../plans/reviews/architecture_v2.md) §6.
