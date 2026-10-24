#!/usr/bin/env python3
"""Validate repeated Gabriel runs and compare median elapsed time per call."""

import argparse
import csv
import gzip
import json
import statistics
from collections import defaultdict
from pathlib import Path


def read_run(path, counts, samples):
    groups = defaultdict(list)
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            name = row.pop("benchmark")
            numbers = {key: int(value) for key, value in row.items()}
            assert numbers["calls"] == counts[name], (path, name, numbers)
            assert numbers["real_ticks"] > 0, (path, name, numbers)
            assert numbers["ticks_per_second"] == 977, (path, name, numbers)
            groups[name].append(numbers)
    assert groups.keys() == counts.keys(), (path, groups.keys())
    medians = {}
    for name, rows in groups.items():
        assert sorted(row["sample"] for row in rows) == list(range(1, samples + 1))
        medians[name] = statistics.median(
            row["real_ticks"] / row["ticks_per_second"] / row["calls"]
            for row in rows
        )
    return medians


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--counts", type=Path, default=Path(__file__).with_name("counts.json"))
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--boots", type=int, default=3)
    args = parser.parse_args()
    counts = json.loads(args.counts.read_text())
    runs = {
        engine: [
            read_run(args.directory / f"{engine}-{boot}.csv", counts, args.samples)
            for boot in range(1, args.boots + 1)
        ]
        for engine in ("c", "alpha")
    }
    # Every boot starts with the same world and executes the same workloads.
    values = []
    for engine in runs:
        for boot in range(1, args.boots + 1):
            path = args.directory / f"{engine}-{boot}.csv.values"
            if path.exists():
                values.append(path.read_bytes())
            else:
                values.append(gzip.decompress(path.with_suffix('.values.gz').read_bytes()))
    assert all(value == values[0] for value in values), "Warm-up results differ"
    fields = ["benchmark", "calls_per_sample", "alpha_ms", "c_ms", "c_over_alpha",
              "alpha_boot_min_ms", "alpha_boot_max_ms", "c_boot_min_ms", "c_boot_max_ms"]
    ratios = []
    with (args.directory / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        print("Benchmark          Alpha (ms)       C (ms)   C / Alpha")
        for name, calls in counts.items():
            times = {engine: [run[name] * 1000 for run in boots]
                     for engine, boots in runs.items()}
            medians = {engine: statistics.median(t) for engine, t in times.items()}
            ratio = medians["c"] / medians["alpha"]
            ratios.append(ratio)
            row = {"benchmark": name, "calls_per_sample": calls,
                   "c_over_alpha": ratio}
            for engine in runs:
                row[f"{engine}_ms"] = medians[engine]
                row[f"{engine}_boot_min_ms"] = min(times[engine])
                row[f"{engine}_boot_max_ms"] = max(times[engine])
            writer.writerow(row)
            print(f"{name:18} {medians['alpha']:10.4f} {medians['c']:12.4f} {ratio:11.3f}")
    print(f"Geometric mean C / Alpha: {statistics.geometric_mean(ratios):.4f}")
    print(f"C faster on {sum(r < 1 for r in ratios)} / {len(ratios)} workloads")
    print("Per-pair geometric means:", ", ".join(
        f"{statistics.geometric_mean(c[name] / a[name] for name in counts):.4f}"
        for c, a in zip(runs["c"], runs["alpha"])
    ))
    print("All warm-up return values match byte-for-byte across both engines and all boots.")


if __name__ == "__main__":
    main()
