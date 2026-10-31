# Obrazy kontroli jakości

Pełne surowe obrazy BMP z I04–I11 są dostępne w [wydaniu I11 Preview](https://github.com/maershal/strefa-zero/releases/tag/v0.7.0-i11-preview.1) jako `StrefaZero-QA-images-I04-I11.zip`. Rozpakuj archiwum do głównego katalogu repozytorium: zachowuje ścieżki `docs/...`, do których odwołują się raporty. Archiwum zawiera własny manifest `QA-IMAGES-SHA256SUMS.txt`; obok pliku ZIP opublikowano również jego sumę SHA-256.

Repozytorium zawiera kod, raporty, CSV, logi, JUnit, metadane prób i porównania PNG. Nowe surowe BMP są wyłączone z Gita, aby kolejne serie pomiarów nie powiększały historii o setki megabajtów. Wcześniej śledzone obrazy pozostają w historii. Główny `SHA256SUMS.txt` obejmuje pliki publikowane w Git, z wyłączeniem samego manifestu.

Gotowy [ZIP Windows I11](https://github.com/maershal/strefa-zero/releases/download/v0.7.0-i11-preview.1/StrefaZero-Windows-x64-I11.zip) zawiera program oraz pełne dane i obrazy etapu I11. To wydanie testowe: oświetlenie i termika pozostają domyślnie wyłączone, a ograniczenia jakości i wydajności opisują raporty etapów.
