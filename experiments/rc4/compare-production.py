#!/usr/bin/env python3
"""Pair baseline/production runs, alternate order, and reject checksum mismatches."""
import argparse
import csv
import math
from pathlib import Path
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("baseline", type=Path)
parser.add_argument("production", type=Path)
parser.add_argument("--samples", type=int, default=7)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--workloads", nargs="+", choices=("180K", "10K", "1300", "TCP", "UDP"),
                    default=("180K", "10K", "1300", "TCP", "UDP"))
args = parser.parse_args()
if args.samples < 3:
    parser.error("at least three samples are required")
executables = [args.baseline.resolve(), args.production.resolve()]
with args.output.open("w", newline="") as stream:
    writer = csv.writer(stream, lineterminator="\n")
    writer.writerow(["workload", "sample", "baseline", "production", "ratio", "checksum", "baseline_cpu", "production_cpu", "cpu_ratio"])
    print("workload,baseline_median,production_median,paired_ratio_median,ratio_min,ratio_max,cpu_ratio_median,cpu_ratio_min,cpu_ratio_max", flush=True)
    for workload in args.workloads:
        baseline, production, ratios, cpu_ratios = [], [], [], []
        for sample in range(args.samples):
            results = {}
            for index in ((0, 1) if sample % 2 == 0 else (1, 0)):
                output = subprocess.check_output([str(executables[index]), workload], text=True, timeout=60)
                rate, cpu_rate, checksum = output.strip().split(",")
                results[index] = float(rate), int(checksum), float(cpu_rate)
            if results[0][1] != results[1][1]:
                raise RuntimeError(f"cipher stream mismatch in {workload} sample {sample}")
            old, new = results[0][0], results[1][0]
            if not (math.isfinite(old) and math.isfinite(new)) or old <= 0 or new <= 0:
                raise RuntimeError("invalid timing sample")
            baseline.append(old)
            production.append(new)
            ratios.append(new / old)
            old_cpu, new_cpu = results[0][2], results[1][2]
            if not (math.isfinite(old_cpu) and math.isfinite(new_cpu)) or old_cpu <= 0 or new_cpu <= 0:
                raise RuntimeError("invalid CPU timing sample")
            cpu_ratios.append(new_cpu / old_cpu)
            writer.writerow([workload, sample, old, new, new / old, results[0][1], old_cpu, new_cpu, new_cpu / old_cpu])
            stream.flush()
        print(f"{workload},{statistics.median(baseline):.1f},"
              f"{statistics.median(production):.1f},{statistics.median(ratios):.3f},"
              f"{min(ratios):.3f},{max(ratios):.3f},"
              f"{statistics.median(cpu_ratios):.3f},{min(cpu_ratios):.3f},{max(cpu_ratios):.3f}", flush=True)
