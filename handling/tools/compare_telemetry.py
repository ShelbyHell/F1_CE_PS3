"""Compare explicitly marked corner and lap windows in two slick telemetry CSVs."""
import argparse
import csv
import statistics


def window(value):
    try:
        start, end = map(float, value.split(':'))
        if 0 <= start < end:
            return start, end
    except ValueError:
        pass
    raise argparse.ArgumentTypeError('expected START:END in elapsed CSV seconds')


def load(path):
    with open(path, newline='', encoding='utf-8') as file:
        return [(float(row['elapsed_s']), row) for row in csv.DictReader(file)]


def corner(samples, start, end):
    data = [(t, r) for t, r in samples if start <= t <= end and r.get('player_driving', '1') == '1']
    if len(data) < 3 or data[0][0] - start > 0.25 or end - data[-1][0] > 0.25:
        raise ValueError(f'incomplete telemetry for corner {start:g}:{end:g}')
    if any(b[0] - a[0] > 0.25 for a, b in zip(data, data[1:])):
        raise ValueError(f'telemetry gap in corner {start:g}:{end:g}')
    throttle = [r for _, r in data if float(r['throttle']) >= 0.5 and int(r['contact_mask']) & 0xc == 0xc]
    slip = [r['slip_2'] == '1' or r['slip_3'] == '1' for r in throttle]
    return float(data[-1][1]['speed_kmh']), (100 * statistics.mean(slip) if slip else None), len(throttle)


def laps(values):
    if any(a[1] > b[0] for a, b in zip(values, values[1:])):
        raise ValueError('lap windows must be chronological and must not overlap')
    return [end - start for start, end in values]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stock')
    p.add_argument('modern')
    for mode in ('stock', 'modern'):
        p.add_argument(f'--{mode}-corner', type=window, required=True)
        p.add_argument(f'--{mode}-lap', type=window, action='append', default=[])
    args = p.parse_args()
    if len(args.stock_lap) != len(args.modern_lap):
        p.error('supply the same number of complete laps for both runs')
    try:
        for name in ('stock', 'modern'):
            speed, slip, count = corner(load(getattr(args, name)), *getattr(args, f'{name}_corner'))
            durations = laps(getattr(args, f'{name}_lap'))
            slip_text = 'n/a' if slip is None else f'{slip:.1f}%'
            print(f'{name}: exit {speed:.1f} km/h; rear slip {slip_text} ({count} powered contact samples)')
            if durations:
                print(f'  laps: {", ".join(f"{t:.2f}" for t in durations)} s; median {statistics.median(durations):.2f} s')
    except (OSError, KeyError, ValueError) as exc:
        p.error(str(exc))


if __name__ == '__main__':
    main()
