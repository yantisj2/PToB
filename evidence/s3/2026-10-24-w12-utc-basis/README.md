# W12 baseline after the T096 UTC-basis repair

24 October 2026 · Jonathan Yantis · hardware observations, relative timing.
This collection supersedes older W12 figures for the current website baseline.
It does not replace the distinct October 22 N30 experiment or the approved
seven-page [implementation paper](../../../papers/ptob-esp32s3-low-level-timing.pdf).
That paper retains its earlier, explicitly dated aggregate populations.

After a declared 120-second warmup, two 300-second windows used W12 0.2.10
(`b5ecbb0`) and T096 0.15.169 (`1a1fa96`). The GNSS-enabled W12 followed BLE;
its GNSS PPS was a witness and did not train the BLE clock. A T096 receiver
followed the W12 relay. The +7671 ns receive, +4026 ns relay and existing
+10543 ns downstream receive adjustments were frozen before these windows.
Their transfer to other units/profiles has not been measured.

| Population | Eligible / logged or selected | RMS including bias | Mean | Absolute P99 / maximum |
| --- | --- | --- | --- | --- |
| W12 delivered PPS phase, both windows | 522 / 600 | 60.328 ns | +40.695 ns | 156 / 180 ns |
| W12 stable second window | 300 / 300 | 38.312 ns | +20.737 ns | 97 / 121 ns |
| Downstream T096 delivered PPS phase | 451 / 600 | 61.922 ns | −5.545 ns | 154 / 211 ns |
| Sampled emitted timestamps at downstream RX | 58 / 120 | 108.306 ns | +13.931 ns | 226 / 226 ns |

Window 1 contains 182 eligible observations on source-A, 78 source-unavailable
records, then 40 eligible on source-B. Window 2 contains 300 on source-B.
All 78 W12 invalids remain. The downstream population retains 109 no-map,
36 expired and four reference-ineligible records. Selected TX retains 61
missing follow-up matches and one without a PPS bracket. The diagnostic radio
record sequence also has gaps; this is not a claim of loss-free RF delivery.
No PPS log sequence gaps were observed in the 600 logged records per device;
counter accounting alone cannot prove every physical pulse.

The repair chooses an integer-second UTC **offset**, preserving each timestamp's
nanosecond fraction: `UTC_ns = relative_ns + K × 1,000,000,000`. All 522 eligible
W12 records have a zero fractional offset and identical relative/UTC phases.
The old −28,835,875 ns basis contamination is absent. This does not round each
timestamp to a second. Empirical downstream NMEA-label agreement is retained
in the CSV; calibrated UTC association and absolute UTC accuracy remain unknown.

The separate GNSS predictor has 600/600 raw and mapped predictions, with
35.217 and 41.853 ns RMS respectively. It evaluates a prior GNSS model;
these figures do not measure delivered BLE time or improve its denominator.
Future PPS brackets in sampled-TX analysis are used only for offline measurement.

The timing gate remains **NOT MET**: the downstream has a 211 ns tail, sampled
TX a 226 ns tail, and availability gaps remain. No W12 GM/HOT work began.
RF endpoint uncertainty, GNSS/GPIO/cable delays, physical W12 output-pin timing
and independently calibrated UTC remain unqualified.

## Public reproduction and provenance

```sh
python3 tools/verify_w12_utc_results.py
```

Run from the repository root with Python 3.9+. The verifier recomputes means,
uncentered RMS, population standard deviation, nearest-rank absolute P95/P99,
threshold counts, every window/source and eligibility denominator from the
selected CSVs. Missing phases are blank, never zero. No fit, centering or tail
deletion is applied.

[manifest.json](manifest.json) records the explicit selection, transformations
and SHA-256 seals; [results.json](results.json) contains full aggregates and
limits. All selected PPS, TX and prediction rows are retained in their CSVs.
Source identities become stable aliases. Factory/boot/group/session identities,
absolute epochs/counters, local USB paths and unrelated console records are
omitted. Complete CRC-framed streams were validated locally; private raw streams
are excluded, so public verification reproduces the numerical analysis rather
than the original raw extraction, hardware capture or calibration.

Original derived CSV/JSON evidence: **CC BY 4.0**, credit Jonathan Yantis; see
[evidence license](../../LICENSE.md). Ordinary documentation and verifier: MIT.
