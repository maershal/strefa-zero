@echo off
"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir "C:\Users\mrmae\Documents\Codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\work\build-strefa-zero-release" --rerun-failed --output-on-failure --output-junit "C:\Users\mrmae\Documents\Codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\work\i04-5b-results\baseline-ctest.xml" > "C:\Users\mrmae\Documents\Codex\2026-10-24\chcia-bym-stworzy-gr-typu-soldat-3\work\i04-5b-results\baseline-ctest.log" 2>&1
exit /b %errorlevel%
