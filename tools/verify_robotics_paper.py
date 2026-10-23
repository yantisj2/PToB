#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Yantis.
"""Check robotics paper arithmetic, cited populations and edition identity offline.

These are analytical reference examples, not a gPTP implementation, oscillator
qualification, network schedulability proof or robot-control test.
"""
import argparse
from decimal import Decimal, localcontext
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "papers/ptob-gptp-tsn-robotics-preprint.tex"
EDITION = ROOT / "papers/editions/gptp-tsn-robotics-2026-10-22.json"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def bound_sum(terms):
    """None represents a missing bound, which may never become zero."""
    require(all(x is None or x >= 0 for x in terms), "negative error bound")
    return None if any(x is None for x in terms) else sum(terms, F(0))


def arithmetic():
    source = SOURCE.read_text()
    # Two independently expressed affine mappings, including fractional ticks.
    a, b, c0, p0, t0 = F(1, 20_000_000), F(1000001, 1000000), 7, F(11), F(23)
    for c in (0, c0, 20_000_007, F(31, 2)):
        phc = p0 + a * (c - c0)
        domain = t0 + b * (phc - p0)
        composed = t0 + b * a * (c - c0)
        require(domain == composed, "affine composition")
        require(c0 + (domain - t0) / (b * a) == c, "map inverse")
    # Inclusive toy intervals: an output must be covered by every dependency.
    intervals = [(F(0), F(20)), (F(3), F(18)), (F(5), F(19))]
    valid = max(x[0] for x in intervals), min(x[1] for x in intervals)
    require(valid == (5, 18), "intersection of map validity intervals")
    require(not (valid[0] <= 19 <= valid[1]), "late output must be rejected")
    require(max(1, 2, 4) <= 5 and not (max(1, 2, 6) <= 5),
            "causal publication fence")
    require(bound_sum([F(1, 10), None, F(2)]) is None, "unknown bound erased")
    require(bound_sum([F(1, 10), F(2)]) == F(21, 10), "bound sum")

    require(2 * F(100, 1_000_000) * 1000 == F("0.2"), "100 us motion in mm")
    require(2 * F(1, 1000) * 1000 == 2, "1 ms motion in mm")
    require(180 * F(100, 1_000_000) == F("0.018"), "angular error in degrees")
    transverse_mm = math.pi / 180 * 0.018 * 1000
    require(round(transverse_mm, 3) == 0.314, "one-metre lever arm")

    e0, rho = F(2, 10_000_000), F(5, 10_000_000)
    deadlines = []
    for budget, expected in ((F(10, 1_000_000), F("19.6")),
                             (F(1, 1_000_000), F("1.6"))):
        deadline = (budget - e0) / rho
        require(deadline == expected, "linear holdover deadline")
        require(e0 + rho * deadline == budget, "holdover boundary")
        require(e0 + rho * (deadline + F(1, 1000)) > budget, "expiry after boundary")
        deadlines.append(str(float(deadline)))
    with localcontext() as ctx:
        ctx.prec = 40
        # Work in microseconds: .2 + .5*t + .01*t*t = 10.
        alpha_deadline = (-Decimal(".5") + Decimal(".642").sqrt()) / Decimal(".02")
        require(abs(Decimal(".2") + Decimal(".5") * alpha_deadline
                    + Decimal(".01") * alpha_deadline**2 - 10) < Decimal("1e-35"),
                "quadratic holdover root")
        require(alpha_deadline.quantize(Decimal(".0001")) == Decimal("15.0625"),
                "rounded quadratic deadline")
        require("15.0625" in source, "quadratic example absent from paper")

    frame_100_us = F(256 * 8, 100_000_000) * 1_000_000
    frame_1000_us = F(256 * 8, 1_000_000_000) * 1_000_000
    require(frame_100_us == F("20.48"), "100 Mbit/s serialization")
    require(frame_1000_us == F("2.048"), "1 Gbit/s serialization")
    required = frame_100_us + 2 * (1 + 2)
    require(required == F("26.48") and 50 - required == F("23.52"), "window slack")
    require(frame_100_us + 2 * (15 + 2) == F("54.48") > 50, "failed window")
    require(1 + 1 == 2, "two absolute bounds imply pairwise sum")
    blocking_100_us = F(1542 * 8, 100_000_000) * 1_000_000
    blocking_1000_us = F(1542 * 8, 1_000_000_000) * 1_000_000
    require(blocking_100_us == F("123.36"), "100 Mbit/s blocking")
    require(blocking_1000_us == F("12.336"), "1 Gbit/s blocking")
    require(F(1, 1000) > F(5, 10000), "missed 1 ms cycle exceeds .5 ms deadline")

    upper = -math.expm1(math.log(0.05) / 1_000_000)
    require(abs(upper - 2.99572778635e-6) < 1e-17, "zero-violation binomial illustration")
    require(1_000_000 / 1000 == 1000, "trial duration")
    pilot_count = 30 * 60 * 1000
    qualified_minimum = F(999, 1000) * pilot_count
    require(pilot_count == 1_800_000 and qualified_minimum == 1_798_200,
            "candidate baseline opportunity denominator")
    require(pilot_count - qualified_minimum == 1800, "candidate missing allowance")
    # At 1 kHz, equally many missing interior opportunities can have very
    # different first-to-next-observed gaps if clustered instead of isolated.
    require(F(1800 + 1, 1000) > F(20, 1000) > F(1 + 1, 1000),
            "total missing allowance must not replace a gap limit")
    for literal in ("0.018", "20.48", "2.048", "26.48", "23.52", "54.48",
                    "123.36", "12.336", "2.996"):
        require(literal in source, "computed example absent from paper: " + literal)
    return {"linear_holdover_s": deadlines,
            "quadratic_holdover_s": str(alpha_deadline),
            "wire_occupancy_bytes": [256, 1542],
            "window_required_us": str(required), "window_slack_us": "23.52",
            "iid_only_95pct_violation_upper": upper,
            "million_trials_at_1khz_s": 1000,
            "candidate_pilot_opportunities": pilot_count,
            "candidate_pilot_qualified_minimum": int(qualified_minimum),
            "unknown_bound_preserved": True}


