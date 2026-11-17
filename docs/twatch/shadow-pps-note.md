# T-Watch Shadow PPS device check

17 November 2026 · One LILYGO T-Watch Ultra V1 / SX1262 test unit.

Shadow observes the BLE software clock at independently captured GNSS PPS
edges. The receiver achieved a coherent GNSS fix and recurring physical PPS.
After phase-blind ten-second settling, the largest completed population in a
three-minute run contained **14 samples**:

| Baseline metric | Observed value |
| --- | ---: |
| Signed MEAN | −2.9104 µs |
| P99 absolute error | ~3.071 µs |
| RMS | 2.911 µs |

The tested unit's local fine-input profile applies a provisional **+2.900 µs**
fixed adjustment, rounded to its 50 ns coordinate step. The tested 0.1.9 image
supplied the baseline; its 0.1.10 image uses the adjusted local profile. The
correction is not a public default or a constant for every 0.1.10 build. Both
application-only installs
passed complete application readback and protected-prefix/final-sector checks.
This is a fixed empirical adjustment; GNSS remains a read-only witness.

**Residual validation is pending.** No valid GNSS/PPS comparison population was
available during the three-minute post-flash check. Arithmetic cancellation of
the training mean is not a measured residual.

The run included changing BLE availability and restarted populations. The
14-sample cohort was selected by sample count, without phase trimming; other
windows and failures remain in the retained local diagnostics. `~` denotes the
coarse histogram tail. Firmware kept the selected cohort in one context, but
exact per-edge GM identities were not exported. The derivation's saved source
checkpoint is a later RC1 observation, distinct from the direct-BLE cohort.
This short result does not qualify P99 tails, separate
receiver/GNSS/path bias, or establish independent output or UTC accuracy.

[Selected aggregate record](shadow-pps-2026-11-17.json) ·
[Controls and limits](../../firmware/twatch/README.md#shadow-pps)

Copyright (c) 2026 Jonathan Yantis. Documentation: MIT; selected measurement
record: CC BY 4.0.
