#!/usr/bin/env python3
"""Validate a frozen G1 variance protocol; keep diagnostic probes separate."""
from __future__ import annotations
import argparse
from collections import defaultdict
import csv
from datetime import datetime
import json
import math
from pathlib import Path
import re
from statistics import median

G1 = '1a1f53457ca590187417818f24d42bbea93d11c7b29bc5e7d8fea7320f918d5e'
G2 = '333287c4063fa722eb4314fd70c0f6509cc92479c7890ee1da772bf1b0443af8'
SCENES = {'mission': 3096, 'stress_16k': 16000, 'stress_32k': 32768, 'mission_blast': 3096}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def obj(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def values(path):
    return dict(line.split('=', 1) for line in path.read_text(encoding='utf-8-sig').splitlines() if '=' in line)


def number(value):
    # PowerShell Export-Csv formats numeric objects using the host culture.
    result = float(value.replace(',', '.'))
    require(math.isfinite(result), 'Nonfinite numeric sample')
    return result


def dump(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')


def write_csv(path, data):
    if data:
        with path.open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(data[0]))
            writer.writeheader(); writer.writerows(data)


def analyze(root):
    environment = obj(root/'environment.json')
    protocol = environment['protocol']
    require(environment['baseline_sha256'] == G1, 'Baseline hash mismatch')
    require(protocol in ('controls', 'reproduction'), 'Unknown protocol')
    schedule, runs = rows(root/'schedule.csv'), rows(root/'runs.csv')
    expected_names = (['observer-off-1', 'observer-on-1', 'observer-on-2', 'observer-off-2'] if protocol == 'controls' else
        ['validation', 'hidden-before-1', 'hidden-before-2', 'q-trigger-64', 'hidden-after-q64', 'q-trigger-128',
         'hidden-after-q128', 'bounds', 'hidden-after-bounds', 'snapshots-1', 'hidden-after-snapshots', 'passes',
         'hidden-before-visible', 'visible-1', 'visible-2', 'hidden-after-visible', 'window-control',
         'hidden-after-window', 'hidden-after-idle', 'snapshots-2', 'hidden-closing'])
    require([s['Name'] for s in schedule] == [s['Name'] for s in runs] == expected_names, 'Incomplete or changed schedule')
    measurements, counter_rows, host_rows, snapshot_rows, bounds_rows = [], [], [], [], []
    native_runs, window_controls = [], []
    for index, (plan, run) in enumerate(zip(schedule, runs), 1):
        require(all(plan[k] == run[k] for k in ('Name', 'Order', 'Kind', 'CostMode', 'Visible', 'Observe')), 'Schedule mismatch')
        require(int(plan['Order']) == index, 'Order mismatch')
        folder = root/run['Name']; native = obj(folder/'run.json'); native_runs.append(native)
        require(run['ExitCode'] == '0' and run['TimedOut'] == 'False' and not run['LaunchError'], 'Native process failed')
        require(native['exit_code'] == 0 and not native['timed_out'] and not native['launch_error'], 'Native process failed')
        require(str(native['pid']) == run['Pid'] and native['name'] == run['Name'] and native['kind'] == run['Kind'], 'Native identity mismatch')
        require(native['start_utc'] == run['StartUtc'] and native['end_utc'] == run['EndUtc'], 'Run time mismatch')
        require(native['executable_sha256'] == (G2 if run['Kind'].startswith('trigger') else G1), 'Executable hash mismatch')
        args = native['arguments']
        require(('--show-test-window' in args) == native['visible_requested'] == (run['Visible'] == 'True'), 'Visibility request mismatch')
        require(args[-2] == '--profile', 'Profile output argument missing')
        host = rows(folder/'host-samples.csv')
        require(len(host) == native['observer_samples'], 'Observer sample mismatch')
        require(bool(host) == native['window_observer'] == (run['Observe'] == 'True'), 'Observer mode mismatch')
        if host:
            require(all(int(h['Pid']) == native['pid'] for h in host), 'Foreign host PID')
            require(all(0 <= int(h['Minimized']) <= int(h['Windows']) and 0 <= int(h['Visible']) <= int(h['Windows']) for h in host), 'Invalid window counts')
            host_rows.append({'run': run['Name'], 'kind': run['Kind'], 'samples': len(host),
                'samples_with_window': sum(int(h['Windows']) > 0 for h in host),
                'samples_visible': sum(int(h['Visible']) > 0 for h in host),
                'samples_minimized': sum(int(h['Minimized']) > 0 for h in host),
                'samples_native_foreground': sum(h['NativeForeground'] == 'True' for h in host),
                'foreground_pids': sorted(set(int(h['ForegroundPid']) for h in host)),
                'ac_line_values': sorted(set(int(h['AcLine']) for h in host)),
                'priorities': sorted(set(h['Priority'] for h in host)),
                'observer_ms_median': median(number(h['ObserverMs']) for h in host),
                'observer_ms_max': max(number(h['ObserverMs']) for h in host),
                'outer_dimensions': sorted(set((int(h['OuterWidth']), int(h['OuterHeight'])) for h in host))})
        if run['Kind'] == 'validation':
            require('--gpu-cost-check' in args and 'status=passed' in (folder/'cost-tests.txt').read_text(), 'Cost validation failed')
            continue
        if run['Kind'].startswith('trigger'):
            expected = '64' if run['Kind'] == 'trigger64' else '128'
            require('--gpu-coupling-check' in args and args[args.index('--gpu-pbf-size')+1] == expected and
                args[args.index('--gpu-pbf-restrict')+1] == 'on', 'Wrong load trigger')
            text = (folder/'coupling-tests.txt').read_text()
            require('status=passed' in text and 'disabled_coupling_negative_control=rejected' in text and
                'paired_state_replay=passed (90 checkpoints' in text, 'Q trigger correctness failed')
            continue
        if run['Kind'] == 'window':
            require('--mission-smoke-test' in args and '--gl-debug' in args and args[args.index('--test-vsync')+1] == 'off' and
                args[args.index('--test-frame-limit')+1] == '60', 'Window control mismatch')
            info = values(folder/'mission-ui.txt')
            require(int(info['frames']) >= 900 and int(info['steps']) >= 1700 and info['paused_ok'] == info['mission_complete'] == '1' and
                info['score'] == '3' and number(info['dropped_seconds']) == 0, 'Window mission failed')
            frames = [f for f in rows(folder/'frame-timings.csv') if f['rendered'] == '1' and int(f['frame']) >= 30]
            require(len(frames) >= 870 and all(number(f['mission_dropped_seconds']) == 0 for f in frames), 'Missing frames or lost simulation time')
            window_controls.append({'run': run['Name'], 'frames': len(frames), 'steps': int(info['steps']),
                'focused_frames': sum(f['focused'] == '1' for f in frames), 'minimized_frames': sum(f['minimized'] == '1' for f in frames),
                'median_ms': number(info['median_ms']), 'p95_ms': number(info['p95_ms']), 'p99_ms': number(info['p99_ms']), 'max_ms': number(info['max_ms']),
                'gl_debug': values(folder/'gl-debug.txt')})
            continue
        require(run['Kind'] == 'cost', 'Unexpected native action')
        mode = run['CostMode']
        for flag, value in {'--gpu-cost-mode': mode, '--gpu-timer-mode': 'elapsed', '--gpu-phase-gating': 'off',
                '--gpu-zero-projection': 'off', '--gpu-local-size': '256', '--gpu-reduction-size': '256', '--gpu-pbf-size': '256'}.items():
            require(args.count(flag) == 1 and args[args.index(flag)+1] == value, 'Cost argument mismatch: '+flag)
        require('--gpu-cost-profile' in args and '--gpu-counters' in args and '--gl-debug' not in args and '--gpu-group-timers' not in args, 'Unexpected cost diagnostics')
        state = values(folder/'environment.txt')
        for key, value in {'cost_mode': mode, 'gpu_timer_mode': 'elapsed', 'group_timers': '0', 'counters': '1',
                'readback_period': '2', 'soa': '1', 'neighbor_mode': '2', 'phase_gating': '0', 'skip_zero_projection': '0',
                'storage_effective': 'direct', 'coupling_mode': '2', 'neighbor_reads_effective': 'full',
                'local_size': '256', 'reduction_size': '256', 'pbf_size_effective': '256'}.items():
            require(state[key] == value, 'Effective mode mismatch: '+key)
        validation = rows(folder/'validation.csv')
        require(len(validation) == 4 and {v['scene'] for v in validation} == set(SCENES), 'Missing scene validation')
        for v in validation:
            scene = v['scene']; count = SCENES[scene]
            require(int(v['initial_active']) == count and v['finite'] == '1' and v['steps'] == '360', 'Invalid scene state')
            require(int(v['final_active']) >= count if scene == 'mission_blast' else int(v['final_active']) == count, 'Particle count mismatch')
            if mode == 'off':
                require(all(v[k] == '0' for k in ('trace_events', 'timestamp_pairs', 'header_copies', 'snapshots', 'staging_bytes')), 'Probe overhead in OFF control')
            elif mode == 'passes':
                require(v['timestamp_pairs'] == '240' and v['header_copies'] == v['snapshots'] == '0', 'Pass probe mismatch')
            else:
                require(int(v['header_copies']) > 0 and v['timestamp_pairs'] == '0', 'Bounds probe mismatch')
                require(int(v['snapshots']) == (24 if mode == 'snapshots' else 0), 'Snapshot probe mismatch')
            ticks = rows(folder/scene/'window-steps.csv')
            require([int(t['tick']) for t in ticks] == list(range(121, 361)), 'Missing raw CPU/counter samples')
            totals = values(folder/scene/'window-counters.txt')
            require(totals['local_size'] == totals['reduction_size'] == totals['pbf_size_effective'] == '256', 'Per-scene group mismatch')
            item = {'run': run['Name'], 'scene': scene, 'cost_mode': mode, 'ticks': len(ticks), 'device_buffer_bytes': int(totals['device_buffer_bytes'])}
            for key in ('dispatches', 'barriers', 'bins', 'body_reductions', 'readback_age_ticks', 'total_ms', 'solver_ms', 'prepare_ms', 'poll_ms', 'staging_ms', 'timer_begin_ms', 'timer_end_ms'):
                data = [number(t[key]) for t in ticks]
                item[key+'_median'] = median(data); item[key+'_max'] = max(data)
            counter_rows.append(item)
            if mode in ('bounds', 'snapshots'):
                dispatches = rows(folder/scene/'dispatches.csv')
                indexed = [d for d in dispatches if d['indexed'] == '1']
                require(len(indexed) == int(v['header_copies']), 'Missing Dmax trace entries')
                dmax = [number(d['displacement']) for d in indexed]
                bounds_rows.append({'run': run['Name'], 'scene': scene, 'indexed_dispatches': len(indexed),
                    'dmax_median': median(dmax), 'dmax_max': max(dmax),
                    'build_ids': len(set(d['builds'] for d in indexed)),
                    'dmax_over_1': sum(x > 1 for x in dmax), 'dmax_over_2': sum(x > 2 for x in dmax)})
            if mode == 'snapshots':
                captures = rows(folder/scene/'snapshots.csv')
                require(len(captures) == 48 and len(set(c['snapshot'] for c in captures)) == 24, 'Incomplete snapshot reconstruction')
                require(all(c['missed'] == '0' and c['reference'] == 'fresh_cpu_grid' for c in captures), 'Incomplete candidate set')
                require(all(number(c['dmax']) >= 0 and number(c['global_candidates']) >= number(c['geometric_neighbors']) and
                    number(c['fresh_candidates']) >= number(c['geometric_neighbors']) for c in captures), 'Invalid candidate reconstruction')
                histograms = rows(folder/scene/'snapshot-histograms.csv')
                hist_count = defaultdict(int)
                for h in histograms:
                    hist_count[h['snapshot'], h['reach']] += int(h['frequency'])
                require(all(hist_count[c['snapshot'], c['reach']] == int(c['active']) for c in captures), 'Incomplete candidate histogram')
                snapshot_rows.extend({'run': run['Name'], 'scene': scene, **c} for c in captures)
        summary = rows(folder/'summary.csv')
        require(len(summary) == len(set((s['scene'], s['metric']) for s in summary)) == 12, 'Invalid summary')
        for s in summary:
            require(s['scene'] in SCENES and s['metric'] in ('gpu_step_ms', 'cpu_submit_ms', 'batch_mean_step_wall_ms'), 'Unexpected metric')
            require(int(s['samples']) == (1 if s['metric'] == 'batch_mean_step_wall_ms' else 240), 'Invalid sample count')
            p = [number(s[k]) for k in ('median', 'p95', 'p99', 'max')]
            require(p == sorted(p) and min(p) >= 0, 'Invalid percentiles')
            measurements.append({'run': run['Name'], 'order': index, 'cost_mode': mode, 'visible': native['visible_requested'], **s})
    engine_summary = []
    if environment['gpu_engine_worker']:
        status = obj(root/'gpu-engine-status.json')
        require(status['available'] and not status['error'], 'GPU engine observer failed')
        engines = rows(root/'gpu-engines.csv')
        known_pids = {n['pid'] for n in native_runs}
        for n in native_runs:
            start, end = datetime.fromisoformat(n['start_utc']), datetime.fromisoformat(n['end_utc'])
            groups = defaultdict(list)
            for e in engines:
                # Select contained query intervals, not exact GPU execution intervals.
                if not (start <= datetime.fromisoformat(e['QueryStartUtc']) <= datetime.fromisoformat(e['QueryEndUtc']) <= end):
                    continue
                match = re.fullmatch(r'pid_(\d+)_luid_(.+?)_phys_(\d+)_eng_(\d+)_engtype_(.+)', e['Engine'])
                require(match is not None, 'Unknown GPU engine instance')
                owner = int(match[1]); role = 'native' if owner == n['pid'] else ('other_protocol_pid' if owner in known_pids else 'other_pid')
                groups[role, match[2], match[3], match[4], match[5]].append(number(e['UtilizationPercentage']))
            for (role, luid, physical, engine, kind), data in sorted(groups.items()):
                engine_summary.append({'run': n['name'], 'role': role, 'luid': luid, 'physical': physical, 'engine': engine,
                    'engine_type': kind, 'nonzero_instance_samples': len(data), 'max_instance_utilization': max(data)})
    write_csv(root/'analysis.csv', measurements); write_csv(root/'counter-analysis.csv', counter_rows)
    write_csv(root/'snapshot-analysis.csv', snapshot_rows); write_csv(root/'bounds-analysis.csv', bounds_rows)
    write_csv(root/'gpu-engine-analysis.csv', engine_summary); dump(root/'host-analysis.json', host_rows)
    dump(root/'window-analysis.json', window_controls)
    result = {'protocol': protocol, 'valid_native_runs': len(runs), 'cost_runs': len(measurements)//12,
        'raw_cpu_counter_ticks': 240*len(counter_rows), 'snapshot_reconstruction_rows': len(snapshot_rows),
        'bounds_scenes': len(bounds_rows), 'engine_groups': len(engine_summary),
        'scope': 'Per-process GPU percentiles, raw CPU/counters. Host and GPU-engine samples include initialization. Dmax/candidate probes are separate interventions; no simultaneous unprobed scene geometry or individual GPU times retained.'}
    dump(root/'analysis.json', result)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    print(json.dumps(analyze(parser.parse_args().input), indent=2))
