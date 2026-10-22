#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Yantis.
"""Verify analytical examples and edition identity; no device/protocol access.

The drop-frame functions are reference arithmetic for the paper's nominal
30-count label example, not an LTC/MTC or vendor protocol implementation.
"""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
EDITION = ROOT / "papers/editions/bluetooth-timecode-2026-10-22.json"
DF_DAY = 2_589_408


def require(condition, message):
    if not condition:
        raise ValueError(message)


def ordinal(label):
    """Equation 5, rejecting labels omitted by nominal 30-count DF."""
    h, m, s, f = label
    require(0 <= h < 24 and 0 <= m < 60 and 0 <= s < 60 and 0 <= f < 30,
            "label outside nominal day")
    require(not (m % 10 and s == 0 and f < 2), "skipped DF label")
    total_minutes = 60 * h + m
    return 30 * (3600 * h + 60 * m + s) + f - 2 * (
        total_minutes - total_minutes // 10)


def label_at(n):
    """Independent construction: ten-minute blocks and minute lengths."""
    require(0 <= n < DF_DAY, "ordinal outside nominal day")
    ten_minute, remainder = divmod(n, 17_982)
    if remainder < 1800:
        minute_in_block = 0
        nominal_index = remainder
    else:
        minute_in_block, nominal_index = divmod(remainder - 1800, 1798)
        minute_in_block += 1
        nominal_index += 2
    total_minutes = ten_minute * 10 + minute_in_block
    h, m = divmod(total_minutes, 60)
    s, f = divmod(nominal_index, 30)
    return h, m, s, f


def arithmetic():
    rate = Fraction(30_000, 1001)
    require(1 / rate == Fraction(1001, 30_000), "frame period")
    require(Fraction(60_000, 1001) / rate == 2, "59.94 coordinate rate")
    require(Fraction("29.97") != rate, "rounded decimal differs")
    require(Fraction(48_000) / rate == Fraction(8008, 5), "sample ratio")
    require(Fraction(48_000) * 5 / rate == 8008, "five-frame samples")
    require((Fraction(-1, 1_000_000_000) * rate).__floor__() == -1,
            "negative floor convention")
    transitions = 0
    rejected = 0
    for total_minutes in range(1440):
        h, m = divmod(total_minutes, 60)
        first_frame = 2 if m % 10 else 0
        first = (h, m, 0, first_frame)
        n = ordinal(first)
        require(label_at(n) == first, "minute-start inverse")
        if n:
            previous_minute = total_minutes - 1
            ph, pm = divmod(previous_minute, 60)
            require(label_at(n - 1) == (ph, pm, 59, 29), "minute predecessor")
        for candidate in range(max(0, n - 2), min(DF_DAY, n + 3)):
            require(ordinal(label_at(candidate)) == candidate, "boundary roundtrip")
        if first_frame:
            for f in (0, 1):
                try:
                    ordinal((h, m, 0, f))
                except ValueError:
                    rejected += 1
                else:
                    raise ValueError("skipped label accepted")
        transitions += 1
    require(label_at(DF_DAY - 1) == (23, 59, 59, 29), "final day label")
    require(DF_DAY / rate == Fraction(53_999_946, 625), "DF day duration")
    require(Fraction(86400) - DF_DAY / rate == Fraction(54, 625), "DF day mismatch")
    require(ordinal((0, 1, 0, 2)) == 1800, "first dropped minute")
    require(ordinal((0, 10, 0, 0)) == 17_982, "tenth minute")
    # An event 50 ns before the first frame boundary, bounded by +/-100 ns.
    boundary = 1 / rate
    estimate = boundary - Fraction(50, 1_000_000_000)
    uncertainty = Fraction(100, 1_000_000_000)
    require(((estimate - uncertainty) * rate).__floor__() == 0, "lower ordinal")
    require(((estimate + uncertainty) * rate).__floor__() == 1, "upper ordinal")
    drifts = []
    for ppm, expected_ms in (("0.5", "43.2"), ("0.2", "17.28"), ("0.1", "8.64")):
        drift_ms = Fraction(ppm) * Fraction(86400, 1000)
        require(drift_ms == Fraction(expected_ms), "constant-rate drift")
        drifts.append({"ppm": ppm, "24h_ms": expected_ms})
    require((Fraction(1, 1000) - Fraction(2, 10_000)) /
            Fraction(5, 10_000_000) == 1600, "hypothetical holdover deadline")
    require(64 * 10 * 6 == 3840, "hypothetical payload bytes")
    return {"minute_boundaries": transitions, "skipped_labels_rejected": rejected,
            "df_frames_per_nominal_day": DF_DAY, "df_day_seconds": "86399.9136",
            "df_day_shortfall_ms": "86.4", "constant_rate_examples": drifts,
            "sample_ratio": "8008/5", "boundary_candidate_ordinals": [0, 1],
            "hypothetical_holdover_s": 1600, "hypothetical_payload_Bps": 3840}


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
    source = (ROOT / "papers/ptob-bluetooth-timecode-preprint.tex").read_text()
    keys = set(re.findall(r"\\bibitem\{([^}]+)\}", source))
    cited = set()
    for group in re.findall(r"\\cite\{([^}]+)\}", source):
        cited.update(group.split(","))
    require(not (cited - keys), "undefined bibliography keys")
    require(len(keys) == edition["review"]["references"], "reference count")
    require(edition["status"] == "RESEARCH_DRAFT_NOT_SUBMITTED_NOT_PROMOTED_NOT_PUSHED",
            "draft status changed without an editorial decision")
    return {"files_checked": len(entries), "references": len(keys),
            "status": edition["status"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arithmetic-only", action="store_true",
                        help="check examples before a new edition receipt is finalized")
    args = parser.parse_args()
    result = {"arithmetic": arithmetic()}
    if not args.arithmetic_only:
        result["edition"] = identity()
    print(json.dumps(result, indent=2))
    print("PASS: offline analytical checks" + (" and edition identity" if not args.arithmetic_only else ""))


if __name__ == "__main__":
    main()
