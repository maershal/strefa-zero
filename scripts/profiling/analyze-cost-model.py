"""Analyze a Z01 run of StrefaQA --gpu-check cost-model (scripts/profiling/profile-cost-model.ps1).

Every segment (one synthetic series or one sweep point, bracketed by calibration probes) is qualified twice:
- GPU: no hw_* or sw_thermal_slowdown reason inside the segment (telemetry.csv); for a sweep point, a median
  graphics clock over its measured ticks of at least --clock-fraction of the session maximum (the GTX 1650 laptop's
  slow regime has no throttle reason); GPU probe before and after within --probe-tolerance (15%) of the fastest GPU
  probe of the run. sw_power_cap is reported, not disqualifying.
- CPU: CPU probe before and after within --probe-tolerance of the fastest CPU probe of the run. The GTX 1650 laptop
  slows its CPU up to ~5x under sustained GPU load with no GPU throttle reason; only the probes see it.
GPU timings (GL_TIMESTAMP spans, GPU tick timers) use GPU-qualified segments; timings that include CPU work (submit,
wall clock) need both. Segments that pass the GPU test but fail the CPU test give a natural experiment: if the GPU
tick does not change when the CPU is slower, the driver's delivery of passes is not what limits the tick.
Writes analysis.json and summary.md (Markdown tables of at most 10 rows for the report) into the run folder.
"""
import argparse, csv, json, math, statistics, sys
from collections import defaultdict
from pathlib import Path

DISQUALIFYING_REASONS = ('hw_slowdown', 'hw_thermal_slowdown', 'hw_power_brake_slowdown', 'sw_thermal_slowdown')


def rows(path):
    return list(csv.DictReader(path.open(encoding='utf-8-sig'))) if path.exists() else []


def fit(points):
    """Least squares y = a + b x; returns (a, b, r2)."""
    n = len(points)
    if n < 2:
        return math.nan, math.nan, math.nan
    sx = sum(x for x, _ in points); sy = sum(y for _, y in points)
    sxx = sum(x * x for x, _ in points); sxy = sum(x * y for x, y in points)
    d = n * sxx - sx * sx
    b = (n * sxy - sx * sy) / d if d else 0.0
    a = (sy - b * sx) / n
    mean = sy / n
    total = sum((y - mean) ** 2 for _, y in points)
    residual = sum((y - a - b * x) ** 2 for x, y in points)
    return a, b, (1 - residual / total) if total else 1.0


def fmt(value, digits=3):
    if value is None or (isinstance(value, float) and not math.isfinite(value)):
        return '–'
    return f'{value:.{digits}f}'.replace('.', ',')


def table(header, body):
    if len(body) > 10:
        raise ValueError('Tabele raportu mają najwyżej 10 wierszy')
    lines = ['| ' + ' | '.join(header) + ' |', '| ' + ' | '.join('---' for _ in header) + ' |']
    lines += ['| ' + ' | '.join(str(c) for c in row) + ' |' for row in body]
    return '\n'.join(lines)


