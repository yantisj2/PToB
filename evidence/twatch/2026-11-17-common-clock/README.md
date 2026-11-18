# T-Watch GNSS-free common-clock evidence — 17 November 2026

Copyright (c) 2026 Jonathan Yantis. Documentation and executable tools: MIT.
Original evidence records, derived datasets and retained figures: CC BY 4.0.
Cited work retains its own terms.

The paper draft was withdrawn at the author's request on 18 November 2026.
Further research is deferred. This collection preserves the completed study's
selected evidence and analysis tools; it is not a manuscript or publication
approval.

The primary cohort used firmware 0.1.20: a fixed 900-second warmup and 19 fixed
test windows totaling 2,820 seconds. The separately frozen parent-transition
cohort used a 60-second setup and four fixed 120-second windows on the same
boot and clock. Its statistics remain separate, including the unobserved gap
between cohorts. Both cohorts ran with GNSS off.

- [Frozen primary protocol](protocol.json) and [parent protocol](parent-replication-protocol.json)
  retain the original windows and limits.
- [Selected data](data/README.md) retain observations, missing opportunities,
  failed acquisition targets, unavailable sources, excursions and all tails.
- `analyze.py`, `admission.py` and `conditions.py` preserve the analysis tools.
  Raw extraction requires private validated captures; the selected statistics
  can be rechecked using only the files here and Python's standard library.
- `figures/` retains the original diagnostic plots. `sources.json` retains the
  prior literature references as background, separate from experiment evidence.
- [Post-study UI evidence](data/post-study-ui.json) describes firmware 0.1.21
  and its separate display checks. Those observations are not part of either
  measurement cohort.
- [The evidence manifest](manifest.json) records retained file identities and
  the relocation. Hashes establish identity, not timing accuracy.

Raw serial records, exact factory/source identities, enrollment, device profiles,
installed/recovery images and their original receipts remain ignored locally.
Their original sealed records were not changed. No private history or raw lab
material was imported into this collection.

## Verification

From the repository root:

```sh
python3 evidence/twatch/2026-11-17-common-clock/test_analysis.py
python3 evidence/twatch/2026-11-17-common-clock/verify.py
```

The verifier recomputes selected distribution statistics and nominal-opportunity
counts. It does not verify private raw extraction or independently measured UTC,
electrical output or holdover accuracy. Same-source model agreement and sampled
clock continuity do not establish those claims. Historical observations do not
qualify current firmware or authorize hardware operation.
