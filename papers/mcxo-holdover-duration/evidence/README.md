# Selected numerical evidence

Copyright (c) 2026 Jonathan Yantis. Original selected records and derived
datasets: CC BY 4.0. Executable extraction/replay code: MIT.

`provenance.json` identifies each device/time-window population, measured
revision, original raw SHA-256, capture boundary and selection. Exact hardware
and mesh identities, enrollment, raw wire wrappers and laboratory paths are
excluded. Numeric records are an explicit field selection, not raw consoles.

Each JSONL preserves record order and contains only:

- `PAIR`: accepted BLE local/reference/availability coordinates, channel,
  generation and sequence.
- `START`: frozen entry epoch, rate, residual phase, counter, original probe
  duration and within-boot trial number.
- `END`: final witness counter, full-run peak, gap count and packed age outcomes.
- `STATUS`: original device duration, readiness, denominator, bad/unknown counts,
  copied last-witness counter and device uptime.
- `RESET`: measurement-scope invalidation.

Counter coordinates use 20 MHz ticks. A common integer counter origin and a
common integer reference origin are subtracted within each capture. Epoch and
reference values receive the same reference shift, preserving every error and
elapsed age. Numeric run-prefix reconstruction matches every complete captured
END with zero difference. ENDs without captured STARTs are orphans; unfinished
starts remain explicit. The independent per-age rings cannot inherit a long
population from two-second lanes. C-2 initial capture includes trial 1; warm
captures independently replace prehistory at each age.

`*-prefixes.csv` contains the first actual witness through each tested age and
the full sampled prefix maximum. It retains overshoot, gaps, expiry, earlier
crossings and outcome codes (1 pass, 2 fail, 3 unknown). `witness=False` means the
numeric peak is a partial/lower-bound prefix rather than a fully witnessed age;
its fail or unknown outcome is still retained. Analysis reports the numeric
prefix denominator separately and flags such quantiles as censored.

`*-ui.csv` contains selected CRC/nonce-bound main-task UI and normal-state facts.
It is a copied UI status, not an optical screen measurement. `figures/` holds
regenerated plot tables, including thin-line observational statuses before
independent ring reconstruction and thick-line exact statuses afterward.

The one-second grid uses its separately predeclared final mature long-run
cohort. It does not replace the device's mixed-lane rolling populations. The
maturity selection can make this cohort older than the capture-end rings;
same-cohort coarse-grid results are included to isolate the effect of resolution.
Immature long starts and programmed-age censoring remain explicit.

`selected-output-witnesses.csv` and `secondary-audit.json` preserve the separate
historical selected-output witness population and private causal-replay audit
facts. These per-witness quantiles are not duration qualification. The old
thermal regression remains as negative evidence.
`expiry-counterfactual.json` is an exploratory continuation of three expired
short clocks through recorded later witnesses. It neither changes installed
outcomes nor qualifies a firmware repair.

Every capture is finite. Overlapping starts share observations and thermal
history; no independent-trial confidence claim is made. Gaps remain unmeasured
internally. This numerical evidence is BLE-relative, not independently
calibrated UTC, electrical output or temperature-range qualification.
