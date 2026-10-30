#!/usr/bin/env python3
"""Verify the frozen V14 research import and recompute its published populations.

MIT, copyright (c) 2026 Jonathan Yantis. Standard library only; no devices or
private inputs. This checks sanitized derivatives, not the original extraction,
reference calibration, firmware conformance, or electrical-output accuracy.
"""
import csv
import hashlib
import json
import math
from pathlib import Path

from check_v14_document import imported_bytes, main as check_document
from verify_v14_proposal import main as check_proposal

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "evidence/v14/2026-10-12"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def close(actual, expected, name):
    require(math.isclose(actual, expected, rel_tol=1e-10, abs_tol=1e-8), name)


def quantile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower, upper = math.floor(position), math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def metrics(values):
    mean = math.fsum(values) / len(values)
    return {
        "n": len(values),
        "bias_ns": mean,
        "rms_including_bias_ns": math.sqrt(math.fsum(x*x for x in values) / len(values)),
        "jitter_rms_ns": math.sqrt(math.fsum((x-mean)**2 for x in values) / len(values)),
        **{f"abs_p{p}_ns": quantile([abs(x) for x in values], p/100)
           for p in (50, 95, 99)},
        "abs_max_ns": max(abs(x) for x in values),
    }


def csv_rows(name):
    with (DATA / name).open(newline="") as stream:
        return list(csv.DictReader(stream))


def main():
    receipt = json.loads((ROOT / "papers/imports/v14-2026-10-13.json").read_text())
    destinations = set()
    for entry in receipt["files"]:
        path = (ROOT / entry["destination"]).resolve()
        require(path.is_relative_to(ROOT), "Import path escapes repository")
        require(entry["destination"] not in destinations, "Duplicate import path")
        destinations.add(entry["destination"])
        data = imported_bytes(entry)
        require(len(data) == entry["bytes"], f"Size mismatch: {path}")
        require(hashlib.sha256(data).hexdigest() == entry["sha256"], f"Hash mismatch: {path}")

    summary = json.loads((DATA / "summary.json").read_text())
    evidence = json.loads((DATA / "ptob-v14-evidence.json").read_text())
    require(summary == evidence["selected_run"]["summary"], "Evidence/summary disagreement")
    rows = csv_rows("pps-sanitized.csv")
    require(len(rows) == summary["observed_edges"], "Observed-edge denominator")
    times = []
    valid, all_causal = [], []
    invalid = expired = 0
    for index, row in enumerate(rows, 1):
        require(int(row["observation"]) == index, "Observation sequence")
        require(row["reference_valid"] in ("0", "1") and
                row["map_available"] in ("0", "1"), "Unknown classification")
        time = float(row["time_s"])
        require(math.isfinite(time) and 0 <= time <= summary["duration_s"], "Edge time range")
        if times:
            require(time > times[-1], "Nonmonotonic edge times")
            close(float(row["gap_before_s"]), time-times[-1], "Recorded witness gap")
        else:
            require(row["gap_before_s"] == "", "First edge has no preceding observed gap")
        times.append(time)
        if row["map_available"] == "1":
            require(row["reason"] == "OK", "Available-map classification")
            require(0 <= float(row["update_age_ms"]) < 500, "Functional holdover limit")
            phase = float(row["phase_ns"])
            require(math.isfinite(phase), "Nonfinite phase")
            all_causal.append(phase)
            if row["reference_valid"] == "1":
                valid.append(phase)
            else:
                invalid += 1
        else:
            require(row["reference_valid"] == "1", "Unexpected unavailable population")
            expired += 1
    computed = {}
    for name, values in (("reference_valid_causal", valid), ("all_causal", all_causal)):
        computed[name] = metrics(values)
        for key, value in computed[name].items():
            close(value, summary[name][key], f"{name}: {key}")
    require(invalid == summary["invalid_reference"], "Invalid-reference denominator")
    require(expired == summary["reference_valid_expired"], "Expired-map denominator")
    nominal = math.ceil(summary["duration_s"])
    require(nominal == summary["nominal_boundaries"], "Conservative nominal boundaries")
    require(nominal-len(rows) == summary["unobserved_conservative"], "Unobserved denominator")
    require(nominal == len(valid)+invalid+expired+summary["unobserved_conservative"],
            "Full opportunity accounting")
    close(max(b-a for a, b in zip(times, times[1:])), summary["max_witness_gap_s"],
          "Maximum witness gap")

    diagnostics = csv_rows("diagnostic-sanitized.csv")
    require({r["role"] for r in diagnostics} == {"RC", "leaf"}, "Diagnostic roles")
    require({r["op"] for r in diagnostics} == {"network", "precision"}, "Diagnostic types")
    for role, expected in summary["continuity"].items():
        network = [r for r in diagnostics if r["role"] == role and r["op"] == "network"]
        precision = [r for r in diagnostics if r["role"] == role and r["op"] == "precision"]
        require(len(network) == expected["network_snapshots"], f"{role}: snapshot denominator")
        require([int(network[0]["binds"]), int(network[-1]["binds"])] ==
                expected["renewal_first_last"], f"{role}: renewal receipts")
        for field, counter in (("expiries", "holdover_expiries"),
                               ("recoveries", "holdover_recoveries"), ("accepted", "accepted")):
            require(int(precision[-1][field])-int(precision[0][field]) ==
                    expected["interval_precision_counter_deltas"][counter], f"{role}: {field}")

    table = {r["quantity"]: float(r["value"]) for r in csv_rows("result-table.csv")}
    expected = {**computed["reference_valid_causal"], "observed_edges": len(rows),
                "nominal_boundaries": nominal, "usable_coverage": len(valid)/nominal}
    require(set(table) == set(expected), "Result-table quantities")
    for key, value in expected.items():
        close(table[key], value, f"Result table: {key}")
    contract = json.loads((ROOT / "protocol/v14_rc1/contract.json").read_text())
    fields = [{"layout": name, "record_bytes": str(layout["bytes"]), "field": field["name"],
               "offset": str(field["offset"]), "bytes": str(field["bytes"])}
              for name, layout in contract["wire"].items() for field in layout["fields"]]
    require(csv_rows("wire-layouts.csv") == fields, "Wire table differs from frozen contract")
    check_document()
    check_proposal()
    print(f"Verified {len(destinations)} V14 imported file hashes; recomputed 436 valid and "
          "501 all-causal samples, 65 invalid-reference and 3 expired edges.")
    print("Full population: 602 = 436 + 65 + 3 + 98 unobserved; sampled RC/leaf counters match.")
    print("Raw extraction, training calibration, historical C/ASan suite, hardware and UTC/output "
          "qualification were not rerun.")


if __name__ == "__main__":
    main()
