"""Analyze StrefaQA --gpu-check iteration-sweep (scripts/profiling/profile-iteration-sweep.ps1).

Inputs in the run folder: iteration-sweep-timing.csv (one row per segment: a fresh world, 240 measured ticks with a
GL_TIMESTAMP pair around step(), configurations in a random order per block), optionally iteration-sweep-gates.csv
(PASS/FAIL per configuration and gate; --gates takes one or more such files from separate runs and counts repeats)
and telemetry.csv. A segment is qualified
when telemetry shows no hw_* or sw_thermal_slowdown reason inside it, its median graphics clock is at least
--clock-fraction of the session maximum and the GPU calibration probes before and after are within --probe-tolerance
of the fastest of the run. Without telemetry.csv only the probes qualify. sw_power_cap is reported, not disqualifying.
The saving of a configuration is the median over blocks of (its segment median - the 8/12 segment median of the same
block, same scene), counting only blocks where both qualify. Writes analysis.json and summary.md (at most 10 rows).
"""
import argparse, csv, json, math, statistics, sys

sys.stdout.reconfigure(encoding='utf-8')
from collections import defaultdict
from pathlib import Path

DISQUALIFYING_REASONS = ('hw_slowdown', 'hw_thermal_slowdown', 'hw_power_brake_slowdown', 'sw_thermal_slowdown')
GATES = ('fluid_quality', 'liquids', 'granular', 'flow', 'coupling', 'mission')
BASELINE = '8/12'


def rows(path):
    return list(csv.DictReader(path.open(encoding='utf-8-sig'))) if path and path.exists() else []


