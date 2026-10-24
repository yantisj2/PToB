#!/usr/bin/env python3
"""Verify the low-level ESP32-S3 draft's identities and stated arithmetic.

This checks selected aggregates and equations, not unavailable raw measurements.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib
import json
import math
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    summary = json.loads((ROOT / 'papers/supplements/esp32s3-low-level-2026-10-24.json').read_text())
    receipt = json.loads((ROOT / 'papers/editions/s3-low-level-2026-10-24.json').read_text())
    assert summary['license'] == 'CC-BY-4.0'
    assert summary['full_certification'] == 'FAIL'
    assert summary['absolute_UTC'] == summary['RF_bound'] == 'UNKNOWN'
    assert not summary['provenance']['empirical_statistics_recomputable_from_public_rows']
    receive = summary['receive_0_2_10']
    assert sum(receive['windows_s']) == receive['denominator'] == receive['eligible'] == 600
    assert receive['strict_below200'] == 600 and receive['strict_below100'] == 593
    assert not receive['final_window_refit']
    downstream = summary['downstream_0_2_10']
    assert downstream['eligible'] + downstream['unscorable'] == downstream['denominator_logged'] == 600
    path = summary['sampled_received_path']
    assert (path['selected_tx'], path['rx_seen'], path['followup_matched'], path['pps_scored']) == (120, 86, 66, 63)
    for row in (receive, downstream, path):
        assert math.isclose(row['rms_ns'] ** 2, row['mean_ns'] ** 2 + row['centered_std_ns'] ** 2, abs_tol=1e-6)
        assert abs(row['mean_ns']) <= row['rms_ns'] <= row['max_abs_ns']
    predictor = summary['predictor_0_2_7']
    assert predictor['independent_replay_per_model'] + predictor['initial_history_unavailable_for_replay'] == 600
    assert Fraction(2**32, 80_000_000) == Fraction(536870912, 10_000_000)
    xs = [2*j - 31 for j in range(32)]
    zs = [x*x - 341 for x in xs]
    assert sum(xs) == sum(zs) == sum(x*z for x, z in zip(xs, zs)) == 0
    # Recover known counter curvature using the paper's orthogonal basis.
    ys = [Fraction(7) + Fraction(3)*j + Fraction(1, 8)*j*j for j in range(32)]
    b = sum(x*y for x, y in zip(xs, ys)) / sum(x*x for x in xs)
    c = sum(z*y for z, y in zip(zs, ys)) / sum(z*z for z in zs)
    assert (sum(ys)/32 - 31*b + 620*c, 2*b - 124*c, 4*c) == (7, 3, Fraction(1, 8))
    assert Fraction(40_000, 50) == 800  # LE1M nominal start-to-ADDRESS ticks.
    for item in receipt['files']:
        data = (ROOT / item['path']).read_bytes()
        assert len(data) == item['bytes'], item['path']
        assert hashlib.sha256(data).hexdigest() == item['sha256'], item['path']
    assert receipt['page_count'] == 7 and receipt['review_status'] == 'DRAFT_FOR_USER_REVIEW'
    print('PASS: selected identities, denominators, RMS decomposition, counter period, orthogonal curvature equations and endpoint units')
    print('LIMIT: aggregate-only new evidence; no raw empirical recomputation or RF/UTC certification')


if __name__ == '__main__':
    main()
