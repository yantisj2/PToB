# ESP32-S3 empirical fine-capture follow-up — research draft

Jonathan Yantis, 22 October 2026. Ordinary documentation: MIT. Selected original
JSON/CSV evidence and original paper/figures: CC BY 4.0.

This collection follows the preserved [earlier edition](../2026-10-22/README.md).
It records a new empirical RX-fall backend, its fine-specific calibration and a
completed fixed 660-second relative-PPS observation. The old incomplete 660-second
attempt remains an availability failure in its own collection. Different roots,
paths, boots, backends and estimators prevent a controlled platform ranking.

The new window has 660 records: 657 eligible, three reason-2 invalid and zero
nominal unrecorded opportunities. A post-window PPS sequence fence was observed.
Eligible mean is −106.811 ns, uncentered RMS 166.590 ns, standard deviation
127.842 ns and maximum absolute residual 1591 ns. All tails remain. The first
48 eligible observations are a subset, not another cohort. Three unread edges
occur after the observed fence; lifetime RAM overwrite growth is not cursor loss.
The completed population was subsequently reused to select total offset 51016 ns;
it cannot also validate that new profile. A fresh second-board transfer on51016ns retained64records:48eligible and16invalid,
zero sequence gaps/duplicates and one terminal unread edge after the first48.
Mean+115.167ns, RMS150.954ns, standard deviation97.590ns and maximum362ns show
that the same total was not mean-zero portable; the sub100ns target failed.
This is a separate short population, with no retuning and no N31 TX packet logs.

`pps-sanitized.csv` retains all 51 records from nominal fine training (48 eligible,
three invalid) all 660 records from the fixed held-out observation, and all64 from the second-
board transfer. Invalid
numeric phase values are retained but excluded from eligible residual statistics.
Columns are an explicit allowlist. Exact private source/root/boot identities were
checked before export and reduced to `same_scope`; counter times are relative.
No MAC, factory/USB identifier, enrollment, raw log, absolute civil time or image
is included. `internal-quality-sanitized.csv` contains sampled normal status from
the fixed-cohort session, including acquisition; it is not a continuous record or
an independent PPS score. Blank RMS means unavailable, not zero.

`protocol.json` records the window and all denominator classes. `results.json`
contains recomputable statistics, nearest-rank absolute quantiles and the four
retained excursions above1us (39.302% of uncentered squared error). The
77.382 ns training spread is not corrected output accuracy. `association.json`
keeps discovery and the separate sparse 128-window causal comparison distinct;
its selected aggregate results are provenance-bound but not recomputed from raw
RF records here. `source-context.json` identifies selected software bytes and
private extraction hashes without importing those captures. Hashes prove identity,
not timing accuracy. The previous edition's public evidence remains unchanged.

From a public checkout, using standard-library Python only:

```sh
python3 tools/verify_s3_fine_paper.py
```

The verifier checks eligibility, order, counter scale, all residuals/tails,
training-derived offsets, minute blocks, subset identities, denominator arithmetic
the distinct transfer denominator and selected manifest hashes. It does not open hardware or require private inputs.
It cannot reproduce physical extraction or establish RF/UTC calibration. The
historical edition's original source/PDF verifier must be run at that edition's
recorded Git revision because its immutable manifest intentionally binds the
previous source/PDF bytes.

The measured backend uses one boot-owned extended 80 MHz capture counter and an
empirical RX-fall convention for a single PDU/PHY/header class. Its delivered
coordinate rounds to 50 ns. A 12.5 ns tick is not an accuracy claim. TX remains a
programmed nominal software estimate. RF END/ADDRESS delay, calibrated UTC and
physical N30 output accuracy remain unknown. No paper promotion or submission is
implied by preparing this replacement draft.
