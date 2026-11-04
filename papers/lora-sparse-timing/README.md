# Sparse one-way LoRa clock transfer

Jonathan Yantis. **Draft for author review, 4 November 2026.**
This edition is not approved for public publication or promotion.

[Read the seven-page manuscript](main.pdf) or edit its [canonical LaTeX](main.tex).
The retrospective describes the authenticated wire3/schedule8 timing path,
low/high and intermediate bandwidth experiments, frequency hopping, selection
of a fixed 922.4 MHz / 500 kHz / SF11 / CR4/8 candidate, and the complete
900-PPS T096 .288 validation. No hardware operations or new experiments were
performed to prepare the paper.

The primary result is 269.140 ns RMS delivered-model phase at local GNSS PPS
inputs, with -95.018 ns mean, 580 ns absolute P99 and 624 ns maximum. All
900 declared future opportunities were captured, eligible and mapped. A fixed
+102931911 ns native-input prior was frozen from four earlier physical pairs;
validation was not recentered, detrended or filtered. This is calibrated
input/model agreement, not independently measured electrical output or portable
UTC qualification. Six physical-SYNC-age bins each retain 150 observations.
The incomplete .287 attempt retains 206 mapped, nine NO_MAP and 685 uncaptured
of 900 planned opportunities, separately from the final result.

Bandwidth-screen pair phases and final model phase are different quantities.
The hopped two-band screen uses SF7/CR4/6; its 500 kHz arm's centered endpoint
SD was 217.550 ns with a raw phase near -6.418 ms. The common-channel five-band
screen retains two separate recovery streams and very small, nonmonotonic
scatter populations. The final fixed candidate uses SF11/CR4/8. These data
support the selected finite test, not a universal optimum or demonstrated
range gain.

## Reproduce selected-data statistics and figure

From the repository root with public Python 3.9 or newer:

```sh
python3 papers/lora-sparse-timing/tools/analyze.py
python3 papers/lora-sparse-timing/tools/verify.py
```

The standard-library analyzer recomputes all included populations and aggregate
metrics from [selected derived CSVs](data/), the calibration median, physical
age bins and guarded airtime. It writes `results.json`. It does not operate
hardware, require credentials or access ignored private files. It cannot
reconstruct physical capture, private authenticated extraction, native model
execution or calibration uncertainty. Unknown transmitter-offer coverage remains
unknown. The [provenance record](provenance.json) records input hashes,
selection, redactions and these reproduction limits.

To regenerate the unsmoothed vector scientific figure, install public ReportLab:

```sh
python3 -m pip install reportlab
python3 papers/lora-sparse-timing/tools/plot.py
```

Build with a public TeX toolchain, such as Tectonic 0.17.0:

```sh
tectonic -X compile papers/lora-sparse-timing/main.tex --outdir artifacts/lora-sparse-paper-tex
```

Copy the rebuilt PDF to this directory only as part of a new matching edition,
then render and inspect every page. The [edition manifest](edition.json) binds
the matching source, PDF, figure, selected data and support files. It is an
identity/review record, not publication approval or firmware qualification.
The source ledger records primary prior-art references and claim boundaries.

The measured .288 source digest and later compatible wire-reference commit
are distinct. Codec/header bytes are identical in those snapshots; processing
limits and other implementation details have evolved. Current or later images
do not inherit the measured .288 result. Historical wire2 documentation is not
the wire3/schedule8 specification.

Original manuscript/figures/datasets: CC BY 4.0, Jonathan Yantis. Original code
and ordinary documentation: MIT. See [licensing](LICENSE.md) and the repository
[licensing policy](../../LICENSING.md). No private raw logs, credentials,
enrollment, device identities or images are included.
