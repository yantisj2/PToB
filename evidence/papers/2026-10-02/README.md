# October 2 paper evidence

This collection belongs to the October 2 proposal and calibration/publication
preprint. Its cutoff is **2026-10-02T17:28:13.400616Z**. It is not v12/R2 evidence.

- [Evidence manifest](evidence.json): 53 run inventories, selected result
  populations, methods, claim mappings, calibration, recovery, and recorded seals.
- [Protocol identity](proposal-wire-manifest.json): canonical codec/contract hashes,
  exact examples, and scope at the paper cutoff; the referenced headers are not
  included in this collection.
- [Manuscript boundary](manuscript-boundary.json): predecessor hashes and the
  original paper-update checkpoints.
- [Calibration](calibration.json), [reacquisition episodes](reacquisition-episodes.json),
  and [literature checks](literature-check.json): supporting context.

The physical T096 source is `f83778e8e31cca725cf78d37a76d8be596d30dcb`;
prepared source `3c1fbf77129b189dd7695389f753ca241d2497c9` was not deployed for
these results. W12 C-2 used `c8e5bb202bce7233e9e3edb752f537664aca08b2`;
C-1 remained a separately identified field unit. Full image hashes and limits
are retained under `hardware` in the evidence manifest.

The fixed 10,516 ns correction was trained before validation. Warm PPS-shadow,
interrupted reverse, internal TIMER/PPI comparison, W12 cold, and W12 normal
populations remain separate. They do not demonstrate independent GPIO output,
calibrated absolute UTC, or measured whole-device energy. Do not transfer their
results to a later protocol or firmware revision.

The manifest contains derived results, not full raw captures. One incomplete
console text fragment was omitted; its metadata and denominator remain. See the
[import/redaction record](../../imports/2026-10-09.json). Historical `paper/`,
`results/archives/`, and firmware paths identify the original archive. The public
manifest's hash differs from the original because of that documented omission.
