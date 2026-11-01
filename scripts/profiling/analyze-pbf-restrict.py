#!/usr/bin/env python3
"""Verify the G1 / G2 OFF / G2 ON experiment before comparing percentiles."""
from __future__ import annotations
import argparse
from collections import defaultdict
import csv
import json
import math
from pathlib import Path
from statistics import median


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def values(path):
    return dict(line.split('=', 1) for line in path.read_text(encoding='utf-8-sig').splitlines() if '=' in line)


def analyze(root):
    schedule, runs = rows(root/'schedule.csv'), rows(root/'runs.csv')
    assert len(schedule) == len(runs) and len(runs) in (18, 36), 'Incomplete experiment'
    scenes = {'mission': 3096, 'stress_16k': 16000, 'stress_32k': 32768, 'mission_blast': 3096}
    records, counters = [], []
    hashes, shader_hashes = defaultdict(set), set()
    for planned, run in zip(schedule, runs):
        assert all(planned[k] == run[k] for k in ('Name', 'Variant', 'Repeat', 'Order')), 'Schedule mismatch'
        assert run['ExitCode'] == '0' and run['TimedOut'].lower() == 'false' and not run['LaunchError'], 'Native process failed'
        variant = run['Variant']; assert variant in ('g1', 'g2off', 'g2on')
        folder = root/run['ProfileDirectory']
        native = json.loads((folder/'run.json').read_text(encoding='utf-8-sig'))
        assert native['exit_code'] == 0 and not native['timed_out'] and not native['launch_error']
        args = native['arguments']; assert args == json.loads(run['ArgumentsJson']), 'Argument record mismatch'
        assert '--gpu-cost-profile' in args and args[args.index('--gpu-cost-mode')+1] == 'off'
        assert '--gl-debug' not in args and '--gpu-group-timers' not in args
        for flag in ('--gpu-local-size', '--gpu-reduction-size', '--gpu-pbf-size'):
            assert args[args.index(flag)+1] == '256', 'Requested group size changed'
        if variant == 'g1':
            assert '--gpu-pbf-restrict' not in args, 'Frozen G1 does not support the new flag'
        else:
            assert args[args.index('--gpu-pbf-restrict')+1] == ('on' if variant == 'g2on' else 'off'), 'Requested restrict mode mismatch'
        environment = values(folder/'environment.txt')
        expected = {'cost_mode': 'off', 'gpu_timer_mode': 'elapsed', 'group_timers': '0', 'counters': '1', 'readback_period': '2',
            'soa': '1', 'neighbor_mode': '2', 'phase_gating': '0', 'skip_zero_projection': '0', 'storage_effective': 'direct',
            'coupling_mode': '2', 'neighbor_reads_effective': 'full', 'local_size': '256', 'reduction_size': '256', 'pbf_size_effective': '256'}
        for key, value in expected.items():
            assert environment[key] == value, (key, environment.get(key))
        if variant != 'g1':
            mode = '1' if variant == 'g2on' else '0'
            assert environment['pbf_restrict_requested'] == environment['pbf_restrict_effective'] == mode, 'Effective restrict mode mismatch'
        metadata = json.loads((folder.parent/'environment.json').read_text(encoding='utf-8-sig'))
        hashes[variant].add(metadata['executable_sha256'])
        shader_hashes.add(tuple(sorted((folder.parent/'shader-sha256.txt').read_text(encoding='utf-8-sig').splitlines())))
        validation = rows(folder/'validation.csv')
        assert len(validation) == 4 and {v['scene'] for v in validation} == set(scenes)
        for v in validation:
            scene, count = v['scene'], scenes[v['scene']]
            assert int(v['initial_active']) == count and v['finite'] == '1' and v['steps'] == '360'
            assert int(v['final_active']) >= count if scene == 'mission_blast' else int(v['final_active']) == count
            assert all(v[k] == '0' for k in ('trace_events', 'timestamp_pairs', 'header_copies', 'snapshots', 'staging_bytes')), 'Diagnostic overhead enabled'
            ticks = rows(folder/scene/'window-steps.csv')
            assert [int(t['tick']) for t in ticks] == list(range(121, 361)), 'Missing raw tick samples'
            assert all(math.isfinite(float(t['total_ms'])) for t in ticks), 'Nonfinite raw CPU samples'
            totals = values(folder/scene/'window-counters.txt')
            assert totals['local_size'] == totals['reduction_size'] == totals['pbf_size_effective'] == '256'
            if variant != 'g1':
                assert totals['pbf_restrict_effective'] == mode, 'Per-scene restrict mode mismatch'
            metrics = {k: {'median': median(float(t[k]) for t in ticks), 'max': max(float(t[k]) for t in ticks)}
                for k in ('dispatches', 'barriers', 'bins', 'body_reductions', 'readback_age_ticks')}
            counters.append({'run': run['Name'], 'variant': variant, 'scene': scene, 'ticks': len(ticks),
                'device_buffer_bytes': int(totals['device_buffer_bytes']), 'metrics': metrics})
        summary = rows(folder/'summary.csv')
        assert len(summary) == 12 and len({(x['scene'], x['metric']) for x in summary}) == 12
        for row in summary:
            assert row['scene'] in scenes and row['metric'] in ('gpu_step_ms', 'cpu_submit_ms', 'batch_mean_step_wall_ms')
            assert int(row['samples']) == (1 if row['metric'] == 'batch_mean_step_wall_ms' else 240)
            percentiles = [float(row[k]) for k in ('median', 'p95', 'p99', 'max')]
            assert all(math.isfinite(x) and x > 0 for x in percentiles) and percentiles == sorted(percentiles)
            records.append({'run': run['Name'], 'variant': variant, 'repeat': run['Repeat'], **row})
    assert all(len(v) == 1 for v in hashes.values()), 'Executable changed during measurement'
    assert hashes['g1'] == {'1a1f53457ca590187417818f24d42bbea93d11c7b29bc5e7d8fea7320f918d5e'}, 'Baseline is not frozen G1'
    assert hashes['g2off'] == hashes['g2on'], 'G2 variants use different binaries'
    assert len(shader_hashes) == 1, 'On-disk shader sources changed between runs'
    grouped = defaultdict(list)
    for row in records:
        grouped[row['scene'], row['metric'], row['variant']].append(row)
    aggregates = []
    for (scene, metric, variant), data in sorted(grouped.items()):
        assert len(data) == len(runs)//3 and len({r['repeat'] for r in data}) == len(data)
        baseline = {r['repeat']: float(r['median']) for r in grouped[scene, metric, 'g1']}
        control = {r['repeat']: float(r['median']) for r in grouped[scene, metric, 'g2off']}
        aggregates.append({'scene': scene, 'metric': metric, 'variant': variant, 'runs': len(data),
            'median_of_medians': median(float(r['median']) for r in data), 'median_of_p95': median(float(r['p95']) for r in data),
            'median_paired_change_vs_g1_pct': median(100*(float(r['median'])/baseline[r['repeat']]-1) for r in data),
            'median_paired_change_vs_off_pct': median(100*(float(r['median'])/control[r['repeat']]-1) for r in data)})
    for name, data in [('analysis.csv', records), ('aggregate.csv', aggregates)]:
        with (root/name).open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(data[0])); writer.writeheader(); writer.writerows(data)
    result = {'schema_version': 1, 'valid_runs': len(runs), 'rows': len(records), 'raw_cpu_counter_ticks': 240*len(counters),
        'executable_hashes': {k: sorted(v) for k, v in hashes.items()}, 'aggregates': aggregates,
        'scope': 'Per-process percentiles over 240 GPU samples; raw CPU/counters retained; no pooled GPU percentile or statistical significance claim.'}
    (root/'analysis.json').write_text(json.dumps(result, indent=2)+'\n')
    (root/'counter-summary.json').write_text(json.dumps(counters, indent=2)+'\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'aggregates'}, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--input', required=True, type=Path)
    analyze(parser.parse_args().input)
