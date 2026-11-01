# Obrazy kontroli jakości

Pełne surowe obrazy BMP z I04–I11 są dostępne w [wydaniu I11 Preview](https://github.com/maershal/strefa-zero/releases/tag/v0.7.0-i11-preview.1) jako `StrefaZero-QA-images-I04-I11.zip`. Archiwum zachowuje pierwotne ścieżki `docs/<etap>/...`; dane etapów leżą teraz w osobnym repozytorium [strefa-zero-measurements](https://github.com/maershal/strefa-zero-measurements) jako `<etap>/`. Sklonuj je, rozpakuj archiwum do katalogu tymczasowego i skopiuj zawartość jego `docs/` do katalogu głównego klonu, nie nadpisując plików:

```bash
git clone https://github.com/maershal/strefa-zero-measurements.git
unzip StrefaZero-QA-images-I04-I11.zip -d /tmp/strefa-qa
cp -rn /tmp/strefa-qa/docs/. strefa-zero-measurements/
```

Archiwum zawiera własny manifest `QA-IMAGES-SHA256SUMS.txt`; obok pliku ZIP opublikowano również jego sumę SHA-256.

Repozytorium gry zawiera kod i raporty; CSV, logi, JUnit, metadane prób, porównania PNG i część BMP są w repozytorium pomiarów. Pełny komplet surowych BMP nie jest śledzony w żadnym z nich, aby kolejne serie nie powiększały historii o setki megabajtów. Starsze commity strefa-zero nadal zawierają wcześniej śledzone dane.

Gotowy [ZIP Windows I11](https://github.com/maershal/strefa-zero/releases/download/v0.7.0-i11-preview.1/StrefaZero-Windows-x64-I11.zip) zawiera program oraz pełne dane i obrazy etapu I11. To wydanie testowe: oświetlenie i termika pozostają domyślnie wyłączone, a ograniczenia jakości i wydajności opisują raporty etapów.
