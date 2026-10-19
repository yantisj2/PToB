# Awake timing information and wake/listen power potential

Copyright (c) 2026 Jonathan Yantis. Documentation and analysis code: MIT.
Original selected evidence, derived datasets, manuscript and figures: CC BY 4.0.
See [the evidence license](../../LICENSE.md) and [repository policy](../../../LICENSING.md).

This is the selected supplement to the **19 October 2026 research draft**
[How Much Timing Information Is Enough?](../../../papers/ptob-power-scheduling-preprint.pdf).
It supports an awake hardware information-budget pilot and hypothetical scheduling,
drift and energy models. Physical sleep, wake latency, power-meter measurements,
calibrated UTC and qualified output remain future work. The edition is locally
prepared and committed as a draft; it has not been submitted or pushed.

## Selected population

PC117 is T096 firmware 0.15.117 at `9ee2bcbc7cdd7bf27e6da80fb65aabf47ff04606`.
One physical receiver/boot used six randomized main blocks of 20 windows, with two
each at 5/10/20 s HOLD. Windows were 125 ms, starting 8 ms before predicted RX on
a nominal 62.5 ms grid, using one acquisition-derived phase anchor per run.
Both RX and reference completion had to fit. The schedule did not rephase from
HOLD observations or widen after a miss. RF and the precision counter remained
active. The 2.439/1.235/0.621% fractions describe scheduled training windows.

All six schedules completed: 42 refreshes, 78 empty windows. Sixty-nine empty
windows had no accepted timing pairs in the cycle, and nine had reception without
a complete selected pair. No source-loss mechanism is inferred from these counts.
Seven priming attempts completed and two failed, including canary priming. The
canary selected 2/3 windows. All failed attempts and bounded wait policy remain.

| Run | HOLD/rep | Refreshes | HOLD BLE n / normalized RMS ns | Occupied BLE HOLD bins | HOLD PPS eligible/recorded / RMS ns |
| --- | --- | --- | --- | --- | --- |
| B1 | 10 s/1 | 4/20 | 617 / 112.331581 | 44/200 | 200/200 / 2173.739217 |
| B2 | 20 s/1 | 5/20 | 1711 / 260.976563 | 116/400 | 400/400 / 788.196452 |
| B3 | 5 s/2 | 16/20 | 1484 / 98.092481 | 100/100 | 100/100 / 107.535761 |
| B4 | 5 s/1 | 1/20 | 134 / 137.113201 | 10/100 | 100/100 / 80.259267 |
| B5 | 10 s/2 | 10/20 | 1681 / 333.220965 | 114/200 | 200/200 / 268.988262 |
| B6 | 20 s/2 | 6/20 | 1580 / 150.396552 | 115/400 | 400/400 / 318.179615 |

BLE uses raw + 10543 ns, an inherited provisional constant. Raw values remain;
no fitted bias or detrending is used. RMS includes remaining bias. Occupancy means
at least one valid score in a one-second HOLD bin; it does not imply continuous
precision. Missing bins remain unknown. B1's last valid HOLD BLE capture was at
44.258 s of 202.5 s, while its PPS witness continued. PPS is independently
observed local fractional phase with explicit eligibility, not calibrated UTC or
qualified output. Across whole main runs, 1414 PPS edges were recorded: 1400 HOLD
(all eligible), 14 LISTEN (four eligible, ten ineligible). The latter remain in CSV.

Only B3 replaced all 16 acquisition fit pairs, generations 26 to 42. Its sixteenth
selected pair completed at 92.330255875 s, leaving 10.169744125 s and 149 causal
HOLD BLE scores at 144.780649 ns RMS. This brief interval is not steady-state
qualification. Other runs retain acquisition history. Runs are never pooled for
precision, even when scope aliases match; source loss, initial fits and uncontrolled
temperature prevent a causal ranking of HOLD intervals.

## Files and schema

- `ble-records.csv`: all 7326 selected derived BLE rows, including invalid and
  out-of-run rows. `train`, `valid`, `in_run`, `in_hold` and
  `after_16_selected` are explicit booleans. Empty error fields are unavailable.
  `cycle` follows RX; `completion_cycle` follows the completion record.
- `pps-records.csv`: all 1414 derived PPS rows in the six run intervals, retaining
  eligibility, reference flags, reason codes and projection status. An empty
  `phase_ns` is unavailable; it is never a zero error.
