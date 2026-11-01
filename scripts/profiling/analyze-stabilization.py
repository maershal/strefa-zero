#!/usr/bin/env python3
"""Summarize native I04.5 runs without mixing runtime and validation.

The program uses only Python's standard library. It keeps per-run percentiles;
percentiles from different runs are never averaged into a fictitious sample.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path


FRAME_PHASES = (
    "input_ms", "metadata_ms", "scenario_ms", "ui_prepare_ms", "upload_ms",
    "poll_ms", "ui_build_ms", "update_ms", "world_render_ms", "gui_render_ms",
    "capture_ms", "validation_ms", "present_ms", "actions_ms", "sleep_ms", "other_ms",
)
FRAME_METRICS = (
    "frame_total_ms", "previous_interval_ms", *FRAME_PHASES,
    "mission_ticks_this_frame", "gpu_ticks_this_frame", "mission_pending_seconds",
    "mission_dropped_seconds", "refresh_hz", "focused", "minimized",
)
FIELDS = (
    "run", "phase", "variant", "mode", "effective_mode", "repeat",
    "phase_gating", "effective_phase_gating", "zero_projection", "effective_zero_projection",
    "storage", "effective_storage", "readback_period", "effective_readback_period",
    "neighbor_reads", "effective_neighbor_reads", "cost_mode", "effective_cost_mode",
    "run_valid", "exit_code", "timed_out",
    "scope", "metric", "samples", "available",
    "median", "p95", "p99", "max",
)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def number(row: dict[str, str], field: str) -> float | None:
    try:
        value = float(row.get(field, ""))
        return value if math.isfinite(value) else None
    except (TypeError, ValueError):
        return None


def describe(values: list[float]) -> dict[str, float | int | str]:
    if not values:
        return {"samples": 0, "available": 0, "median": "", "p95": "", "p99": "", "max": ""}
    ordered = sorted(values)

    def quantile(q: float) -> float:
        # Same nearest-rank/index convention as the project's C++ reports.
        return ordered[min(len(ordered) - 1, int(len(ordered) * q))]

    return {"samples": len(ordered), "available": 1, "median": quantile(.5),
            "p95": quantile(.95), "p99": quantile(.99), "max": ordered[-1]}


def key_values(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    return dict(line.split("=", 1) for line in path.read_text(encoding="utf-8-sig").splitlines() if "=" in line)


def is_true(value: object) -> bool:
    return str(value).strip().lower() in {"true", "1", "yes"}


def option_state(value: object) -> int | None:
    text = str(value).strip().lower()
    if text in {"on", "1", "true"}:
        return 1
    if text in {"off", "0", "false"}:
        return 0
    return None


def exit_code(value: object) -> int | None:
    try:
        return int(str(value).strip())
    except (ValueError, TypeError):
        return None


def analyze(input_root: Path, warmup_frames: int) -> tuple[list[dict], list[dict]]:
    rows: list[dict] = []
    statuses: list[dict] = []
    for run in read_csv(input_root / "runs.csv"):
        name = run["Name"]
        relative_directory = Path(run.get("ProfileDirectory", "") or name)
        directory = input_root / relative_directory
        if relative_directory.is_absolute() or not directory.resolve().is_relative_to(input_root.resolve()):
            raise ValueError(f"Run profile directory escapes its input root: {relative_directory}")
        identity = {"run": name, "phase": run.get("Phase", ""), "variant": run.get("Variant", ""),
                    "mode": run.get("Mode", ""), "repeat": run.get("Repeat", "")}
        status: dict = {**identity, "warnings": []}
        metadata_path = directory / "run.json"
        native: dict = {}
        if metadata_path.exists():
            native = json.loads(metadata_path.read_text(encoding="utf-8-sig"))
            status["native_run"] = native
        actual_exit = exit_code(native.get("exit_code", run.get("ExitCode")))
        timed_out = is_true(native.get("timed_out", run.get("TimedOut", "")))
        launch_error = native.get("launch_error", run.get("LaunchError", ""))
        valid = actual_exit == 0 and not timed_out and not launch_error
        if actual_exit != 0:
            status["warnings"].append(f"Native run exit code is {actual_exit!r}; retained measurements are invalid for successful-run comparisons.")
        if timed_out:
            status["warnings"].append("Native run timed out; retained measurements are partial and invalid for successful-run comparisons.")
        if launch_error:
            status["warnings"].append(f"Native launch/supervision error: {launch_error}")
        actual_modes: dict[str, str] = {}
        for metadata_name, mode_key in (("environment.txt", "gpu_timer_mode"),
                                        ("frame-timings-info.txt", "timer_mode"),
                                        ("mission-ui.txt", "gpu_timer_mode")):
            mode = key_values(directory / metadata_name).get(mode_key, "").strip()
            if mode:
                actual_modes[metadata_name] = mode
        known_modes = set(actual_modes.values())
        effective_mode = next(iter(known_modes)) if len(known_modes) == 1 else ("conflicting" if known_modes else "unknown")
        if any(mode != identity["mode"] for mode in known_modes):
            valid = False
            status["warnings"].append(f"Requested timer mode {identity['mode']!r} differs from effective mode metadata {actual_modes!r}; do not compare under the requested label.")
        if len(known_modes) > 1:
            valid = False
            status["warnings"].append("Effective timer mode metadata conflicts between files.")
        # New work variants carry explicit requests. Compare them to the
        # actual execution metadata, including phase-gating's linked fallback.
        for label, native_key, csv_key, metadata_keys in (
            ("phase_gating", "phase_gating", "PhaseGating", ("phase_gating_effective", "phase_gating")),
            ("zero_projection", "zero_projection", "ZeroProjection", ("skip_zero_projection",)),
        ):
            requested = option_state(native.get(native_key, run.get(csv_key, "")))
            actual_work: dict[str, int] = {}
            for metadata_name in ("environment.txt", "frame-timings-info.txt", "mission-ui.txt"):
                values = key_values(directory / metadata_name)
                for key in metadata_keys:
                    actual = option_state(values.get(key, ""))
                    if actual is not None:
                        actual_work[metadata_name] = actual
                        break
            known = set(actual_work.values())
            effective = next(iter(known)) if len(known) == 1 else ("conflicting" if known else "unknown")
            if requested is not None and (not known or known != {requested}):
                valid = False
                status["warnings"].append(
                    f"Requested {label}={requested} differs from effective execution metadata {actual_work!r}; work-variant comparison is invalid.")
            identity[label] = requested if requested is not None else ""
            identity[f"effective_{label}"] = effective
            status[f"effective_{label}_metadata"] = actual_work
        arguments = native.get("arguments", [])
        if not arguments and run.get("ArgumentsJson"):
            arguments = json.loads(run["ArgumentsJson"])
        for label, flag, csv_key, metadata_key in (
            ("cost_mode", "--gpu-cost-mode", "CostMode", "cost_mode"),
            ("neighbor_reads", "--gpu-neighbor-reads", "NeighborReads", "neighbor_reads_effective"),
            ("storage", "--gpu-storage", "Storage", "storage_effective"),
            ("readback_period", "--gpu-readback-period", "ReadbackPeriod", "readback_period"),
        ):
            requested = run.get(csv_key, "")
            if flag in arguments:
                index = arguments.index(flag)
                recorded = arguments[index + 1] if index + 1 < len(arguments) else "missing"
                if requested != "" and str(requested) != str(recorded):
                    valid = False
                    status["warnings"].append(f"CSV request {label}={requested!r} conflicts with recorded native arguments {recorded!r}.")
                requested = recorded
            actual = {}
            for metadata_name in ("environment.txt", "frame-timings-info.txt", "mission-ui.txt", "window-counters.txt"):
                value = key_values(directory / metadata_name).get(metadata_key, "").strip()
                if value:
                    actual[metadata_name] = value
            known = set(actual.values())
            effective = next(iter(known)) if len(known) == 1 else ("conflicting" if known else "unknown")
            allowed = {"staged", "immutable"} if label == "storage" and requested == "auto" else {str(requested)}
            if label == "neighbor_reads" and requested == "sorted":
                indices = {key_values(directory / name).get("neighbor_mode", "") for name in ("environment.txt", "window-counters.txt")}
                indices.discard("")
                if indices == {"0"}:
                    allowed = {"narrow"}  # linked lists have no sorted view
            if requested != "" and (not known or len(known) != 1 or not known.issubset(allowed)):
                valid = False
                status["warnings"].append(f"Requested {label}={requested!r} differs from effective metadata {actual!r}; transport comparison is invalid.")
            identity[label] = requested
            identity[f"effective_{label}"] = effective
            status[f"effective_{label}_metadata"] = actual
        status.update({"run_valid": int(valid), "exit_code": actual_exit,
                       "timed_out": int(timed_out), "effective_mode": effective_mode,
                       "effective_mode_metadata": actual_modes})
        identity.update({"run_valid": int(valid), "exit_code": actual_exit if actual_exit is not None else "",
                         "timed_out": int(timed_out), "effective_mode": effective_mode})
        status["validation_report"] = key_values(directory / "mission-ui.txt")
        benchmark = directory / "summary.csv"
        if benchmark.exists():
            for item in read_csv(benchmark):
                # Preserve the runner's distinction between absent GPU timers and 0 ms.
                available = item.get("available", "1")
                row = {**identity, "scope": f"throughput_{item.get('scene', 'unknown')}",
                       "metric": item.get("metric", ""), "samples": item.get("samples", ""),
                       "available": available}
                row.update({field: item.get(field, "") if available == "1" else ""
                            for field in ("median", "p95", "p99", "max")})
                rows.append(row)
        frame_path = directory / "frame-timings.csv"
        if frame_path.exists():
            frames = read_csv(frame_path)
            required = {"frame", "rendered", "frame_total_ms", "capture_performed", "capture_ms", "validation_ms", "present_ms"}
            if frames and not required.issubset(frames[0]):
                status["warnings"].append(f"Unsupported frame schema; missing {sorted(required - frames[0].keys())}")
            else:
                rendered = [item for item in frames if number(item, "rendered") == 1]
                after_warmup = [item for item in rendered
                                if (number(item, "frame") or 0) >= warmup_frames]
                # validation_ms is the ordinary per-frame glGetError check and is
                # part of runtime. Full mission readbacks happen after the loop.
                # A Boolean marker avoids mistaking timer overhead for a capture.
                runtime = [item for item in after_warmup if number(item, "capture_performed") != 1]
                status["frame_counts"] = {
                    "all_rows": len(frames), "rendered": len(rendered),
                    "after_warmup": len(after_warmup), "runtime_without_capture": len(runtime),
                    "excluded_capture": len(after_warmup) - len(runtime),
                    "minimized": sum(number(item, "minimized") == 1 for item in frames),
                    "focused_runtime": sum(number(item, "focused") == 1 for item in runtime),
                }
                # Include both scopes so expensive captures remain visible instead of
                # being silently hidden, while normal presentation remains comparable.
                for scope, chosen in (("frame_runtime", runtime), ("frame_with_capture", after_warmup)):
                    for field in FRAME_METRICS:
                        values = [value for item in chosen if (value := number(item, field)) is not None]
                        rows.append({**identity, "scope": scope, "metric": field, **describe(values)})
                phase_error = []
                for item in frames:
                    parts = [number(item, field) for field in FRAME_PHASES]
                    total = number(item, "frame_total_ms")
                    if total is not None and all(value is not None for value in parts):
                        phase_error.append(abs(total - sum(parts)))
                status["max_phase_accounting_error_ms"] = max(phase_error, default=None)
                if phase_error and max(phase_error) > .05:
                    status["warnings"].append("Frame phase sum differs from total by more than 0.05 ms.")
                dropped = [value for item in frames if (value := number(item, "mission_dropped_seconds")) is not None]
                status["max_mission_dropped_seconds"] = max(dropped, default=None)
                status["frame_clock_metadata"] = key_values(directory / "frame-timings-info.txt")
                if not runtime:
                    status["warnings"].append("No eligible runtime frames after warmup; no window performance result.")
        elif run.get("Phase") == "window":
            status["warnings"].append("No frame-timings.csv: detailed CPU collection may be disabled or the run incomplete.")
        statuses.append(status)
    return rows, statuses


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Directory containing native runs.csv")
    parser.add_argument("--warmup-frames", type=int, default=30)
    args = parser.parse_args()
    if args.warmup_frames < 0:
        parser.error("warmup frames must be nonnegative")
    rows, statuses = analyze(args.input, args.warmup_frames)
    with (args.input / "analysis.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    result = {"schema_version": 6, "warmup_frames": args.warmup_frames,
              "run_valid": "native exit 0, no timeout/launch error, and effective timer/work/transport/neighbor-read and diagnostic-mode option metadata agrees with available explicit requests; separate warnings still describe measurement quality",
              "percentiles": "sorted[min(n-1,floor(n*q))], kept separately for every run",
              "telemetry_scope": "nvidia-smi.csv covers the complete process, including final validation; use run timestamps and frame clock anchor to correlate",
              "validation_scope": "frame validation_ms is the routine glGetError check, retained in runtime; full mission readbacks are after the frame loop and retained in validation_report",
              "runs": statuses}
    (args.input / "analysis.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    warning_count = sum(len(item["warnings"]) for item in statuses)
    print(f"Analyzed {len(statuses)} native runs; {len(rows)} per-run metrics; {warning_count} warnings.")
    for status in statuses:
        for warning in status["warnings"]:
            print(f"WARNING [{status['run']}]: {warning}")
    print(args.input / "analysis.csv")
    print(args.input / "analysis.json")


if __name__ == "__main__":
    main()
