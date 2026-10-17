#!/usr/bin/env python3
"""Summarize the temporary AMULE_UPLOAD_IO_PROFILE CSV baseline."""
import argparse
import csv
import math


def summarize(stream):
    durations = []
    read_ns = active_ns = byte_count = failed = loops = elapsed_ns = 0
    modes = set()
    pending_reads = 0
    for row in csv.DictReader(stream):
        duration = int(row['duration_ns'])
        if duration < 0:
            raise ValueError('negative duration')
        if row['kind'] == 'loop':
            active_ns += duration
            elapsed_ns = max(elapsed_ns, int(row['elapsed_ns']))
            loops += 1
            pending_reads = 0
        elif row['kind'] == 'read':
            pending_reads += 1
            modes.add(int(row['mmap']))
            read_ns += duration
            if int(row['success']):
                durations.append(duration)
                byte_count += int(row['bytes'])
            else:
                failed += 1
        else:
            raise ValueError('unknown record kind')
    if modes - {0, 1} or len(modes) > 1:
        raise ValueError('keep mmap mode fixed throughout each baseline run')
    if pending_reads:
        raise ValueError('incomplete final loop; stop aMule normally before analyzing')
    if not loops or not elapsed_ns or not durations:
        raise ValueError('no completed loops with successful reads; collect an upload workload first')
    durations.sort()
    seconds = elapsed_ns / 1e9
    return {
        'mmap_enabled': bool(next(iter(modes))),
        'successful_reads': len(durations),
        'failed_or_deferred_reads': failed,
        'wall_seconds': seconds,
        'p50_ms': durations[math.ceil(len(durations) * .50) - 1] / 1e6,
        'p95_ms': durations[math.ceil(len(durations) * .95) - 1] / 1e6,
        'p99_ms': durations[math.ceil(len(durations) * .99) - 1] / 1e6,
        'reads_per_second': len(durations) / seconds,
        'bytes_per_second': byte_count / seconds,
        'read_share_active_percent': 100 * read_ns / active_ns if active_ns else 0,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv_path')
    args = parser.parse_args()
    try:
        with open(args.csv_path, newline='') as stream:
            result = summarize(stream)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'{error}\n')
    for key, value in result.items():
        print(f'{key}: {value:.3f}' if isinstance(value, float) else f'{key}: {value}')


if __name__ == '__main__':
    main()