def fmt(value, digits=3, sign=False):
    if value is None or (isinstance(value, float) and not math.isfinite(value)):
        return '–'
    return (f'{value:+.{digits}f}' if sign else f'{value:.{digits}f}').replace('.', ',')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', type=Path)
    parser.add_argument('--gates', type=Path, nargs='+', help='iteration-sweep-gates.csv files of separate runs')
    parser.add_argument('--clock-fraction', type=float, default=0.9)
    parser.add_argument('--probe-tolerance', type=float, default=0.15)
    args = parser.parse_args()
    segments = rows(args.folder / 'iteration-sweep-timing.csv')
    if not segments:
        sys.exit('Brak iteration-sweep-timing.csv')
    missing = [str(path) for path in args.gates or [] if not path.is_file()]
    if missing:
        sys.exit('Brak pliku bramek (PowerShell nie rozwija * w argumentach): ' + ', '.join(missing))
    gates = [g for path in (args.gates or [args.folder / 'iteration-sweep-gates.csv']) for g in rows(path)]
    telemetry = [r for r in rows(args.folder / 'telemetry.csv') if r.get('utc_unix_ns')]
    clocks = [float(r['graphics_mhz']) for r in telemetry if r.get('graphics_mhz')]
    session_max = max(clocks) if clocks else None
    fastest_gpu = min(float(s[f'{w}_gpu_ms']) for s in segments for w in ('before', 'after'))
    power_cap = 0
    for s in segments:
        s['config'] = f"{s['pbf']}/{s['contact']}"
        reasons = set()
        start, end = int(s['start_utc_unix_ns']), int(s['end_utc_unix_ns'])
        inside = [r for r in telemetry if start <= int(r['utc_unix_ns']) <= end]
        reasons |= {n for r in inside for n in DISQUALIFYING_REASONS if r.get('reason_' + n) == '1'}
        segment_clocks = [float(r['graphics_mhz']) for r in inside if r.get('graphics_mhz')]
        clock = statistics.median(segment_clocks) if segment_clocks else None
        if session_max and clock is not None and clock < args.clock_fraction * session_max:
            reasons.add('graphics_clock_below_session_max')
        if telemetry and not inside:
            reasons.add('no_telemetry')
        for when in ('before', 'after'):
            if float(s[f'{when}_gpu_ms']) / fastest_gpu - 1 > args.probe_tolerance:
                reasons.add(f'probe_gpu_{when}')
        power_cap += any(r.get('reason_sw_power_cap') == '1' for r in inside)
        s['reasons'] = sorted(reasons)
    configs = list(dict.fromkeys(s['config'] for s in sorted(segments, key=lambda s: (-int(s['pbf']), -int(s['contact'])))))
    scenes = list(dict.fromkeys(s['scene'] for s in segments))
    by_key = defaultdict(dict)
    for s in segments:
        by_key[(s['config'], s['scene'])][int(s['block'])] = s
    per_config = {}
    for config in configs:
        entry = dict(gates={}, scenes={})
        for g in gates:
            if f"{g['pbf']}/{g['contact']}" == config:
                gate = entry['gates'].setdefault(g['gate'], dict(runs=0, passes=0, messages=[]))
                gate['runs'] += 1
                gate['passes'] += g['result'] == 'PASS'
                if g['result'] != 'PASS' and g.get('message') and g['message'] not in gate['messages']:
                    gate['messages'].append(g['message'])
        entry['all_gates_pass'] = len(entry['gates']) == len(GATES) and all(
            v['passes'] == v['runs'] for v in entry['gates'].values())
        for scene in scenes:
            mine = by_key[(config, scene)]
            base = by_key[(BASELINE, scene)]
            ok = [s for s in mine.values() if not s['reasons']]
            deltas = [float(s['gpu_median_ms']) - float(base[b]['gpu_median_ms'])
                      for b, s in mine.items() if not s['reasons'] and b in base and not base[b]['reasons']]
            entry['scenes'][scene] = dict(
                segments=len(mine), qualified=len(ok),
                gpu_median_ms=statistics.median(float(s['gpu_median_ms']) for s in ok) if ok else None,
                delta_vs_baseline_ms=statistics.median(deltas) if deltas else None,
                delta_min_ms=min(deltas, default=None), delta_max_ms=max(deltas, default=None),
                dispatches_per_tick=statistics.median(float(s['dispatches_per_tick']) for s in mine.values()),
                barriers_per_tick=statistics.median(float(s['barriers_per_tick']) for s in mine.values()))
        per_config[config] = entry
    passing = [c for c in configs if per_config[c]['all_gates_pass']]
    cheapest = min(passing, key=lambda c: per_config[c]['scenes'].get('mission', {}).get('dispatches_per_tick', 1e9),
                   default=None)
    disqualified = [dict(sequence=s['sequence'], config=s['config'], scene=s['scene'], reasons=s['reasons'])
                    for s in segments if s['reasons']]
    result = dict(session_max_mhz=session_max, telemetry_samples=len(telemetry), fastest_gpu_probe_ms=fastest_gpu,
                  segments=len(segments), disqualified=disqualified, segments_with_power_cap=power_cap,
                  cheapest_passing=cheapest, configs=per_config)
    (args.folder / 'analysis.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    lines = ['| PBF/kontakt | ' + ' | '.join(GATES) + ' | Dispatche/tick misji | Misja [ms/tick] | Δ misji [ms] | '
             'Tama [ms/tick] | Δ tamy [ms] |',
             '| --- | ' + ' | '.join('---' for _ in GATES) + ' | ---: | ---: | ---: | ---: | ---: |']
    for config in configs:
        e = per_config[config]
        cells = []
        for g in GATES:
            v = e['gates'].get(g)
            cells.append('–' if not v else ('PASS' if v['passes'] == v['runs'] else 'FAIL')
                         + (f" {v['passes']}/{v['runs']}" if v['runs'] > 1 else ''))
        m, d = e['scenes'].get('mission', {}), e['scenes'].get('dam-break', {})
        lines.append(f"| {config} | " + ' | '.join(cells) + f" | {fmt(m.get('dispatches_per_tick'), 0)} | "
                     f"{fmt(m.get('gpu_median_ms'))} | {fmt(m.get('delta_vs_baseline_ms'), 3, True)} | "
                     f"{fmt(d.get('gpu_median_ms'))} | {fmt(d.get('delta_vs_baseline_ms'), 3, True)} |")
    lines.append('')
    lines.append(f'Odcinki zdyskwalifikowane: {len(disqualified)} z {len(segments)}; z sw_power_cap: {power_cap}; '
                 f'maksimum zegara sesji: {session_max} MHz; najtańsza konfiguracja ze wszystkimi bramkami: {cheapest}.')
    (args.folder / 'summary.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
