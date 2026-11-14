#!/usr/bin/env python3
"""Summarize paired stop-token benchmark samples.

Reads the samples.csv written by run_paired.sh. For every kernel it prints the
median per-operation time of each variant and the distribution of the
per-sample candidate/base ratios. Only the standard library is used.
"""
import statistics
import sys

CONTROLS = ('barrier', 'stop_requested')


def main(argv):
    if len(argv) != 2:
        print('usage: summarize_pairs.py SAMPLES_CSV', file=sys.stderr)
        return 2
    order = []
    values = {}
    with open(argv[1], encoding='utf-8') as samples:
        for line in samples:
            parts = line.strip().split(',')
            if not parts or parts[0] != 'stop_bench':
                continue
            fields = dict(part.split('=', 1) for part in parts[1:])
            kernel = fields['kernel']
            if kernel not in values:
                order.append(kernel)
                values[kernel] = {}
            count = int(fields['iterations'])
            if count <= 0:
                print(f'invalid iteration count for {kernel}: {count}', file=sys.stderr)
                return 2
            per_op = float(fields['ns']) / count
            values[kernel][(int(fields['sample']), fields['candidate'])] = per_op
    controls_ok = True
    for kernel in order:
        rows = values[kernel]
        samples = sorted({sample for sample, _ in rows})
        base = [rows[(s, 'base')] for s in samples if (s, 'base') in rows]
        candidate = [rows[(s, 'candidate')] for s in samples if (s, 'candidate') in rows]
        ratios = [rows[(s, 'candidate')] / rows[(s, 'base')] for s in samples
                  if (s, 'base') in rows and (s, 'candidate') in rows]
        if not ratios:
            continue
        if len(ratios) >= 2:
            q1, median, q3 = statistics.quantiles(ratios, n=4, method='inclusive')
        else:
            q1 = median = q3 = ratios[0]
        if kernel in CONTROLS and not 0.98 <= median <= 1.02:
            controls_ok = False
        print(f'{kernel},pairs={len(ratios)},base_ns={statistics.median(base):.3f},'
              f'candidate_ns={statistics.median(candidate):.3f},ratio_median={median:.4f},'
              f'ratio_q1={q1:.4f},ratio_q3={q3:.4f},ratio_min={min(ratios):.4f},'
              f'ratio_max={max(ratios):.4f}')
    print('controls_within_2_percent=' + ('yes' if controls_ok else 'no'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
