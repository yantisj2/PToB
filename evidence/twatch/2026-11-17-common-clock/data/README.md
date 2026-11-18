# Selected GNSS-free study aggregates

Copyright (c) 2026 Jonathan Yantis. These original evidence records and derived
CSV datasets are licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

The primary population is one T-Watch Ultra on firmware **0.1.20**, with a fixed
900-second warmup and 19 fixed test windows totaling 2,820 seconds. Raw serial
frames, exact factory/source identities, enrollment, profiles and images are
private. No private Git history or bulk workspace was imported.

- `summary.json` retains every declared window, missing observations, source and
  mode states, calibration applicability, correction policy versus actual mapped
  projections, refusal masks and dependent model-disagreement populations.
- `availability.csv` includes every nominal one-second opportunity. An empty
  availability field means no observation; it is not a zero measurement.
- `distributions.csv` stores exact integer-nanosecond values and occurrence
  counts for selected dependent distributions. It reproduces their signed means,
  RMS, nearest-rank absolute P99 and maxima without sharing raw identities or
  precise UTC. Causal-event records are deduplicated; repeated projections remain
  dependent observations, not independent RF packets.
- `timeline.csv` contains five-second aggregates with observed minima, medians
  and maxima, retaining excursions and unavailable comparisons.
- `admission.json` reports the first observed authenticated source state and
  its preceding-negative to observed-positive sampling bracket. Earlier brief
  admissions can be missed. Receiver-model qualification is a separate measure.
- `conditions.json`, `offset-audit.json`, `study-identity.json` and
  `verification.json` bind operating conditions, fixed priors, source/image
  identity and separate native/readback/collection checks. Hashes identify
  evidence; they do not establish accuracy.

Any `parent-replication-*` files describe the separately frozen RC/GM repetition
cohort. Its windows, aliases and generations are separate from the primary
population. Neither cohort includes an independent timing witness.

Run `python3 evidence/twatch/2026-11-17-common-clock/verify.py` to verify evidence hashes and
recompute selected statistics and opportunity counts. Private raw extraction
and independent UTC/electrical-output accuracy cannot be verified from these
aggregates. Firmware changes after the recorded study are not qualified by it.

`post-study-ui.json` describes the separately installed receiver-metrics display
fix after both cohorts were sealed. Its receiver-fit values and UI checks are
not samples in either study. Host checker mistakes and their correction are
recorded; original failed attempts remain private and were not discarded.
