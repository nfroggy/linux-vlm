#!/usr/bin/env python3
"""Compare arbitrary interpreter variants using the same Gabriel protocol."""

import argparse
import csv
import glob
import gzip
import json
import statistics
from pathlib import Path

from analyze import read_run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", action="append", required=True,
                        metavar="NAME=CSV_GLOB",
                        help="one group of boots; the first is the baseline")
    parser.add_argument("--counts", type=Path,
                        default=Path(__file__).with_name("counts.json"))
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    counts = json.loads(args.counts.read_text())
    groups = {}
    reference_values = None
    for spec in args.group:
        name, pattern = spec.split("=", 1)
        if not name or name in groups:
            parser.error("Group names must be nonempty and unique")
        paths = sorted(Path(p) for p in glob.glob(pattern))
        if not paths:
            parser.error(f"No files match {pattern}")
        groups[name] = [read_run(path, counts, args.samples) for path in paths]
        for path in paths:
            values_path = Path(str(path) + ".values")
            values = (values_path.read_bytes() if values_path.exists() else
                      gzip.decompress(Path(str(values_path) + ".gz").read_bytes()))
            if reference_values is None:
                reference_values = values
            assert values == reference_values, f"Warm-up results differ: {path}"
        print(f"{name}: {len(paths)} boots, {args.samples} samples per workload per boot")

    baseline = next(iter(groups))
    fields = ["benchmark", "calls_per_sample"]
    for name in groups:
        fields += [f"{name}_ms", f"{name}_min_ms", f"{name}_max_ms",
                   f"{name}_speedup"]
    speedups = {name: [] for name in groups}
    print(f"{'Benchmark':18}" + "".join(f"{name:>14}" for name in groups))
    with args.output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for benchmark, calls in counts.items():
            times = {name: [run[benchmark] * 1000 for run in runs]
                     for name, runs in groups.items()}
            medians = {name: statistics.median(t) for name, t in times.items()}
            row = {"benchmark": benchmark, "calls_per_sample": calls}
            for name, t in times.items():
                speedup = medians[baseline] / medians[name]
                speedups[name].append(speedup)
                row.update({f"{name}_ms": medians[name],
                            f"{name}_min_ms": min(t), f"{name}_max_ms": max(t),
                            f"{name}_speedup": speedup})
            writer.writerow(row)
            print(f"{benchmark:18}" + "".join(f"{medians[n]:14.4f}" for n in groups))
    print("Times are milliseconds per canonical suite call.")
    for name, ratios in speedups.items():
        speedup = statistics.geometric_mean(ratios)
        print(f"{name}: {speedup:.4f}x speedup over {baseline}; "
              f"{100 * (1 - 1 / speedup):.2f}% less elapsed time; "
              f"faster on {sum(r > 1 for r in ratios)}/{len(ratios)} workloads")
    print("All warm-up return values match byte-for-byte across all groups and boots.")


if __name__ == "__main__":
    main()