def qualify(folder, segments, sweep, clock_fraction, probe_tolerance):
    telemetry = [r for r in rows(folder / 'telemetry.csv') if r.get('utc_unix_ns')]
    clocks = [float(r['graphics_mhz']) for r in telemetry if r.get('graphics_mhz')]
    session_max = max(clocks) if clocks else None
    measured = {r['sequence']: (int(r['measured_start_utc_unix_ns']), int(r['measured_end_utc_unix_ns']))
                for r in sweep if r.get('measured_start_utc_unix_ns')}
    # Reference = fastest probe of the run (the cool machine), not the median: a run that spends most of its time
    # throttled would otherwise make the throttled state the reference.
    probe = {k: min(float(s[f'{w}_{k}']) for s in segments for w in ('before', 'after'))
             for k in ('cpu_ms', 'gpu_ms')}
    result = {}
    power_cap = 0
    for s in segments:
        gpu, cpu = set(), set()
        start, end = int(s['start_utc_unix_ns']), int(s['end_utc_unix_ns'])
        inside = [r for r in telemetry if start <= int(r['utc_unix_ns']) <= end]
        gpu |= {n for r in inside for n in DISQUALIFYING_REASONS if r.get('reason_' + n) == '1'}
        window = measured.get(s['sequence'], (start, end))  # older runs: whole segment, incl. world creation
        ticks = [r for r in inside if window[0] <= int(r['utc_unix_ns']) <= window[1]]
        tick_clocks = [float(r['graphics_mhz']) for r in ticks if r.get('graphics_mhz')]
        median_clock = statistics.median(tick_clocks) if tick_clocks else None
        if s['part'] == 'sweep' and median_clock is not None and median_clock < clock_fraction * session_max:
            gpu.add('graphics_clock_below_session_max')
        if any(r.get('reason_sw_power_cap') == '1' for r in inside):
            power_cap += 1
        for when in ('before', 'after'):
            if float(s[f'{when}_gpu_ms']) / probe['gpu_ms'] - 1 > probe_tolerance:
                gpu.add(f'probe_gpu_{when}')
            if float(s[f'{when}_cpu_ms']) / probe['cpu_ms'] - 1 > probe_tolerance:
                cpu.add(f'probe_cpu_{when}')
        temps = [float(r['temperature_c']) for r in inside if r.get('temperature_c')]
        result[s['sequence']] = dict(part=s['part'], label=s['label'], block=int(s['block']), gpu=sorted(gpu),
                                     cpu=sorted(cpu), graphics_mhz_median=median_clock,
                                     temperature_c_max=max(temps, default=None), telemetry_samples=len(inside))
    return result, dict(telemetry_samples=len(telemetry), session_max_mhz=session_max, fastest_probes=probe,
                        segments_with_power_cap=power_cap)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', type=Path)
    parser.add_argument('--clock-fraction', type=float, default=.90)
    parser.add_argument('--probe-tolerance', type=float, default=.15)
    args = parser.parse_args()
    folder = args.folder
    info = dict(line.split('=', 1) for line in (folder / 'cost-model.txt').read_text(encoding='utf-8').splitlines()
                if '=' in line)
    if info.get('status') != 'ok':
        raise ValueError('cost-model.txt: przebieg się nie zakończył')
    segments = rows(folder / 'segments.csv')
    series = rows(folder / 'series.csv')
    sweep = rows(folder / 'sweep.csv')
    if len({r['seed'] for r in segments + series + sweep}) != 1:
        raise ValueError('Pliki mają różne ziarna losowania')
    quality, session = qualify(folder, segments, sweep, args.clock_fraction, args.probe_tolerance)
    gpu_bad = {k for k, q in quality.items() if q['gpu']}
    cpu_bad = {k for k, q in quality.items() if q['cpu']}
    md = [f"Plan `{info['plan']}`, renderer `{info['renderer']}`, ziarno {info['seed']}, "
          f"przebieg wstępny L = {info['prefix_chain']}."]
    reasons = defaultdict(int)
    for q in quality.values():
        for r in q['gpu'] + q['cpu']:
            reasons[r] += 1
    md.append(f"Odcinki: {len(segments)}; zakwalifikowane dla GPU {len(segments) - len(gpu_bad)}, dla GPU i CPU "
              f"{len(segments) - len(gpu_bad | cpu_bad)} (powody: {dict(reasons) or 'brak'}); odcinki z "
              f"sw_power_cap: {session['segments_with_power_cap']}; próbki telemetrii: {session['telemetry_samples']}; "
              f"maks. zegar sesji: {session['session_max_mhz']} MHz; czekanie na schłodzenie: "
              f"{sum(int(s.get('cooldown_s') or 0) for s in segments)} s w "
              f"{sum(1 for s in segments if int(s.get('cooldown_s') or 0) > 0)} odcinkach.")

    # Part 1: GPU spans from GPU-qualified segments, submit and wall time from fully qualified ones.
    groups = defaultdict(lambda: defaultdict(list))
    unclean = 0
    for r in series:
        if r['mode'] == 'primed' and r['primed_clean'] != '1':
            unclean += 1
            continue
        key = (r['family'], r['kind'], r['mode'], int(r['logical_threads']), int(r['logical_chain']),
               int(r['logical_passes']))
        if r['sequence'] not in gpu_bad:
            groups[key]['gpu_ms'].append(float(r['gpu_ms']))
            if r['sequence'] not in cpu_bad:
                groups[key]['submit_ms'].append(float(r['submit_ms']))
                groups[key]['wall_ms'].append(float(r['wall_ms']))
    med = {k: {f: statistics.median(v) for f, v in g.items() if v} for k, g in groups.items()}
    fits = {}
    for field in ('gpu_ms', 'submit_ms', 'wall_ms'):
        points = defaultdict(list)
        for (family, kind, mode, t, l, m), v in med.items():
            if family == 'series' and field in v:
                points[(kind, mode, t, l)].append((m, v[field]))
        for key, p in points.items():
            a, b, r2 = fit(p)
            fits.setdefault(key, {})[field] = dict(fixed_ms=a, ms_per_pass=b, r2=r2, points=len(p))
    md.append(f'Odrzucone próbki primed (zlecenie trwało dłużej niż przebieg wstępny): {unclean}.')

    def pp(kind, mode, t, l, field='gpu_ms'):
        f = fits.get((kind, mode, t, l), {}).get(field)
        return f['ms_per_pass'] * 1000 if f and f['points'] >= 2 else None  # microseconds

    body = []
    for name, kind, t, l in (('pusty + bariera', 'empty', 64, 0), ('indirect, 0 grup', 'indirect_zero', 64, 0),
                             ('łańcuch T=256 L=16', 'chain', 256, 16), ('łańcuch T=256 L=256', 'chain', 256, 256),
                             ('łańcuch T=4096 L=16', 'chain', 4096, 16), ('łańcuch T=32768 L=16', 'chain', 32768, 16)):
        body.append([name, fmt(pp(kind, 'cold', t, l), 1), fmt(pp(kind, 'primed', t, l), 1),
                     fmt(pp(kind, 'cold', t, l, 'wall_ms'), 1), fmt(pp(kind, 'finish_each', t, l, 'wall_ms'), 1),
                     fmt(pp(kind, 'cold', t, l, 'submit_ms'), 2)])
    md.append('### Koszt jednego przebiegu (µs, nachylenie a + b·M po M)\n\n' + table(
        ['Przebieg', 'GPU cold', 'GPU primed', 'ścienny cold', 'ścienny finish_each', 'CPU zlecenie'], body))

    body = []
    for t in (256, 4096, 32768):
        for l in (16, 256):
            primed = pp('chain', 'primed', t, l)
            if primed is not None:
                body.append([t, l, t * l, fmt(primed, 1), fmt(primed / l, 3), fmt(primed / (t * l) * 1e3, 3)])
    md.append('### Łańcuch: koszt przebiegu wobec L i T·L (GPU primed)\n\n' + table(
        ['T', 'L', 'T·L', 'µs/przebieg', 'µs na odczyt łańcucha (L)', 'ns na odczyt (T·L)'], body))

    body = []
    for t, l in ((256, 16), (4096, 16), (32768, 16), (256, 256), (4096, 256), (32768, 256)):
        m = 64
        s = med.get(('series', 'chain', 'cold', t, l, m), {})
        c = med.get(('fused_chain', 'chain', 'cold', t, l, m), {})
        w = med.get(('fused_wide', 'chain', 'cold', t, l, m), {})
        if 'gpu_ms' in s:
            body.append([t, l, m, fmt(s['gpu_ms']), fmt(c.get('gpu_ms')), fmt(w.get('gpu_ms'))])
    md.append('### Ta sama praca: M przebiegów wobec jednego (GPU ms)\n\n' + table(
        ['T', 'L', 'M', 'M przebiegów', '1 przebieg, L·M na wątek', '1 przebieg, T·M wątków'], body))

    sweep_fits = {}
    throttle = []
    if sweep:
        by = defaultdict(lambda: defaultdict(list))
        for r in sweep:
            if r['sequence'] in gpu_bad:
                continue
            key = (r['material'], int(r['n']))
            by[key]['gpu_tick_median_ms'].append(float(r['gpu_tick_median_ms']))
            by[key]['dispatches_per_tick'].append(float(r['dispatches_per_tick']))
            if r['sequence'] in cpu_bad:
                by[key]['gpu_tick_cpu_slow'].append(float(r['gpu_tick_median_ms']))
            else:
                by[key]['gpu_tick_cpu_ok'].append(float(r['gpu_tick_median_ms']))
                by[key]['wall_ms_per_tick'].append(float(r['wall_ms_per_tick']))
                by[key]['cpu_submit_median_ms'].append(float(r['cpu_submit_median_ms']))
        counts = sorted({n for _, n in by})

        def med_of(v, f, d=3):
            return fmt(statistics.median(v[f]), d) if v and v.get(f) else '–'

        body = []
        for n in counts:
            row = [n]
            for material in ('grains', 'water'):
                v = by.get((material, n))
                row += [med_of(v, 'gpu_tick_median_ms'), med_of(v, 'wall_ms_per_tick'),
                        med_of(v, 'dispatches_per_tick', 0)]
            body.append(row)
        md.append('### Sweep: ms na tick (mediana bloków)\n\n' + table(
            ['N', 'ziarna GPU', 'ziarna ścienny', 'ziarna dispatche', 'woda GPU', 'woda ścienny', 'woda dispatche'],
            body))
        # Same definitions as sweep-fit.csv of StrefaQA (src/gpu/cost_model.cpp): a fit over every N ("all") and
        # over N >= N_max / 4 ("upper", the linear regime); crossover = (cost at the smallest N - a) / b, the N where
        # the fitted line meets the flat level, i.e. from which N the tick is throughput-bound.
        body = []
        upper_from = max(counts) / 4 if counts else 0
        names = {'gpu_tick_median_ms': 'GPU', 'wall_ms_per_tick': 'ścienny'}
        for material in ('grains', 'water'):
            for metric in ('gpu_tick_median_ms', 'wall_ms_per_tick'):
                pts = [(n, statistics.median(by[(material, n)][metric])) for n in counts
                       if by[(material, n)].get(metric)]
                if not pts:
                    continue
                flat = pts[0][1]
                for rng, chosen in (('all', pts), ('upper', [(n, y) for n, y in pts if n >= upper_from])):
                    if len(chosen) < 2:
                        continue
                    a, b, r2 = fit(chosen)
                    crossover = (flat - a) / b if b > 0 else None
                    sweep_fits[f'{material}:{metric}:{rng}'] = dict(
                        a_ms=a, b_ms_per_1000=b * 1000, r2=r2, flat_ms=flat, crossover_n=crossover,
                        points=len(chosen), extrapolated_64k_ms=a + b * 65536, extrapolated_128k_ms=a + b * 131072)
                    body.append([{'grains': 'ziarna', 'water': 'woda'}[material], names[metric],
                                 f"{chosen[0][0]}–{chosen[-1][0]} ({len(chosen)})", fmt(a), fmt(b * 1000, 4), fmt(r2),
                                 fmt(crossover, 0), fmt(a + b * 65536), fmt(a + b * 131072)])
        md.append('### Dopasowanie a + b·N i ekstrapolacja (64k i 128k to ekstrapolacja poza pomiarem)\n\n'
                  'Próg: N, przy którym prosta przecina koszt przy najmniejszym N.\n\n' + table(
            ['Materiał', 'Miara', 'zakres N (punkty)', 'a [ms]', 'b [ms/1000]', 'R²', 'próg N', '64k [ms]',
             '128k [ms]'], body))
        for (material, n), v in sorted(by.items()):
            if v.get('gpu_tick_cpu_slow') and v.get('gpu_tick_cpu_ok'):
                throttle.append(statistics.median(v['gpu_tick_cpu_slow']) / statistics.median(v['gpu_tick_cpu_ok']))
        if throttle:
            md.append(f'Tick GPU przy wolnej wobec szybkiej sondzie CPU, ten sam punkt ({len(throttle)} punktów): '
                      f'mediana stosunku {fmt(statistics.median(throttle))}, zakres '
                      f'{fmt(min(throttle))}–{fmt(max(throttle))}.')

    cpu = [float(s[f'{w}_cpu_ms']) for s in segments for w in ('before', 'after')]
    gpu = [float(s[f'{w}_gpu_ms']) for s in segments for w in ('before', 'after')]
    md.append(f'Sondy: CPU mediana {fmt(statistics.median(cpu))} ms (min {fmt(min(cpu))}, maks. {fmt(max(cpu))}); '
              f'GPU mediana {fmt(statistics.median(gpu))} ms (min {fmt(min(gpu))}, maks. {fmt(max(gpu))}).')
    (folder / 'summary.md').write_text('\n\n'.join(md) + '\n', encoding='utf-8')
    json.dump(dict(info=info, session=session, gpu_disqualified=sorted(gpu_bad, key=int),
                   cpu_disqualified=sorted(cpu_bad, key=int), reasons=dict(reasons),
                   series_fits={'|'.join(map(str, k)): v for k, v in fits.items()}, sweep_fits=sweep_fits,
                   gpu_tick_ratio_cpu_slow_to_ok=throttle, quality=quality, primed_unclean=unclean),
              (folder / 'analysis.json').open('w', encoding='utf-8'), indent=1)
    sys.stdout.reconfigure(encoding='utf-8')
    print('\n\n'.join(md))


if __name__ == '__main__':
    main()
