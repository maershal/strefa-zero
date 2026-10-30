#!/usr/bin/env python3
"""Reconcile identical-reference controls and common-state coupling replays."""
from __future__ import annotations
import argparse
from collections import defaultdict
import csv
import json
import math
from pathlib import Path
import re


def csv_rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def analyze(root: Path):
    controls = json.loads((root/"reference-only-results.json").read_text())
    legacy = []
    for item in controls:
        folder = root/"reference-only"/str(item["repeat"])
        text = (folder/"coupling-tests.txt").read_text()
        assert "geometry_target=identical_reference_control" in text and not item["timed_out"]
        values = [dict(re.findall(r"(geometry_run|mean|center|actor_max)=([\d.eE+-]+)", line)) for line in text.splitlines() if line.startswith("geometry_run=")]
        assert len(values) == 2
        reference, candidate = ({key: float(value) for key, value in row.items()} for row in values)
        limits = {"mean": max(.05, 2*reference["mean"]), "center": max(.02, 2*reference["center"]), "actor_max": max(.15, 2*reference["actor_max"])}
        accepted = all(candidate[key] < limit for key, limit in limits.items())
        assert accepted == (item["exit_code"] == 0)
        legacy.append({**item, "reference_difference": reference, "third_reference_difference": candidate, "limits": limits, "accepted": accepted})
    repeats = json.loads((root/"paired-repeat-results.json").read_text())
    roles = ("reference_repeat", "local", "combined", "disabled_fault")
    paired = []; maxima = defaultdict(lambda: defaultdict(float)); legacy_rejections = 0
    for run in repeats:
        assert run["exit_code"] == 0 and not run["timed_out"]
        folder = root/"paired-repeat"/str(run["repeat"])
        groups = defaultdict(list)
        for row in csv_rows(folder/"coupling-replay.csv"):
            groups[row["role"]].append(row)
        summaries = {row["role"]: row for row in csv_rows(folder/"coupling-replay-summary.csv")}
        assert set(groups) == set(roles) and set(summaries) == set(roles)
        for role in roles:
            data = groups[role]; summary = summaries[role]
            assert [int(row["tick"]) for row in data] == list(range(90))
            assert all(row["queue_pending"] == "0" and row["queue_overflow"] == "0" for row in data)
            numeric = {key: [float(row[key]) for row in data] for key in ("mean", "center", "actor", "center_dx", "center_dy", "actor_dx", "actor_dy")}
            assert all(math.isfinite(value) for values in numeric.values() for value in values)
            values = {"mean_max": max(numeric["mean"]), "center_max": max(numeric["center"]), "actor_max": max(numeric["actor"]),
                      "mean_sum": sum(numeric["mean"]), "center_bias": math.hypot(sum(numeric["center_dx"]), sum(numeric["center_dy"])),
                      "actor_bias": math.hypot(sum(numeric["actor_dx"]), sum(numeric["actor_dy"]))}
            for key, value in values.items():
                assert math.isclose(value, float(summary[key]), rel_tol=1e-8, abs_tol=1e-9), (role, key, value, summary[key])
                maxima[role][key] = max(maxima[role][key], value)
            accepted = values["mean_max"] < .05 and values["mean_sum"] < .05 and values["center_max"] < .02 and values["center_bias"] < .02 and values["actor_max"] < .15 and values["actor_bias"] < .15
            assert accepted == (summary["accepted"] == "1") == (role != "disabled_fault")
            reactions = sum(int(row["reactions"]) for row in data)
            assert reactions == int(summary["reactions"])
            assert reactions > 0 if role in ("local", "combined") else reactions == 0
        log = (folder/"coupling-tests.txt").read_text()
        assert "paired_state_replay=passed" in log and "disabled_coupling_negative_control=rejected" in log
        legacy_flag = re.search(r"legacy90_pass=(\d)", log); assert legacy_flag
        legacy_rejections += legacy_flag[1] == "0"
        paired.append({**run, "checkpoint_cases": 90, "legacy90_diagnostic_accepted": legacy_flag[1] == "1"})
    result = {"schema_version": 1,
              "legacy_identical_reference": {"trials": len(legacy), "rejected": sum(not row["accepted"] for row in legacy), "rows": legacy},
              "paired": {"trials": len(paired), "checkpoint_cases": 90*len(paired), "comparisons": 360*len(paired),
                         "disabled_faults_rejected": len(paired), "legacy90_diagnostic_rejections": legacy_rejections, "maxima": dict(maxima), "rows": paired},
              "limits": {"mean": .05, "arithmetic_centroid": .02, "actor": .15},
              "scope": "Per-step and accumulated defects at identical authoritative inputs; not a bound on divergence of independent long trajectories. The legacy random-ratio gate is retained as a diagnostic/explicit CLI control."}
    (root/"coupling-qa-analysis.json").write_text(json.dumps(result, indent=2)+"\n")
    print(f"Identical reference: {result['legacy_identical_reference']['rejected']}/{len(legacy)} rejected by legacy rule.")
    print(f"Paired replay: {len(paired)} passed trials, {90*len(paired)} checkpoints, {len(paired)} real disabled-coupling faults rejected.")
    print(json.dumps(result["paired"]["maxima"], indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--input", required=True, type=Path)
    analyze(parser.parse_args().input)
