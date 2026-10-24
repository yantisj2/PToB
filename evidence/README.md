# Claim and evidence index

These dated collections support review of specific experiments. They are not a
single pooled benchmark or a statement of current firmware acceptance.

| Claim or question | Evidence | Boundary |
| --- | --- | --- |
| Current W12 delivered clock, UTC-basis repair and downstream TX baseline | [October 24 W12 baseline](s3/2026-10-24-w12-utc-basis/README.md) | 522/600 W12 PPS eligible, 60.328 ns RMS / 180 ns maximum; downstream 451/600 and 211 ns maximum; sampled TX 58/120 and 226 ns maximum. Availability and calibrated UTC/output gate remain open |
| ESP32-S3 empirical fine capture and completed-window tails | [October 22 fine follow-up](s3/2026-10-22-fine/README.md) | 657 eligible / 660 nominal opportunities, three invalid, no nominal missing; actual 166.590 ns RMS / 1591 ns maximum, all tails retained; empirical RX, estimated TX and output target unqualified |
| ESP32-S3 relative timing and the gap to Nordic event timestamps | [October 22 S3 study](s3/2026-10-22/README.md) | Incomplete fixed 660-second attempt, 615 valid / 660 nominal opportunities; disabled capture ending and separate historical Nordic population; raw 80 MHz activity is not a qualified RF timestamp |
| Awake training-window budget and potential wake/listen power tradeoffs | [October 19 PC117 pilot](power/2026-10-19/README.md) | Six separate runs, all windows and missing observations; RF stayed on, energy/drift curves hypothetical, sleep and calibrated output pending |
| Current BLE-derived RC versus USB Direct timing | [October 17 USB comparison](usb/2026-10-17/README.md) | Fixed shared-bus window; sanitized observations, separate RX/chrony/availability populations; clean-bus and independent UTC validation pending |
| Current admission/continuity fixes and unresolved USB blackouts | [October 16 stability engineering](stability/2026-10-16/README.md) | Separate v63/v67/v68/v69 and W12 observations; failures retained; no durability or UTC qualification |
| USB/chrony delivery, oscillator-extension research and system viability | [October 15–16 research](research/2026-10-16/README.md) | Shared-source agreement; separate historical runs; hypothetical scaling models |
| V14 boundary-clock regeneration, prospective one-RC phase, and renewal | [October 12 V14 research](v14/2026-10-12/README.md) | 504 observed edges with complete 602-opportunity accounting; fractional PPS phase, not calibrated UTC or output |
| Prospective edge calibration and causal publication in the October 2 papers | [Historical paper collection](papers/2026-10-02/README.md) | PPS-shadow, internal compare, and W12 predictions retain different methods and populations |
| Software-map convergence and native lock in the October 3 follow-up | [PLL collection](papers/oscillator-pll-2026-10-03/README.md) | Hardware-derived errors and sampled states; not quartz/carrier or electrical-output qualification |
| v12 behavior under declared scheduling, loss, and clock assumptions | [Numerical model](v12/model/README.md) | Synthetic inputs; historical model differs from R2 firmware |
| R2 control/service and P1 behavior in a controller/PHY simulator | [R2 simulation](v12/r2/simulation/README.md) | Synthetic clocks/sources; failed P1 campaign retained |
| What the selected T096 hardware campaigns actually observed | [R2 hardware](v12/r2/hardware/README.md) | Finite installation/startup/UI/service observations; open precision and continuity limits retained |

Each collection preserves its own protocol/profile, source/image identities,
denominators, failures, and unknowns. A build result inside a hardware campaign
record remains a build result. A reviewed simulation remains simulation.

## Integrity and reproduction

Run from the repository root with Python 3.9 or newer:

```sh
python3 tools/verify_evidence.py
python3 tools/verify_v14.py
python3 tools/verify_research.py
python3 tools/verify_stability.py
python3 tools/verify_usb_results.py
python3 tools/verify_power.py
python3 tools/verify_s3_fine_paper.py
python3 tools/verify_w12_utc_results.py
```

The previous S3 edition's source/PDF verifier runs at its recorded historical
revision; its immutable manifest binds the previous bytes. The fine follow-up
verifier checks the current selected data and matching edition.

This checks all imported evidence files against the [import manifest](imports/2026-10-09.json)
and recomputes seven PLL capture populations from the included derived samples.
The V14 command checks its [separate receipt](../papers/imports/v14-2026-10-13.json),
recomputes both PPS populations and diagnostic deltas, and verifies printed
proposal tables/examples. Its [verification record](v14/2026-10-12/verification-2026-10-13.json)
separates fresh checks from historical C, raw-extraction, and hardware results.
The v12 model has its own [standalone test instructions](v12/model/README.md).
The stability command checks the selected public summary hashes and aggregate
accounting; private raw logs are excluded, so physical extraction is not reproduced.
The [local verification record](imports/verification-2026-10-09.json) distinguishes
checks rerun for this import from historical results.

The import manifest records original and public hashes, selection, and every
redaction. Local silicon identifiers, private checkout prefixes, and one raw
console fragment were removed; numerical results, cohort aliases, and incomplete
record denominators were retained. Original sealed archives were not modified.

Paths inside historical records are **archive provenance locators**, not live
links or assertions that those files are included. Original code hashes and seal
verification outcomes are preserved as historical records; this import did not
reverify private archives. Raw console/RF/instrument captures, enrolled images,
and the full R2 build environment are not included. Consequently, reviewers can
inspect summaries and reproduce the documented subset, but cannot independently
reconstruct every physical experiment from this checkout.

Original evidence records and included derived CSV datasets are licensed under
[CC BY 4.0](LICENSE.md). Executable model and verification code uses MIT.
