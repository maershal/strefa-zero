#!/usr/bin/env python3
"""Validate and summarize the four-treatment PBF experiment without pooling ticks."""
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
    schedule = rows(root/'schedule.csv')
    runs = rows(root/'runs.csv')
    assert len(schedule) == len(runs) and len(runs) in (16, 32), 'Incomplete experiment'
    expected_scenes = {'mission': 3096, 'stress_16k': 16000, 'stress_32k': 32768, 'mission_blast': 3096}
    records = []
    hashes = defaultdict(set)
    for planned, run in zip(schedule, runs):
        assert all(planned[k] == run[k] for k in ('Name', 'Variant', 'Repeat', 'Order')), 'Schedule mismatch'
        assert run['ExitCode'] == '0' and run['TimedOut'].lower() == 'false' and not run['LaunchError'], 'Native process failed'
        variant = run['Variant']
        assert variant in ('q', 'g256', 'g128', 'g64')
        folder = root/run['ProfileDirectory']
        native = json.loads((folder/'run.json').read_text(encoding='utf-8-sig'))
        assert native['exit_code'] == 0 and not native['timed_out'] and not native['launch_error']
        args = native['arguments']
        assert args == json.loads(run['ArgumentsJson']), 'Argument record mismatch'
        assert '--gpu-cost-profile' in args and args[args.index('--gpu-cost-mode')+1] == 'off'
        environment = values(folder/'environment.txt')
        assert environment['cost_mode'] == 'off' and environment['gpu_timer_mode'] == 'elapsed'
        for key, value in {'soa': '1', 'phase_gating': '0', 'skip_zero_projection': '0', 'storage_effective': 'direct', 'coupling_mode': '2', 'neighbor_reads_effective': 'full'}.items():
            assert environment[key] == value, (key, environment.get(key))
        if variant == 'q':
            assert '--gpu-pbf-size' not in args, 'Baseline must use its frozen default'
        else:
            size = variant[1:]
            assert args[args.index('--gpu-pbf-size')+1] == size, 'Requested PBF size mismatch'
            assert environment['pbf_size_effective'] == environment['pbf_size_requested'] == size, 'Effective PBF size mismatch'
            assert environment['local_size'] == environment['reduction_size'] == '256', 'Other group sizes changed'
        executable = json.loads((folder.parent/'environment.json').read_text(encoding='utf-8-sig'))
        hashes[variant].add(executable['executable_sha256'])
        validation = rows(folder/'validation.csv')
        assert {v['scene'] for v in validation} == set(expected_scenes) and len(validation) == 4
        for v in validation:
            count = expected_scenes[v['scene']]
            assert int(v['initial_active']) == count and v['finite'] == '1' and v['steps'] == '360'
            assert int(v['final_active']) >= count if v['scene'] == 'mission_blast' else int(v['final_active']) == count
            assert all(v[key] == '0' for key in ('trace_events', 'timestamp_pairs', 'header_copies', 'snapshots', 'staging_bytes')), 'Diagnostic overhead enabled'
            ticks = rows(folder/v['scene']/'window-steps.csv')
            assert [int(t['tick']) for t in ticks] == list(range(121, 361)), 'Missing raw tick samples'
            assert all(math.isfinite(float(t['total_ms'])) for t in ticks), 'Nonfinite raw CPU samples'
            counters = values(folder/v['scene']/'window-counters.txt')
            assert counters['local_size'] == counters['reduction_size'] == '256'
            if variant != 'q':
                assert counters['pbf_size_effective'] == variant[1:], 'Per-scene PBF configuration mismatch'
        summaries = rows(folder/'summary.csv')
        assert len(summaries) == 12 and len({(r['scene'], r['metric']) for r in summaries}) == 12
        for row in summaries:
            assert row['scene'] in expected_scenes and row['metric'] in ('gpu_step_ms', 'cpu_submit_ms', 'batch_mean_step_wall_ms')
            assert int(row['samples']) == (1 if row['metric'] == 'batch_mean_step_wall_ms' else 240)
            assert all(math.isfinite(float(row[k])) and float(row[k]) > 0 for k in ('median', 'p95', 'p99', 'max'))
            records.append({'run': run['Name'], 'variant': variant, 'repeat': run['Repeat'], **row})
    assert all(len(v) == 1 for v in hashes.values()), 'Executable changed during measurements'
    assert hashes['q'] == {'50dd13f0f4999f1aca209c1fc4783483aba0454a31995dc36578949335c7fdb4'}, 'Baseline is not the frozen published Q executable'
    assert hashes['g64'] == hashes['g128'] == hashes['g256'], 'Candidate variants use different binaries'
    grouped = defaultdict(list)
    for row in records:
        grouped[row['scene'], row['metric'], row['variant']].append(row)
    aggregates = []
    for (scene, metric, variant), data in sorted(grouped.items()):
        assert len(data) == len(runs)//4
        baseline = {r['repeat']: float(r['median']) for r in grouped[scene, metric, 'q']}
        control = {r['repeat']: float(r['median']) for r in grouped[scene, metric, 'g256']}
        aggregates.append({'scene': scene, 'metric': metric, 'variant': variant, 'runs': len(data),
            'median_of_medians': median(float(r['median']) for r in data),
            'median_of_p95': median(float(r['p95']) for r in data),
            'median_paired_change_vs_q_pct': median(100*(float(r['median'])/baseline[r['repeat']]-1) for r in data),
            'median_paired_change_vs_g256_pct': median(100*(float(r['median'])/control[r['repeat']]-1) for r in data)})
    for name, data in [('analysis.csv', records), ('aggregate.csv', aggregates)]:
        with (root/name).open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(data[0])); writer.writeheader(); writer.writerows(data)
    result = {'schema_version': 1, 'valid_runs': len(runs), 'rows': len(records), 'executable_hashes': {k: sorted(v) for k, v in hashes.items()},
        'scope': 'Medians of per-process percentiles and within-repeat median changes; no pooled percentile or significance claim.', 'aggregates': aggregates}
    (root/'analysis.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'aggregates'}, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    analyze(parser.parse_args().input)
