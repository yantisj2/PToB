# V14 successor research — 12 October 2026

This collection accompanies the [V14 research preprint](../../../papers/ptob-v14-preprint.pdf)
and [V14-RC1 proposal](../../../papers/ptob-v14-sig-proposal.pdf).
Evidence cutoff: **2026-10-12T18:26:44.293859Z**. Selected historical application:
**0.14.43**, source `566ea2029c7ffc84a7d321df427c5c811fac2730`.
The specification freeze is `7995249`. Later firmware is outside this cutoff.

## Claims and populations

| Claim or question | Included evidence | Limit |
| --- | --- | --- |
| Prospective GM → one RC → leaf phase | [All 504 observed edges](pps-sanitized.csv), [summary](summary.json), [result table](result-table.csv) | A causal software map evaluated at local physical PPS; signed modulo-second phase, not calibrated UTC or a measured output pin |
| Availability of that phase measurement | Validity/map flags, gap lengths, and [phase/availability figure](phase-availability.pdf) | 602 conservative nominal boundaries; no interpolation across missing observations |
| Renewal and expiry behavior | [Sanitized diagnostic samples](diagnostic-sanitized.csv), [renewal/expiry figure](renewal-expiry.pdf) | Sampled counters; no transition-complete duration or continuous-service guarantee |
| Per-link offset calibration | [Research provenance](ptob-v14-evidence.json) and summary | +10,543 ns fixed before validation; training samples/raw inputs are absent, so calibration cannot be independently regenerated here |
| Portable wire description | [Frozen contract/vectors](../../../protocol/v14_rc1/README.md), [wire table](wire-layouts.csv) | Document/HMAC checks are offline; current C decoder and stateful conformance are not included |
| Remaining normative gaps | [Issue record](provenance/ptob-v14-sig-issues.json) | Historical GR3 decoder accepted duplicate and nonpreceding corrections contrary to RC1 requirements |
| Literature and document identity | [Research literature](provenance/ptob-v14-literature.json), [proposal literature](provenance/ptob-v14-sig-literature.json), original build/review receipts | Historical retrieval/review records, not a new literature audit |

The observed phase populations stay separate:

- **436 reference-valid causal samples**: signed mean −18.417431 ns,
  RMS including bias 62.503871 ns, demeaned jitter 59.728822 ns,
  absolute P95/P99/maximum 123.25/160.25/191 ns.
- **501 all-causal samples**, including 65 invalid-reference observations:
  RMS 3,929,463.788052 ns and maximum 19,752,827 ns. Reference-invalid excursions
  are retained rather than pooled into the reference-valid estimate.
- **3 reference-valid edges with expired maps** and **98 conservative unobserved
  opportunities** complete the accounting: **602 = 436 + 65 + 3 + 98**.
  Usable coverage is 72.425%; observation span is 601.5761865 s and maximum
  observed witness gap is 59.0171903125 s. The lower 601-boundary convention
  would give 97 unobserved opportunities; the conservative convention is used here.

RC and leaf sampled interval counters retain six and eleven expiry/recovery
increments respectively, while admission receipts advance 1→3. Historical
post-run records report clocks left running after collection. Sparse observations
do not prove every intervening instant met the clock policy.

## Reproduce from this checkout

Python 3.9+ and the standard library suffice:

```sh
python3 tools/verify_v14.py
```

This verifies the [33-file import receipt](../../../papers/imports/v14-2026-10-13.json),
recomputes both PPS populations and linear-interpolation quantiles, checks full
denominators/gaps and sampled counter deltas, and checks all 181 wire fields and
six printed examples against the proposal and HMAC domains. The recorded
14-valid/19-rejected C/ASan/UBSan run is historical; it was not rerun for this
import and is not a current firmware-conformance pass.

Optional scientific figure regeneration uses Python 3.12+ and public packages. From the
repository root, install NumPy 2.5.4 and Matplotlib 3.11.2 in a local environment
and choose a fresh output directory:

```sh
python3 -m venv artifacts/v14-figures-venv
artifacts/v14-figures-venv/bin/python -m pip install -r tools/v14-figures-requirements.txt
artifacts/v14-figures-venv/bin/python tools/derive_v14_figures.py --out artifacts/v14-figures
```

The public adapter regenerates vector/PNG/PDF figures and the result table from
the included sanitized inputs, writing a new derivation manifest into the output
directory. It has no raw-extraction mode and never rewrites reviewed evidence.
Other package/font versions may change figure bytes. The manuscripts themselves
embed vector graphics and bibliographies and need no external figure files.
The [import verification record](verification-2026-10-13.json) distinguishes
checks rerun here from historical raw/source/seal verification.

## Provenance, omissions, and rights

Selected exports and historical receipts were imported byte for byte. Paths under
`paper/`, `artifacts/`, and `firmware/` within those receipts are original archive
locators, not claims that the referenced inputs are included. Their old PDF/hash
inventories and `local_only`/review statuses are preserved. The historical PLL PDF
hash describes the laboratory's preceding export; this repository's separately
reconciled edition is documented in [the PLL receipt](../../../papers/imports/oscillator-pll-2026-10-09.json).

The original derivation manifest binds private input hashes. Those inputs and
the original extraction step are unavailable here. Imported rows omit factory
identifiers, boots, absolute operational timestamps, enrollment, and console
records; numerical results, classifications, and gaps remain. Raw captures,
enrolled images, rollback binaries, private checkout/history, and ZIP review
packages were excluded. Seals establish identity, not calibration or durable backup.
Total reference uncertainty, calibrated electrical output, absolute UTC,
all-second precision, physical two-RC performance, and resource advantages remain
unqualified.

Original evidence, CSVs, and scientific figures/LaTeX exports are **CC BY 4.0**,
attributed to **Jonathan Yantis**; see [the evidence license](../../LICENSE.md).
Reproduction code is **MIT**. The separate contract and synthetic vectors use MIT;
their public test key is fixture material, never deployment enrollment.