def populations():
    historical = json.loads((ROOT / "evidence/v14/2026-10-12/summary.json").read_text())
    h = historical
    counts = [h["reference_valid_causal"]["n"], h["invalid_reference"],
              h["reference_valid_expired"], h["unobserved_conservative"]]
    require(counts == [436, 65, 3, 98] and sum(counts) == h["nominal_boundaries"] == 602,
            "historical V14 opportunity accounting")
    require(h["all_causal"]["n"] == 501, "V14 all-causal count")
    require(round(h["reference_valid_causal"]["rms_including_bias_ns"], 3) == 62.504,
            "V14 conditional RMS")
    require(round(h["all_causal"]["rms_including_bias_ns"] / 1e6, 3) == 3.929,
            "V14 all-causal RMS in ms")
    s3 = json.loads((ROOT / "evidence/s3/2026-10-22/results.json").read_text())
    long = s3["cohorts"]["actual46993_fixed660s"]
    require(long["valid"] == 615 and long["invalid"] == 1, "S3 recorded populations")
    require(660 - long["valid"] - long["invalid"] == 44, "S3 nominal missing")
    require(round(long["rms_ns"], 3) == 110.801 and long["max_abs_ns"] == 402,
            "S3 conditional metrics")
    return {"v14_opportunities": 602, "v14_categories": counts,
            "s3_nominal_opportunities": 660, "s3_categories": [615, 1, 44],
            "populations_pooled": False}


def identity():
    edition = json.loads(EDITION.read_text())
    entries = edition["files"] + edition["cited_local_sources"]
    for entry in entries:
        path = ROOT / entry["path"]
        require(path.is_file(), "missing edition dependency: " + entry["path"])
        data = path.read_bytes()
        require(len(data) == entry["bytes"], "size mismatch: " + entry["path"])
        require(hashlib.sha256(data).hexdigest() == entry["sha256"],
                "hash mismatch: " + entry["path"])
    source = SOURCE.read_text()
    items = re.findall(r"\\bibitem\{([^}]+)\}", source)
    keys = set(items)
    require(len(keys) == len(items), "duplicate bibliography keys")
    cited = {key for group in re.findall(r"\\cite\{([^}]+)\}", source)
             for key in group.split(",")}
    require(not (cited - keys), "undefined bibliography keys")
    require(not (keys - cited), "uncited bibliography entries")
    require(len(keys) == edition["review"]["references"], "reference count")
    require(edition["status"] == "RESEARCH_DRAFT_NOT_SUBMITTED_NOT_PROMOTED_NOT_PUSHED",
            "unexpected edition status")
    return {"files_checked": len(entries), "references": len(keys),
            "status": edition["status"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arithmetic-only", action="store_true",
                        help="check examples/populations before sealing a new edition")
    args = parser.parse_args()
    result = {"arithmetic": arithmetic(), "populations": populations()}
    if not args.arithmetic_only:
        result["edition"] = identity()
    print(json.dumps(result, indent=2))
    print("PASS: offline robotics paper checks" +
          (" and edition identity" if not args.arithmetic_only else ""))


if __name__ == "__main__":
    main()
