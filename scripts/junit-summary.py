#!/usr/bin/env python3
"""Markdown summary of ctest --output-junit reports, for $GITHUB_STEP_SUMMARY.

Usage: scripts/junit-summary.py [--fail-on-skipped] TITLE REPORT.xml... (missing files are listed as absent)

--fail-on-skipped: exit 1 when a report has skipped tests (exit 77), e.g. GPU tests without OpenGL 4.3 in CI,
or when a report is missing (a step that never wrote it must not pass as "nothing skipped").
"""

import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path


def main() -> int:
    args = sys.argv[1:]
    fail_on_skipped = bool(args) and args[0] == "--fail-on-skipped"
    if fail_on_skipped:
        args = args[1:]
    if len(args) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    lines = [f"### {args[0]}", "", "| Raport | Testy | Zaliczone | Błędy | Pominięte | Czas [s] |",
             "| --- | ---: | ---: | ---: | ---: | ---: |"]
    failures = []
    skips = []
    missing = []
    for name in args[1:]:
        path = Path(name)
        if not path.is_file():
            lines.append(f"| {path.stem} | brak raportu | | | | |")
            missing.append(path.stem)
            continue
        suite = ElementTree.parse(path).getroot()
        cases = suite.findall("testcase")
        failed = [c for c in cases if c.find("failure") is not None or c.find("error") is not None]
        skipped = [c for c in cases if c.find("skipped") is not None]
        seconds = sum(float(c.get("time", 0)) for c in cases)
        passed = len(cases) - len(failed) - len(skipped)
        lines.append(f"| {path.stem} | {len(cases)} | {passed} | {len(failed)} | {len(skipped)} | {seconds:.1f} |")
        failures += [f"{path.stem}: `{c.get('name')}`" for c in failed]
        skips += [f"{path.stem}: `{c.get('name')}`" for c in skipped]
        slowest = sorted(cases, key=lambda c: float(c.get("time", 0)), reverse=True)[:5]
        if slowest:
            names = ", ".join("`{}` {:.1f}".format(c.get("name"), float(c.get("time", 0))) for c in slowest)
            lines.append(f"| ↳ najwolniejsze | {names} | | | | |")
    if failures:
        lines += ["", "**Niezaliczone:**", *[f"- {f}" for f in failures]]
    if skips and fail_on_skipped:
        lines += ["", "**Pominięte (niedozwolone w tym kroku):**", *[f"- {s}" for s in skips]]
    if missing and fail_on_skipped:
        lines += ["", "**Brak raportu (niedozwolone w tym kroku):**", *[f"- `{m}`" for m in missing]]
    print("\n".join(lines))
    return 1 if (skips or missing) and fail_on_skipped else 0


if __name__ == "__main__":
    sys.exit(main())
