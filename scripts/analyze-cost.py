#!/usr/bin/env python3
"""Reconcile sparse dispatch timings, intermediate bounds and spatial histograms.

Run analyze-stabilization.py first. Instrumented variants are diagnostic probes;
only the off variant is the performance reference.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import csv
import json
import math
from pathlib import Path
import statistics


def rows(path: Path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        yield from csv.DictReader(stream)


def describe(values):
    values = sorted(values)
    if not values:
        return {"samples": 0, "available": False}
    return {"samples": len(values), "available": True, "mean": statistics.mean(values),
            "median": values[min(len(values)-1, int(len(values)*.5))],
            "p95": values[min(len(values)-1, int(len(values)*.95))],
            "p99": values[min(len(values)-1, int(len(values)*.99))], "max": values[-1]}


def analyze(root: Path):
    accepted = json.loads((root/"analysis.json").read_text(encoding="utf-8-sig"))["runs"]
    assert accepted and all(x["run_valid"] == 1 for x in accepted), "Review invalid native runs first"
    accepted_names = {x["run"] for x in accepted}
    scalar = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    pass_rows, bound_rows, captures, accounting = [], [], [], []
    combined = defaultdict(lambda: {"samples": [], "calls": [], "ticks": []})
    for run in rows(root/"runs.csv"):
        assert run["Name"] in accepted_names
        folder = root/run["ProfileDirectory"]
        assert folder.resolve().is_relative_to(root.resolve())
        mode = run["CostMode"]
        for metric in rows(folder/"summary.csv"):
            scalar[metric["scene"]][mode][metric["metric"]].append({key: float(metric[key]) for key in ("median", "p95", "p99", "max")})
        for validation in rows(folder/"validation.csv"):
            scene = validation["scene"]
            assert validation["finite"] == "1" and validation["steps"] == "360"
            sub = folder/scene
            groups = defaultdict(lambda: {"count": 0, "samples": []})
            per_tick, points, peak, event_count = {}, [], None, 0
            for event in rows(sub/"dispatches.csv"):
                event_count += 1
                key = (event["family"], int(event["operation"]))
                group = groups[key]; group["count"] += 1
                if event["sampled"] == "1":
                    value = float(event["milliseconds"])
                    assert math.isfinite(value) and value >= 0
                    group["samples"].append(value)
                if event["displacement"]:
                    value = float(event["displacement"])
                    assert math.isfinite(value) and value >= 0 and event["indexed"] == "1"
                    points.append(value)
                    tick = (int(event["generation"]), int(event["tick"]))
                    per_tick[tick] = max(per_tick.get(tick, 0), value)
                    if peak is None or value > float(peak["displacement"]):
                        peak = event
            assert event_count == int(validation["trace_events"])
            assert sum(len(x["samples"]) for x in groups.values()) == int(validation["timestamp_pairs"])
            assert len(points) == int(validation["header_copies"])
            accounting.append({"run": run["Name"], "mode": mode, **validation})
            if mode == "passes":
                for (family, operation), group in groups.items():
                    stats = describe(group["samples"])
                    calls = group["count"]/240
                    pass_rows.append({"run": run["Name"], "scene": scene, "family": family, "operation": operation,
                                      "calls_per_tick": calls, **stats,
                                      "estimated_ms_per_tick": stats["mean"]*calls if stats["available"] else ""})
                    item = combined[(scene, family, operation)]
                    item["samples"].extend(group["samples"]); item["calls"].append(calls)
                assert int(validation["timestamp_pairs"]) <= 240
            if points:
                bound_rows.append({"run": run["Name"], "mode": mode, "scene": scene,
                                   "checkpoints": describe(points), "tick_peaks": describe(per_tick.values()), "peak_event": peak})
            snapshot_rows = list(rows(sub/"snapshots.csv"))
            histograms = defaultdict(lambda: {"active": 0, "global": 0, "fresh": 0, "geometric": 0})
            for h in rows(sub/"snapshot-histograms.csv"):
                key = (h["snapshot"], h["reach"]); frequency = int(h["frequency"])
                item = histograms[key]; item["active"] += frequency
                item["global"] += frequency*int(h["global_candidates"])
                item["fresh"] += frequency*int(h["fresh_candidates"])
                item["geometric"] += frequency*int(h["geometric_neighbors"])
            assert len(snapshot_rows) == 2*int(validation["snapshots"])
            for shot in snapshot_rows:
                totals = histograms.get((shot["snapshot"], shot["reach"]), {"active": 0, "global": 0, "fresh": 0, "geometric": 0})
                assert totals["active"] == int(shot["active"])
                assert totals["global"] == int(shot["global_candidates"])
                assert totals["fresh"] == int(shot["fresh_candidates"])
                assert totals["geometric"] == int(shot["geometric_neighbors"])
                assert int(shot["missed"]) == 0
                captures.append({"run": run["Name"], "mode": mode, "scene": scene, **shot})
    performance = {}
    for scene, modes in scalar.items():
        performance[scene] = {}
        for mode, metrics in modes.items():
            performance[scene][mode] = {metric: {"runs": len(values),
                "median_of_medians": statistics.median(x["median"] for x in values),
                "median_of_p95": statistics.median(x["p95"] for x in values),
                "median_of_p99": statistics.median(x["p99"] for x in values),
                "median_range": [min(x["median"] for x in values), max(x["median"] for x in values)],
                "max": max(x["max"] for x in values)} for metric, values in metrics.items()}
    estimates = []
    for (scene, family, operation), item in combined.items():
        stats = describe(item["samples"])
        calls = statistics.mean(item["calls"])
        estimates.append({"scene": scene, "family": family, "operation": operation, "calls_per_tick": calls,
                          **stats, "estimated_ms_per_tick": stats["mean"]*calls if stats["available"] else None})
    with (root/"pass-estimates.csv").open("w", newline="", encoding="utf-8") as stream:
        fields = ["run", "scene", "family", "operation", "calls_per_tick", "samples", "available", "mean", "median", "p95", "p99", "max", "estimated_ms_per_tick"]
        writer = csv.DictWriter(stream, fieldnames=fields); writer.writeheader(); writer.writerows(pass_rows)
    result = {"schema_version": 1, "performance": performance, "pass_estimates": estimates,
              "estimator_limit": "Sparse deterministic samples of dispatch plus barrier, with timestamp perturbation. Mean duration times observed calls/tick is a diagnostic estimate, not marginal removable cost. Missing samples stay unavailable.",
              "bound_limit": "Checkpoint percentiles weight dispatch boundaries, not wall time. Per-tick peaks are separate; observations cover the recorded trajectories.",
              "snapshot_limit": "Captured current state and existing anchor index. Candidate counts are CPU reconstructions. Small fixtures use all-pairs; large scenes use an independent current-position CPU grid. Visibility/material filters are not counted.",
              "bounds": bound_rows, "snapshots": captures, "accounting": accounting}
    (root/"cost-analysis.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(f"Reconciled {len(accepted)} native runs, {len(bound_rows)} bound series, {len(captures)} spatial rows; zero missing geometric pairs.")
    for scene in performance:
        best = sorted((x for x in estimates if x["scene"] == scene and x["available"]), key=lambda x: x["estimated_ms_per_tick"], reverse=True)[:5]
        print(scene, "off GPU", performance[scene].get("off", {}).get("gpu_step_ms", {}).get("median_of_medians"), "top estimates", [(x["family"], x["operation"], round(x["estimated_ms_per_tick"], 5), x["samples"]) for x in best])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--input", required=True, type=Path)
    analyze(parser.parse_args().input)
