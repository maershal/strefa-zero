"""Analyze StrefaQA --gpu-check thermal-gate (scripts/profiling/profile-thermal-gate.ps1).

Each block measures every scene as an ABBA quad: gate off, on, on, off, each segment a fresh world with 240 measured
ticks (GL_TIMESTAMP pair around step(), i.e. the whole physics tick with thermal). A segment is qualified when
telemetry.csv shows no hw_* or sw_thermal_slowdown reason inside it, its median graphics clock is at least
--clock-fraction of the session maximum (the GTX 1650 laptop's slow regime has no throttle reason) and the GPU
calibration probes before and after are within --probe-tolerance of the fastest of the run. Without telemetry.csv
only the probes qualify. sw_power_cap is reported, not disqualifying. A quad counts only when all four segments
qualify; its effect is mean(on) - mean(off) of the segment medians, so a linear drift within the quad cancels.
Writes analysis.json and summary.md (tables of at most 10 rows) into the run folder.
"""
import argparse, csv, json, math, statistics, sys
from collections import defaultdict
from pathlib import Path

DISQUALIFYING_REASONS = ('hw_slowdown', 'hw_thermal_slowdown', 'hw_power_brake_slowdown', 'sw_thermal_slowdown')
SCENES = ('cold', 'idle', 'fire', 'quench')


def rows(path):
    return list(csv.DictReader(path.open(encoding='utf-8-sig'))) if path.exists() else []


def fmt(value, digits=3):
    if value is None or (isinstance(value, float) and not math.isfinite(value)):
        return '–'
    return f'{value:.{digits}f}'.replace('.', ',')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', type=Path)
    parser.add_argument('--clock-fraction', type=float, default=0.9)
    parser.add_argument('--probe-tolerance', type=float, default=0.15)
    args = parser.parse_args()
    segments = rows(args.folder / 'thermal-gate-segments.csv')
    if not segments:
        sys.exit('Brak thermal-gate-segments.csv')
    telemetry = [r for r in rows(args.folder / 'telemetry.csv') if r.get('utc_unix_ns')]
    clocks = [float(r['graphics_mhz']) for r in telemetry if r.get('graphics_mhz')]
    session_max = max(clocks) if clocks else None
    fastest_gpu = min(float(s[f'{w}_gpu_ms']) for s in segments for w in ('before', 'after'))
    power_cap = 0
    for s in segments:
        reasons = set()
        start, end = int(s['start_utc_unix_ns']), int(s['end_utc_unix_ns'])
        inside = [r for r in telemetry if start <= int(r['utc_unix_ns']) <= end]
        reasons |= {n for r in inside for n in DISQUALIFYING_REASONS if r.get('reason_' + n) == '1'}
        segment_clocks = [float(r['graphics_mhz']) for r in inside if r.get('graphics_mhz')]
        s['clock_mhz'] = statistics.median(segment_clocks) if segment_clocks else None
        if session_max and s['clock_mhz'] is not None and s['clock_mhz'] < args.clock_fraction * session_max:
            reasons.add('graphics_clock_below_session_max')
        if telemetry and not inside:
            reasons.add('no_telemetry')
        for when in ('before', 'after'):
            if float(s[f'{when}_gpu_ms']) / fastest_gpu - 1 > args.probe_tolerance:
                reasons.add(f'probe_gpu_{when}')
        power_cap += any(r.get('reason_sw_power_cap') == '1' for r in inside)
        s['reasons'] = sorted(reasons)
    quads = defaultdict(dict)
    for s in segments:
        quads[(s['scene'], int(s['block']))][int(s['order'])] = s
    per_scene = {}
    for scene in SCENES:
        effects, off_values, on_values, skipped, gated, total = [], [], [], 0, 0, 0
        for (name, block), quad in sorted(quads.items()):
            if name != scene or len(quad) != 4:
                continue
            total += 1
            if any(q['reasons'] for q in quad.values()):
                continue
            off = [float(quad[k]['gpu_median_ms']) for k in (0, 3)]
            on = [float(quad[k]['gpu_median_ms']) for k in (1, 2)]
            effects.append(statistics.mean(on) - statistics.mean(off))
            off_values += off
            on_values += on
            skipped += sum(int(quad[k]['skipped_ticks']) for k in (1, 2))
            gated += sum(int(quad[k]['gated_ticks']) for k in (1, 2))
        off_median = statistics.median(off_values) if off_values else None
        per_scene[scene] = dict(
            quads=total, qualified_quads=len(effects), off_median_ms=off_median,
            on_median_ms=statistics.median(on_values) if on_values else None,
            effect_median_ms=statistics.median(effects) if effects else None,
            effect_min_ms=min(effects, default=None), effect_max_ms=max(effects, default=None),
            effect_percent=(100 * statistics.median(effects) / off_median) if effects and off_median else None,
            skipped_fraction=skipped / gated if gated else None)
    disqualified = [dict(sequence=s['sequence'], scene=s['scene'], variant=s['variant'], reasons=s['reasons'])
                    for s in segments if s['reasons']]
    result = dict(session_max_mhz=session_max, telemetry_samples=len(telemetry), fastest_gpu_probe_ms=fastest_gpu,
                  segments=len(segments), disqualified=disqualified, segments_with_power_cap=power_cap,
                  scenes=per_scene)
    (args.folder / 'analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    lines = ['| Scena | Kwadraty ABBA (zakwalifikowane) | Tick bez bramki [ms] | Tick z bramką [ms] | Efekt [ms] | '
             'Efekt [%] | Zakres efektu [ms] | Pominięte ticki gazu |',
             '| --- | --- | --- | --- | --- | --- | --- | --- |']
    for scene, r in per_scene.items():
        lines.append(f"| {scene} | {r['qualified_quads']}/{r['quads']} | {fmt(r['off_median_ms'])} | "
                     f"{fmt(r['on_median_ms'])} | {fmt(r['effect_median_ms'])} | {fmt(r['effect_percent'], 1)} | "
                     f"{fmt(r['effect_min_ms'])}…{fmt(r['effect_max_ms'])} | "
                     f"{fmt(100 * r['skipped_fraction'], 0) + '%' if r['skipped_fraction'] is not None else '–'} |")
    lines.append('')
    lines.append(f'Odcinki zdyskwalifikowane: {len(disqualified)} z {len(segments)}; z sw_power_cap: {power_cap}; '
                 f'maksimum zegara sesji: {session_max} MHz.')
    (args.folder / 'summary.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
