#!/usr/bin/env python3
"""Regenerate the sim.golden_master reference for the toolchain of a build and report what changed.

    python3 scripts/golden.py --build-dir build/linux-release-core          # rewrite tests/golden/<toolchain>.txt
    python3 scripts/golden.py --build-dir build/release-core --check        # compare only, exit 1 on a difference
    python3 scripts/golden.py --compare OLD.txt NEW.txt                     # report between two dumps

The report is Markdown (one row per case and thread count that changed) so it can go into a PR description.
An intended physics change needs that report and an explanation; never regenerate to make a failure go away.
"""
import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BINARY = "strefa_golden_master_tests"


def parse(text):
    toolchain, values = None, {}
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("# sim.golden_master reference, toolchain "):
            toolchain = line.split("toolchain ", 1)[1].split(",", 1)[0]
        if not line or line.startswith("#"):
            continue
        name, threads, field, value = line.split()
        values[(name, int(threads), field)] = value
    return toolchain, values


def report(old, new, title):
    runs = {}
    for key in sorted(set(old) | set(new)):
        before, after = old.get(key), new.get(key)
        if before != after:
            runs.setdefault(key[:2], []).append((key[2], before or "-", after or "-"))
    total = len({key[:2] for key in set(old) | set(new)})
    lines = [f"### {title}", ""]
    if not runs:
        lines.append(f"No differences ({total} runs, {len(new)} values).")
        return "\n".join(lines), 0
    lines += [f"{len(runs)} of {total} runs differ.", "", "| Case | Threads | Changed fields | Before -> after |",
              "|------|---------|----------------|----------------|"]
    for (name, threads), fields in runs.items():
        names = ", ".join(field for field, _, _ in fields)
        values = "<br>".join(f"{field}: `{b}` -> `{a}`" for field, b, a in fields)
        lines.append(f"| {name} | {threads} | {names} | {values} |")
    return "\n".join(lines), len(runs)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=pathlib.Path)
    parser.add_argument("--golden-dir", type=pathlib.Path, default=ROOT / "tests" / "golden")
    parser.add_argument("--check", action="store_true", help="compare only, do not write")
    parser.add_argument("--compare", nargs=2, type=pathlib.Path, metavar=("OLD", "NEW"))
    args = parser.parse_args()

    if args.compare:
        _, old = parse(args.compare[0].read_text(encoding="utf-8"))
        _, new = parse(args.compare[1].read_text(encoding="utf-8"))
        text, changed = report(old, new, f"{args.compare[0]} -> {args.compare[1]}")
        print(text)
        return 1 if changed else 0

    if not args.build_dir:
        parser.error("wymagane --build-dir albo --compare")
    candidates = [args.build_dir / BINARY, args.build_dir / f"{BINARY}.exe"]
    binary = next((c for c in candidates if c.is_file()), None)
    if binary is None:
        sys.exit(f"Brak {BINARY} w {args.build_dir}; najpierw zbuduj testy")
    dump = subprocess.run([str(binary), "--print"], check=True, capture_output=True, text=True).stdout
    toolchain, new = parse(dump)
    if not toolchain:
        sys.exit("Binarka testu nie wypisała nagłówka z nazwą toolchainu")
    reference = args.golden_dir / f"{toolchain}.txt"
    old = parse(reference.read_text(encoding="utf-8"))[1] if reference.is_file() else {}
    text, changed = report(old, new, f"Golden master ({toolchain})")
    print(text)
    if args.check:
        return 1 if changed else 0
    if changed or not reference.is_file():
        reference.parent.mkdir(parents=True, exist_ok=True)
        reference.write_text(dump, encoding="utf-8", newline="\n")
        print(f"\nWrote {reference.relative_to(ROOT) if reference.is_relative_to(ROOT) else reference}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
