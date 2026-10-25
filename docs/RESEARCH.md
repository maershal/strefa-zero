# Źródła metod i granice inspiracji

Implementacja jest własna w C++23. Kod The Powder Toy ani Basilisk nie został włączony do projektu. Przypięte zależności to SDL 3.4.18 i Dear ImGui 1.93.0; OpenGL 3.3 dostarcza sterownik. Licencje dystrybuowanych bibliotek i fontów znajdują się w `licenses/`.

- [Referencja wizualna ParticleStatesRecording](https://claude.ai/artifact/XSREHzXnVKzCoUAEM8qrcr): katalog 37 scen, zapis co trzy kroki, podglądy i diagnostyka. Nagrania są wzorcem testowych sytuacji i prezentacji, bez wnioskowania o nieudostępnionych algorytmach.
- [Basilisk centered Navier–Stokes](https://basilisk.fr/src/navier-stokes/centered.h): rozdzielenie przewidywania prędkości, sił, zmiennej gęstości i projekcji ciśnienia. Nasz operator jest pięciopunktowy, stałosiatkowy i ma własny PCG/IC(0).
- [Basilisk two-phase](https://basilisk.fr/src/two-phase.h): transport udziału fazowego i interpolacja gęstości. Nasz donor-cell jest prostszy i bardziej dyfuzyjny niż geometryczny VOF; nie deklarujemy równoważności z Basilisk.
- [Clawpack: acoustics](https://www.clawpack.org/riemann_book/html/Acoustics.html): liniowa para równań ciśnienia i prędkości. Używamy ograniczonej amplitudy, tłumienia i prędkości propagacji dobranej do gry, bez pełnego opisu detonacji.
- [GPU Gems, Fast Fluid Dynamics](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu): podział sił, adwekcji i projekcji oraz warunki brzegowe. Implementacja 0.4 pozostaje na CPU.
- [The Powder Toy: Air.cpp](https://github.com/The-Powder-Toy/The-Powder-Toy/blob/master/src/simulation/Air.cpp): inspiracja oddzielnymi polami powietrza, temperatury, ciśnienia, transportu i masek przeszkód. Termit, metal i nitrogliceryna są inspiracją repertuarem materiałów. Nie kopiujemy jego kodu ani nie uznajemy automatu za laboratoryjny model chemii.

Wykorzystanie dokumentacji jako opisu metody nie oznacza skopiowania licencjonowanej implementacji. Parametry gry są jawne w `material.hpp` i `PhysicsConfig`; wyniki i ograniczenia opisuje `PARTICLES.md` oraz raport wydania.
