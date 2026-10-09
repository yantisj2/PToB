#!/usr/bin/env python3
"""Verify curated import hashes and recompute the published PLL capture metrics.

Standard library only. Does not contact hardware or open private archives.
This verifies the included derivatives, not calibration or raw-data provenance.
"""
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    manifest = json.loads((ROOT / "evidence/imports/2026-10-09.json").read_text())
    for entry in manifest["files"]:
        path = (ROOT / entry["destination"]).resolve()
        require(path.is_relative_to(ROOT), "Manifest path escapes repository")
        data = path.read_bytes()
        require(len(data) == entry["bytes"], f"Size mismatch: {path}")
        require(hashlib.sha256(data).hexdigest() == entry["sha256"],
                f"Hash mismatch: {path}")

    base = ROOT / "evidence/papers/oscillator-pll-2026-10-03"
    groups = defaultdict(list)
    with (base / "indexed-pair-samples.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            groups[(row["run_id"], row["cohort"], row["capture"])].append(row)
    with (base / "capture-metrics.csv").open(newline="") as stream:
        summaries = list(csv.DictReader(stream))
    expected_keys = {(r["run_id"], r["cohort"], r["capture"]) for r in summaries}
    require(len(expected_keys) == len(summaries), "Duplicate capture summary")
    require(set(groups) == expected_keys, "Sample/summary populations differ")
    scored_total = unavailable_total = 0
    for summary in summaries:
        key = (summary["run_id"], summary["cohort"], summary["capture"])
        rows = groups[key]
        valid = [r for r in rows if r["delivered_causal_valid"] == "1"]
        require(all(r["delivered_causal_valid"] in ("0", "1") for r in rows),
                f"Unknown validity state: {key}")
        errors = [int(r["delivered_error_ns"]) for r in valid]
        require(len(rows) == int(summary["retained_pair_rows"]), f"Rows: {key}")
        require(len(errors) == int(summary["n"]), f"Scored denominator: {key}")
        missing = len(rows) - len(errors)
        require(missing == int(summary["unavailable_delivered_rows"]),
                f"Unavailable denominator: {key}")
        require(errors, f"No scoreable rows: {key}")
        absolute = sorted(abs(x) for x in errors)
        computed = {
            "mean": sum(errors) / len(errors),
            "rms": math.sqrt(sum(x*x for x in errors) / len(errors)),
            "min": min(errors), "max": max(errors), "max_abs": absolute[-1],
            "p95_abs": absolute[math.ceil(0.95 * len(errors))-1],
            "p99_abs": absolute[math.ceil(0.99 * len(errors))-1],
        }
        for name, value in computed.items():
            require(math.isclose(value, float(summary[name]), rel_tol=1e-12,
                                 abs_tol=1e-9), f"Metric {name}: {key}")
        scored_total += len(errors)
        unavailable_total += missing
    print(f"Verified {len(manifest['files'])} imported file hashes.")
    print(f"Recomputed {len(summaries)} separate PLL captures: "
          f"{scored_total} scored and {unavailable_total} unavailable rows.")
    print("No pooled accuracy, raw-archive verification, or hardware qualification implied.")


if __name__ == "__main__":
    main()
