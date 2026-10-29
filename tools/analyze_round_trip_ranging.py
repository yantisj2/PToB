#!/usr/bin/env python3
"""Synthetic arithmetic for the proposed RC ranging study; no hardware access.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import json
import math
import random
import statistics

C_M_PER_NS = 0.299792458
Q_NS = 62.5


def ds_tof(a, b):
    """A: poll TX, response RX, final TX; B: poll RX, response TX, final RX.

    Inputs are durations in a common nominal unit, with independent clock
    origins. Production code needs bounded integer products and capture fences.
    """
    ra, da = a[1] - a[0], a[2] - a[1]
    db, rb = b[1] - b[0], b[2] - b[1]
    if min(ra, da, db, rb) <= 0:
        raise ValueError("nonpositive interval")
    return (ra * rb - da * db) / (ra + rb + da + db)


def stamps(tau, db, da, scale_a=1.0, scale_b=1.0):
    a = (0, 2 * tau + db, 2 * tau + db + da)
    b = (tau, tau + db, 3 * tau + db + da)
    return ([v * scale_a + 125000 for v in a],
            [v * scale_b + 875000 for v in b])


def analyze():
    checks = 0
    for distance in (1, 10, 30, 100, 300):
        tau = distance / C_M_PER_NS
        for db, da in ((150000, 150000), (1000000, 2000000),
                       (7500000, 62500000)):
            for ka, kb in ((1, 1), (1.00002, 0.99998), (1.000001, 1)):
                a, b = stamps(tau, db, da, ka, kb)
                # Exact affine-clock residual; no reply-delay-amplified error.
                expected = tau * 2 * ka * kb / (ka + kb)
                assert math.isclose(ds_tof(a, b), expected, abs_tol=1e-7)
                checks += 1

    # Balanced DS linearization coefficients for six independent latches.
    a, b = stamps(100, 1000000, 1000000)
    derivatives = []
    for i in range(6):
        plus, minus = list(a + b), list(a + b)
        plus[i] += 0.01
        minus[i] -= 0.01
        derivatives.append((ds_tof(plus[:3], plus[3:]) -
                            ds_tof(minus[:3], minus[3:])) / 0.02)
    sigma_ds_ns = Q_NS / 4
    numerical_sigma = Q_NS / math.sqrt(12) * math.sqrt(
        sum(v * v for v in derivatives))
    assert math.isclose(numerical_sigma, sigma_ds_ns, rel_tol=1e-6)

    # Independent uniform errors are an idealization, not MCU capture behavior.
    rng = random.Random(20261029)
    noise = []
    for _ in range(20000):
        x = [v + rng.uniform(-Q_NS / 2, Q_NS / 2) for v in a + b]
        noise.append(ds_tof(x[:3], x[3:]) - 100)
    assert abs(statistics.mean(noise)) < 0.5
    assert abs(statistics.pstdev(noise) - sigma_ds_ns) < 0.5

    # A perfectly repeated phase retains its bias even after many repetitions.
    tau = 30 / C_M_PER_NS
    a, b = stamps(tau, 1000000, 2000000)
    a = [math.floor((v + Q_NS * 0.3) / Q_NS) * Q_NS for v in a]
    b = [math.floor((v + Q_NS * 0.7) / Q_NS) * Q_NS for v in b]
    repeated_m = ds_tof(a, b) * C_M_PER_NS
    assert abs(repeated_m - 30) > 1
    assert statistics.mean([repeated_m] * 256) == repeated_m

    return {
        "status": "PASS_SYNTHETIC_ARITHMETIC_ONLY",
        "affine_clock_cases": checks,
        "tick_ns": Q_NS,
        "one_way_metres_per_tick": Q_NS * C_M_PER_NS,
        "half_round_trip_metres_per_tick": Q_NS * C_M_PER_NS / 2,
        "balanced_ds_ideal_sigma_ns": sigma_ds_ns,
        "ideal_averaging": [
            {"independent_rounds": n,
             "sigma_ns": sigma_ds_ns / math.sqrt(n),
             "sigma_m": sigma_ds_ns * C_M_PER_NS / math.sqrt(n)}
            for n in (1, 16, 32, 64, 128)],
        "iid_monte_carlo": {"samples": len(noise),
                            "mean_ns": statistics.mean(noise),
                            "sigma_ns": statistics.pstdev(noise)},
        "repeated_phase_counterexample": {
            "true_m": 30, "estimate_m_at_1_and_256_rounds": repeated_m},
        "single_sided_1ppm_error": [
            {"turnaround_ms": ms, "error_ns": ms / 2,
             "error_m": ms / 2 * C_M_PER_NS}
            for ms in (1, 10, 100)],
        "persistent_delay_floor_m": [
            {"bias_ns": ns, "bias_m": ns * C_M_PER_NS}
            for ns in (10, 20, 30)],
        "two_independent_20ns_pps_sigma_ns": math.sqrt(2) * 20,
        "two_pps_plus_independent_16MHz_capture_sigma_ns": math.sqrt(
            2 * 20**2 + 2 * Q_NS**2 / 12),
        "raw_32bit_wrap_seconds": 2**32 / 16000000,
        "limits": "No measured RF noise, distance, scheduling or hardware acceptance."
    }


if __name__ == "__main__":
    print(json.dumps(analyze(), indent=2))
