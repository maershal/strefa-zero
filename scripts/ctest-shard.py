#!/usr/bin/env python3
"""Splits CTest tests into balanced shards and prints one shard as an anchored `ctest -R` regex.

Usage: scripts/ctest-shard.py TESTS.json SHARDS INDEX [JUNIT.xml...] [--list]

TESTS.json is `ctest --show-only=json-v1` output (already filtered, e.g. with -L graphics); INDEX is 1-based.
Weights are the test times from the given JUnit reports (an earlier run); a test missing from them weighs its
TIMEOUT. Tests linked by a fixture stay in one shard. Greedy longest-first assignment to the lightest shard is
deterministic, so every shard job computes the same split. --list prints `weight name` lines instead of the regex.
"""

import json
import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path


def properties(test: dict) -> dict:
    return {p["name"]: p["value"] for p in test.get("properties", [])}


def measured_times(reports: list[str]) -> dict[str, float]:
    times = {}
    for report in reports:
        path = Path(report)
        if not path.is_file():
            continue
        for case in ElementTree.parse(path).getroot().iter("testcase"):
            times[case.get("name")] = float(case.get("time", 0))
    return times


def groups(tests: list[dict]) -> list[list[str]]:
    # Union-find over fixture names: a setup test and every test requiring it run in the same shard.
    parent = {t["name"]: t["name"] for t in tests}

    def find(name: str) -> str:
        while parent[name] != name:
            parent[name] = parent[parent[name]]
            name = parent[name]
        return name

    owner = {}
    for test in tests:
        props = properties(test)
        for fixture in props.get("FIXTURES_SETUP", []) + props.get("FIXTURES_REQUIRED", []):
            if fixture in owner:
                parent[find(test["name"])] = find(owner[fixture])
            else:
                owner[fixture] = test["name"]
    members = {}
    for test in tests:
        members.setdefault(find(test["name"]), []).append(test["name"])
    return list(members.values())


def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--list"]
    listing = len(args) != len(sys.argv) - 1
    if len(args) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    tests = json.loads(Path(args[0]).read_text(encoding="utf-8")).get("tests", [])
    shards, index = int(args[1]), int(args[2])
    if shards < 1 or not 1 <= index <= shards:
        print("INDEX musi należeć do 1..SHARDS.", file=sys.stderr)
        return 2
    if not tests:
        print("Brak testów w TESTS.json.", file=sys.stderr)
        return 1
    times = measured_times(args[3:])
    weight = {}
    for test in tests:
        name = test["name"]
        weight[name] = times.get(name, float(properties(test).get("TIMEOUT", 1500)))
    loads = [0.0] * shards
    assigned = [[] for _ in range(shards)]
    for group in sorted(groups(tests), key=lambda g: (-sum(weight[n] for n in g), g[0])):
        lightest = min(range(shards), key=lambda s: (loads[s], s))
        loads[lightest] += sum(weight[n] for n in group)
        assigned[lightest] += group
    chosen = sorted(assigned[index - 1])
    if listing:
        print(f"# shard {index}/{shards}: {len(chosen)} tests, estimated {loads[index - 1]:.0f} s")
        for name in chosen:
            print(f"{weight[name]:.0f} {name}")
        return 0
    if not chosen:
        # More shards than test groups: match nothing instead of everything.
        print("^$")
        return 0
    print("^(" + "|".join(re.escape(name) for name in chosen) + ")$")
    return 0


if __name__ == "__main__":
    sys.exit(main())
