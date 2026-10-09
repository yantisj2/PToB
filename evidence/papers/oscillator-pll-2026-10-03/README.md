# October 3 oscillator/PLL evidence

This is the hardware-derived companion to the [software clock discipline draft](../../../papers/oscillator-pll-research-20261003.pdf).
Installed T096 source: `28ee7d8120e2c479dce52e7a39cb44bb7b338885`.
The experiment used a raw-radio reference and a numerical clock-map actuator;
it is separate from the October 2 PPS-shadow calibration and later R2 transport.

| File | Contents |
| --- | --- |
| [evidence.json](evidence.json) | Claims, methods, installed source, nine run inventories, populations, exclusions, and figure hashes |
| [capture-metrics.csv](capture-metrics.csv) | Seven UI5 capture populations with denominators, errors, gaps, and sampled state counts |
| [indexed-pair-samples.csv](indexed-pair-samples.csv) | All 7,324 retained pair rows for those captures: 7,293 scoreable, 31 unavailable |
| [final-sampled-status.csv](final-sampled-status.csv) | Final-run discrete native PLL/UI status observations |
| [literature.json](literature.json) | Primary-source references and comparison limits |

The failed earlier overnight campaign remains in the manifest: 71 closed captures,
33 complete and 38 incomplete. The simultaneous three-device, quiet-pair,
delayed-join, and final-extension cohorts are not pooled. The final extension
includes its recovery tail. The displayed 84 ns value is a recent-history
statistic, not a whole-run bound. Sampled lock counts do not establish continuous
lock duration. These samples cannot establish physical quartz/carrier discipline,
independent electrical output, calibrated UTC, energy, or field restart accuracy.

From the repository root, `python3 tools/verify_evidence.py` recomputes each
capture's signed mean, uncentered RMS, extrema, and nearest-rank absolute P95/P99,
while retaining unavailable denominators. It does not independently reconstruct
capture extraction or native lock decisions. Raw archives and the original
private-archive extraction script are not included. Raw line/offset columns are
provenance locators, not embedded raw console logs. The complete plots are inline
in the standalone manuscript source.
