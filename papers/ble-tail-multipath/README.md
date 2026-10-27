# BLE timing tails and multipath research draft

Jonathan Yantis · 26 October 2026 · public research draft for review.

[Read the nine-page PDF](main.pdf), [edit the source](main.tex), or inspect the
[dated edition receipt](../editions/ble-tail-multipath-2026-10-26.json).
The [publication record](../editions/ble-tail-multipath-2026-10-26-publication.json)
records approval to commit and publish the latest draft for review. The reviewed
LaTeX, nine-page PDF and analytical supplement retain their prepared bytes.

This paper reviews current PToB timing mechanisms and proposes a prioritized
validation programme for the operating BLE precision mesh. It distinguishes
packet delivery latency, correction age, clock/output error, availability,
burst loss and recovery. Practical placement and channel diagnostics accompany
candidate scheduling, instrumentation, weighting and holdover strategies.
The experimental matrix gives hypotheses, controls, instrumentation and
failure criteria. It does not identify multipath as the cause of historical
tails or report a new hardware experiment.

The coherence revision adds historical context from Saleh–Valenzuela channel
measurements, RBS, FTSP and CheepSync, and connects PToB's remaining calibration
ambiguities to affine-clock identifiability results. Direct detector/phase
effects and the indirect loss–aging–recovery route are explained separately.
It expands the same manuscript from seven to nine pages, including references.

The October 24 W12 population, downstream PPS and selected TX population remain
separate from the October 22 N30 fixed-window, calibration and transfer cohorts.
The paper does not qualify newer images from those historical results.
RF first-path detection, detector equivalence, calibration portability,
absolute UTC and physical output bounds remain open. ESP-NOW/Wi-Fi FTM appear
only as separate future comparisons, not implemented PToB timing.

From the repository root, use public Tectonic 0.17.0, Python 3.9+ and Git:

```sh
mkdir -p artifacts/ble-tail-multipath-build
TECTONIC_CACHE_DIR="$PWD/artifacts/ble-tail-multipath-build/cache" \
  tectonic --keep-logs --outdir artifacts/ble-tail-multipath-build \
  papers/ble-tail-multipath/main.tex
python3 tools/verify_ble_tail_multipath_paper.py
python3 tools/verify_w12_utc_results.py
python3 tools/verify_s3_fine_paper.py
```

Tectonic may fetch public typesetting dependencies on first use. No private
checkout, enrollment, firmware build or hardware access is needed. The
bibliography and two original vector figures are embedded in the LaTeX source.
The bundled app's LaTeX compiler can also compile/open this project. Other
engines or typesetting bundles may change PDF bytes or pagination; inspect a
rebuilt PDF before replacing a sealed review copy. Hashes establish identity,
not accuracy. The verifier checks the supplied edition, not an arbitrary rebuild.

For visual review with publicly obtainable Poppler:

```sh
pdfinfo artifacts/ble-tail-multipath-build/main.pdf
pdftoppm -r 120 -png artifacts/ble-tail-multipath-build/main.pdf \
  artifacts/ble-tail-multipath-build/page
```

Inspect every rendered page for readable tables, equations, figures, references
and intact margins. The prepared edition used 11 pt body text and 10 pt tables
and references; all nine pages were visually checked. The source and matching
PDF, receipt, supplement, verifier and changelog travel together. The immutable
preparation receipt records the earlier pending-approval state; the separate
publication record records subsequent approval. The paper remains a research
draft, without final promotion or submission.

The standard-library verifier reuses the existing W12/N30 statistical functions.
It reads the explicitly selected [reviewed source snapshots](reviewed-sources/README.md),
whose bytes match the original Git checkpoint in the preparation receipt. Later
firmware edits do not rewrite the paper's source identity. The publication
verifier needs no unpublished branch or sibling checkout. Other sealed evidence and edition files are
checked directly against their SHA-256 identities.
It checks numerical examples, channel-mix confounding and its limits, idealized
loss/age and confidence calculations, historical denominators/tails and file
identities. Its `--write-analysis` option explicitly regenerates the supplement
without rewriting the edition receipt. Analytical curves and examples are not
hardware evidence. Public evidence replay reproduces arithmetic and selected
provenance; it cannot reconstruct private raw extraction or calibrate RF/output.
The future replay/fault-injection and hardware matrix is proposed, not executed
by this manuscript verifier.

Original manuscript, LaTeX and vector figures: **CC BY 4.0**, credit Jonathan
Yantis. Original derived `analysis.json`: **CC BY 4.0**, explicitly analytical
and separately derived historical summaries. This README, edition receipt and
executable verifier: **MIT**. Cited/third-party material and existing evidence
retain their recorded terms. See [repository licensing](../../LICENSING.md).
