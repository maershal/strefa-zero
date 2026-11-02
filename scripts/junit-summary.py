#!/usr/bin/env python3
"""Markdown summary of ctest --output-junit reports, for $GITHUB_STEP_SUMMARY.

Usage: scripts/junit-summary.py TITLE REPORT.xml... (missing files are listed as absent)
"""

import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    lines = [f"### {sys.argv[1]}", "", "| Raport | Testy | Zaliczone | Błędy | Pominięte | Czas [s] |",
             "| --- | ---: | ---: | ---: | ---: | ---: |"]
    failures = []
    for name in sys.argv[2:]:
        path = Path(name)
        if not path.is_file():
            lines.append(f"| {path.stem} | brak raportu | | | | |")
            continue
        suite = ElementTree.parse(path).getroot()
        cases = suite.findall("testcase")
        failed = [c for c in cases if c.find("failure") is not None or c.find("error") is not None]
        skipped = [c for c in cases if c.find("skipped") is not None]
        seconds = sum(float(c.get("time", 0)) for c in cases)
        passed = len(cases) - len(failed) - len(skipped)
        lines.append(f"| {path.stem} | {len(cases)} | {passed} | {len(failed)} | {len(skipped)} | {seconds:.1f} |")
        failures += [f"{path.stem}: `{c.get('name')}`" for c in failed]
        slowest = sorted(cases, key=lambda c: float(c.get("time", 0)), reverse=True)[:5]
        if slowest:
            names = ", ".join("`{}` {:.1f}".format(c.get("name"), float(c.get("time", 0))) for c in slowest)
            lines.append(f"| ↳ najwolniejsze | {names} | | | | |")
    if failures:
        lines += ["", "**Niezaliczone:**", *[f"- {f}" for f in failures]]
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
