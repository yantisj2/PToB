# Claim and evidence index

These dated collections support review of specific experiments. They are not a
single pooled benchmark or a statement of current firmware acceptance.

| Claim or question | Evidence | Boundary |
| --- | --- | --- |
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
```

This checks all imported evidence files against the [import manifest](imports/2026-10-09.json)
and recomputes seven PLL capture populations from the included derived samples.
The V14 command checks its [separate receipt](../papers/imports/v14-2026-10-13.json),
recomputes both PPS populations and diagnostic deltas, and verifies printed
proposal tables/examples. Its [verification record](v14/2026-10-12/verification-2026-10-13.json)
separates fresh checks from historical C, raw-extraction, and hardware results.
The v12 model has its own [standalone test instructions](v12/model/README.md).
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
