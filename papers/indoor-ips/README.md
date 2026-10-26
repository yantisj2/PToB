# PToB-IPS theoretical research draft

Jonathan Yantis · 26 October 2026 · approved for public draft review.

[Read the PDF](main.pdf), [edit the LaTeX](main.tex), inspect the
[analytical model output](analysis.json), or review the
[preparation receipt](../editions/indoor-ips-2026-10-26.json) and
[publication approval](../editions/indoor-ips-2026-10-26-publication.json).

This new paper develops a local constellation of surveyed PToB timing beacons.
A receiver uses corrected physical TX times and native RX captures to solve
jointly for position and receiver clock state. Beacons align their periodic
advertising timers to a common epoch with assigned offsets and related repetition
rates. One receiver maintains several train synchronizations and tunes to their
predicted channels in separate receive windows. The illustrative 16/8/4 Hz plan
repeats after 250 ms; admission includes airtime, early/late guards, retuning and
the cyclic boundary. A software clock update alone does not move the physical
advertising timer.

The study explores coarse IPS first, then finer clocks, calibrated RF events and
future phase/baseband capabilities for robot and drone navigation. It quantifies
random averaging, persistent bias, correlated clocks, geometry and motion.
UWB appears only as an optional intermediate service/reference. No sensor-radio
purchase or UWB integration is required by the proposed baseline.

All IPS architecture, timer alignment and accuracy examples remain theoretical.
The public relative-timing collections keep their own populations and limits.
The room calculation assumes ideal survey, linearized geometry and hypothetical
noise; it is not a measured range/position or a forecast from existing PPS RMS.
Large Bluetooth shipment volumes do not establish a count of compatible MCUs.

From the repository root, with public Tectonic and Python 3.9+:

```sh
mkdir -p artifacts/indoor-ips-build
tectonic --outdir artifacts/indoor-ips-build papers/indoor-ips/main.tex
python3 tools/verify_indoor_ips_paper.py
```

The source embeds its bibliography and vector figures. Tectonic may fetch public
typesetting resources on first use. The build writes
`artifacts/indoor-ips-build/main.pdf`; inspect the rebuilt PDF before replacing
the matching review copy. Other engines or bundles can change bytes/pagination.
Local preparation uses Tectonic 0.17.0 and all-page Poppler visual review.

The standard-library verifier checks the preserved model output, nonlinear
noiseless solver examples, a clock gauge and persistent-bias counterexample,
covariance/rank, quantization, averaging, schedule admission and selected W12/N30
CSV statistics. It verifies the immutable prepared edition and the approved publication files.
The earlier sensor-fusion draft remains unpublished and is not required in a
public checkout; its preservation hashes remain historical preparation metadata.
No hardware or private input is opened. To independently regenerate analytical JSON without overwriting its
preserved copy:

```sh
python3 tools/verify_indoor_ips_paper.py --write-analysis artifacts/indoor-ips-rebuilt.json
```

The existing collection checks remain useful:

```sh
python3 tools/verify_w12_utc_results.py
python3 tools/verify_s3_fine_paper.py
```

Jonathan Yantis approved this IPS paper for integration into main and GitHub
public review on 26 October 2026. The approved manuscript, PDF and analytical
JSON bytes are unchanged from the reviewed edition. The separate publication
receipt records that approval and the README/verifier adaptations needed for
standalone public verification. Statements about pending approval in the frozen
manuscript and preparation receipt describe the preparation checkpoint.

The paper remains a research draft, without stable promotion or submission.
The earlier sensor-fusion draft and all T096 branch development remain local.
This publication includes no firmware change or hardware operation.

Original manuscript, LaTeX, vector figures and analytical JSON: **CC BY 4.0**,
credit Jonathan Yantis. The JSON is explicitly analytical model output, not a
new evidence dataset. This README and executable analysis: **MIT**. Existing
evidence and cited third-party material retain their recorded terms; see
[repository licensing](../../LICENSING.md).
