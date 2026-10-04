"""Analyze capture-only app metrics and PresentMon display evidence separately.

Percentiles use nearest rank; median uses the midpoint for even sample counts.
PresentMon v2 DisplayedTime is duration on screen, NOT CPU submission cadence.
https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def summarize(values):
    values = sorted(values)
    if not values:
        return None
    mean = statistics.mean(values)
    return {'count': len(values), 'median_ms': statistics.median(values),
            'p95_ms': values[math.ceil(len(values) * .95) - 1],
            'p99_ms': values[math.ceil(len(values) * .99) - 1],
            'worst_ms': values[-1], 'mean_ms': mean,
            'average_fps': 1000 / mean if mean > 0 else None}


def number(value, allow_zero=False):
    try:
        result = float(value)
        return result if math.isfinite(result) and (result >= 0 if allow_zero else result > 0) else None
    except (TypeError, ValueError):
        return None

def analyze_presentmon(rows):
    streams = {(r.get('ProcessID', ''), r.get('SwapChainAddress', '')) for r in rows}
    if len(streams) > 1:
        raise ValueError('Multiple process/swap-chain streams; capture or select one explicitly.')
    displayed = []
    undisplayed = invalid = 0
    for row in rows:
        if 'DisplayedTime' not in row:
            invalid += 1
            continue
        raw = row['DisplayedTime']
        if str(raw).strip().upper() == 'NA':
            undisplayed += 1
        elif (value := number(raw)) is not None:
            displayed.append(value)
        else:
            invalid += 1
    return {'rows': len(rows), 'stream': sorted(streams), 'displayed': summarize(displayed),
            'undisplayed_rows': undisplayed, 'invalid_display_rows': invalid,
            'present_modes': sorted({r.get('PresentMode', '') for r in rows}),
            'sync_intervals': sorted({r.get('SyncInterval', '') for r in rows})}

def gates(result, refresh_hz, width, height):
    display = result.get('displayed')
    result = {'minimum': 'unverified', 'preferred': 'unverified',
              'stutter_attribution': 'unverified'}
    if display and refresh_hz and refresh_hz > 60:
        result['minimum'] = 'pass' if display['average_fps'] > 60 and display['p99_ms'] < 16.67 else 'fail'
        result['worst_gap_refresh_intervals'] = display['worst_ms'] * refresh_hz / 1000
        if refresh_hz == 144 and width == 1920 and height == 1080:
            result['preferred'] = 'pass' if abs(display['median_ms'] - 1000 / 144) <= .35 and display['p99_ms'] <= 7.3 else 'fail'
    return result

def analyze_app(rows):
    result = {label: summarize([value for row in rows
                            if (value := number(row.get(column), allow_zero=True)) is not None])
            for label, column in [('cpu_active', 'cpu_active_ms'), ('gpu', 'gpu_ms'),
                                  ('present_api', 'present_ms')]}
    result['gpu_missing_or_invalid_rows'] = sum(number(row.get('gpu_ms'), allow_zero=True) is None for row in rows)
    for label in ('cpu_active', 'gpu', 'present_api'):
        if result[label]:
            result[label].pop('average_fps', None)  # CPU/GPU work duration is not a frame cadence.
    return result


def read_csv(path):
    with Path(path).open(newline='', encoding='utf-8-sig') as capture:
        return list(csv.DictReader(capture))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app')
    parser.add_argument('--presentmon')
    parser.add_argument('--refresh', type=float)
    parser.add_argument('--width', type=int)
    parser.add_argument('--height', type=int)
    parser.add_argument('--output')
    args = parser.parse_args()
    metadata_path = Path(args.app + '.json') if args.app else None
    metadata = json.loads(metadata_path.read_text(encoding='utf-8-sig')) if metadata_path and metadata_path.exists() else {}
    report = {'method': 'nearest-rank p95/p99; midpoint median; FPS = 1000 / mean displayed duration',
              'app': analyze_app(read_csv(args.app)) if args.app else None,
              'presentation': analyze_presentmon(read_csv(args.presentmon)) if args.presentmon else None}
    refresh = metadata.get('refresh_hz', args.refresh)
    width = metadata.get('output_width', args.width)
    height = metadata.get('output_height', args.height)
    report['metadata'] = metadata or None
    report['gates'] = gates(report['presentation'] or {}, refresh, width, height)
    report['protocol'] = {'warmup_at_least_30s': metadata.get('warmup_seconds', 0) >= 30,
                          'capture_at_least_300s': metadata.get('capture_seconds', 0) >= 300,
                          'note': 'Sample thresholds are distinct from full acceptance; protocol and three independent runs must also be reviewed.'}
    display = (report['presentation'] or {}).get('displayed')
    display_seconds = display['count'] * display['mean_ms'] / 1000 if display else None
    report['protocol']['displayed_duration_seconds'] = display_seconds
    report['protocol']['displayed_coverage_at_least_295s'] = display_seconds is not None and display_seconds >= 295
    report['headroom'] = {
        name: ('unverified' if not report['app'] or not report['app'][name]
               else 'pass' if report['app'][name]['p99_ms'] <= budget else 'fail')
        for name, budget in [('cpu_active', 1), ('gpu', 5.5)]}
    rendered = json.dumps(report, indent=2, allow_nan=False)
    if args.output:
        Path(args.output).write_text(rendered + '\n', encoding='utf-8')
    print(rendered)


if __name__ == '__main__':
    main()