- `blocks.csv`, `cycles.csv`: six run and 120 window summaries, respectively.
  Cycle BLE summaries use all valid in-run captures in that cycle, including
  LISTEN; the paper's precision table uses **HOLD only**, recomputed from rows.
- `hold-audit.csv`: 123 reduced frozen-state records (120 main, three canary),
  with repeated state-observation counts. Full raw envelopes are excluded.
- `protocol.json`, `attempts.json`, `audit-receipt.json`: selected scientific
  protocol, all nine priming outcomes, and original audit/integrity results.
- `prior-context.json`, `pc112-listening-budgets.csv`, `pc112-hold-windows.csv`:
  explicitly separate historical PC112/115/116 context. The two PC112 timing-only
  CSVs are selected unchanged. They are not PC117 replicates. Earlier HOLD-window
  validation is discussed separately in the paper; its samples are not imported.
- `results.json`: public recomputation, per-run raw/normalized means, RMS, maxima,
  counts, missing bins, turnover and hypothetical scenario outputs.
- `claims.json`, `provenance.json`, `literature.json`, `manifest.json`: claim
  boundaries, original source hashes/transformations/exclusions, primary sources,
  and public-file integrity.

Counter units are 16 MHz ticks; `elapsed_rx_tick`, `elapsed_available_tick` and
`elapsed_edge_tick` are relative to that run's original scheduled start. Negative
BLE times identify preserved pre-run records. Block `elapsed_*` fields use the
same shift. All `*_ns`, `*_ms`, `*_s`, `*_ppb` and `*_us` suffixes identify units.
The phase residual is against the fixed nominal packet grid, not a fitted phase.
`after_16_selected` means sequence strictly after the sixteenth selected pair.

`physical_boot_alias=B01` preserves the common physical-boot population. `scope_alias`
replaces the full root/root-boot/root-epoch/parent/parent-boot/capture-or-edge-epoch
tuple with a stable equality alias. S01 applies to valid B1, S02 to valid B2--B5,
S03 to valid B6; S04 appears only in excluded PPS context. Actual identities and
the alias lookup are withheld. Separate run/trial IDs and all validity flags remain.

## Public offline reproduction

From the repository root, Python 3.9+ with its standard library is sufficient:

```sh
python3 tools/verify_power.py
python3 tools/verify_power.py --out artifacts/power-rebuild
```

The first verifies manifest hashes and independently recomputes normalization,
eligibility, per-run HOLD RMS/counts/coverage, aperture/anchor arithmetic, cycle
outcomes and map turnover, checks the manuscript's embedded figure data and
matching edition hashes. The second writes a fresh JSON report and standalone
PGFPlots figure sources. Use a new output directory. Neither command opens a
device, contacts a server, launches a daemon or reads private evidence.

The manuscript includes its bibliography and vector figures inline. Public
Tectonic can compile it; see [paper build instructions](../../../papers/README.md).
Standalone figure sources are in [papers/figures/power-20261019/](../../../papers/figures/power-20261019/).
Energy inputs are explicit hypothetical scenarios, not datasheet current copied
onto a board or fitted measurements. Drift examples assume independent zero-mean
errors; unknown bias and covariance invalidate that simplification. Coarse RTC
handoff and retained fine-counter models are separate.

## Provenance and reconstruction limit

The sealed private PC117 selection index SHA-256 is
`7fad272ec1426dea568e8c323d6349f4fc686d676911fb60384abd265bf8a33d`.
All 34 selected files and raw, analysis and restoration manifests were verified
without changes. Original analyzer, independent verifier and alignment audit
were rerun against closed inputs in a separate scratch directory; four derived
CSVs reproduced byte for byte. Source hashes and selected transformation rules
are in `provenance.json`. They establish identity, not accuracy.

The public export uses explicit column allowlists. It replaces actual device,
parent/root/boot/epoch identity with aliases and shifts absolute counter values
without changing differences. Numeric observations, failures, missingness and
all selected derived rows remain. No raw console, authentication envelopes,
factory identity, enrollment, keys, enrolled images, private working files or
private Git history are included. The original sealed captures remain untouched.

Public verification starts from derived CSVs. It cannot independently recreate
raw capture extraction, authentication, physical instrumentation or full raw
HOLD-state immutability. The reduced audit records and receipts disclose that
boundary. Earlier PC112/115/116 populations have their own source hashes and
selection limits; they are never merged with the main six runs.
