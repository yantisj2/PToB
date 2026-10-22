# ESP32-S3 timing: selected derived evidence

Research draft, 22 October 2026. Jonathan Yantis.

This collection accompanies *Precision Bluetooth Timing on ESP32-S3: Causal
Clock Discipline and Hardware Event Capture*. The production S3 clock uses a
20 MHz GPTimer, 500 ns controller-descriptor receive coordinates and estimated
relay transmit timing. The separate 80 MHz native observer was OFF in the
prospective PPS experiments. Its earlier digital activity observations do not
qualify an RF packet endpoint.

The fixed 660 s experiment retains its original **FAILED** status. Its 615
eligible PPS observations have mean 42.346 ns, RMS 110.801 ns and maximum
absolute residual 402 ns over a 614.000253 s first-to-last eligible span. One
additional row is ineligible; 44 nominal one-second opportunities are
unrecorded. Collection became disabled, and the required post-window sequence
fence was absent. This incomplete ending does not establish physical GNSS/PPS
failure. Nominal accounting is **660 = 615 valid + 1 invalid + 44 unrecorded**;
the 93.182% eligible fraction is an opportunity model, not independently counted
physical pulses or continuous service availability.

## Files and populations

| File | Contents and limits |
| --- | --- |
| `pps-sanitized.csv` | Every selected recorded PPS residual with validity flags and exact relative 16 MHz hardware-counter coordinates; separate calibration, reserved check, actual 46,965 ns training, fresh 46,993 ns validation and fixed 660 s validation cohorts |
| `internal-quality-sanitized.csv` | Sampled recent-history causal RMS and state; context and in-window populations stay distinct; these are not independent PPS errors or whole-run RMS |
| `results.json` | Recomputed signed mean, uncentered RMS, population standard deviation, nearest-rank absolute quantiles, maxima, counts and descriptive block statistics |
| `protocol.json` | Fixed selection rules, failures, missing/terminal/transport counters, profile/source identity and limitations |
| `historical-nordic.json` | Explicit selected public October 12 V14 summary; underlying rows remain in `evidence/v14/2026-10-12/pps-sanitized.csv` |
| `capture-chain.json` | Synthetic/native digital observer aggregates, signal grids and export losses; native 112/1262 is an exported callback subset with physical completeness unknown |
| `source-context.json` | Pinned public source identities; source hashes establish identity, not physical accuracy |
| `primary-sources.json` | Dated primary literature/manufacturer retrieval scope and explicit versions |
| `manifest.json` | Final selected-file hashes and transformation receipts |

The nominal first 32 training and next 32 reserved observations share the first
calibration edge as their time origin. Later short cohorts use their own first
selected edge; the fixed 660 s cohort uses its immutable native-counter window
start. Absolute UTC times, device/boot identifiers, enrollment and private paths
are omitted. The original immutable scope equality is retained as `same_scope`.

The first 32 observations select an additional 6965 ns. Applying that constant
to the reserved 32 is a **counterfactual**, not corrected hardware observation.
The actual 46,965 ns first 48 were reused to select an additional 28 ns, yielding
46,993 ns, so they are training history for the final profile. The fresh 48 and
fixed long cohort receive no further fitted offset or detrending. One extra
terminal edge after the fresh first-48 selection remains counted separately.

Two failed transmit-proof attempts before PPS population collection and the
lossy nominal-calibration diagnostics remain in `protocol.json`. Empty cursor
polls, intentional diagnostic filtering, transport loss, missing sequence records
and terminal unread edges are different categories.

The historical Nordic result retains **436 usable samples out of 602 nominal
opportunities**, conditional RMS 62.504 ns, 65 invalid-reference observations,
three expired maps and 98 unobserved opportunities. The 501 all-causal samples
have RMS 3.929 ms. That historical population is never pooled with the new S3
path experiment or treated as a controlled platform ranking.

## Reproduce offline

From the repository root, Python 3.9 or newer and the standard library suffice:

```sh
python3 tools/verify_s3_paper.py --require-long-cohort
python3 tools/verify_v14.py
```

The manuscript is self-contained LaTeX, with inline vector figure data:

```sh
mkdir -p artifacts/papers
tectonic --outdir artifacts/papers papers/ptob-esp32s3-timing-preprint.tex
```

No sibling checkout, device or private credential is needed. Different engines
and bundles can alter pagination or PDF bytes; use the paper's edition receipt
for its reviewed source/PDF/compiler identity.

## Limits and rights

These relative PPS observations do not establish calibrated UTC, an identified
S3 packet-level RF endpoint, propagation/path-delay calibration or electrical
output accuracy. The local PPS witness is outside the BLE fit but can share
source errors; source-to-UTC association is unavailable. One observation per
second can miss subsecond excursions. Rolling quality and sampled lock states
are neither whole-run bounds nor continuous lock-duration measurements. The
calibration applies only to its frozen path/profile scope.
UI/session-dependent missingness may be nonrandom; conditional errors cannot
bound the unavailable ending.

Raw captures, private device identities, enrollment and images are excluded.
The public derivatives reproduce the reported analysis, rather than the entire
physical extraction or exact-device operator verification. Preparation does not
authorize push, submission, hardware qualification or draft promotion.

Copyright (c) 2026 Jonathan Yantis. This ordinary documentation and executable
analysis use **MIT**. Original paper/figures and derived evidence JSON/CSV use
**[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)**; credit Jonathan
Yantis. Referenced source code retains its original terms. No enrolled image is
distributed.
