# Empirical sub-microsecond holdover duration

Jonathan Yantis · 30 October 2026 · author-review research draft.

[Read the preprint](main.pdf), [edit its LaTeX source](main.tex), or inspect the
[edition manifest](edition.json). This is a new manuscript and evidence package.
It remains a draft; preparation is not publication approval or firmware
qualification.

The question is the longest experimentally supported hold duration at a fixed
sampled error limit of 1000 ns and empirical P99. Each frozen run contributes
its maximum absolute error over the entire sampled prefix through the first
actual BLE witness at or after each age. The result inverts this statistic on
the device's age grid, requiring 100 scored runs and retaining the latest 128
outcomes independently at every age. Twelve seconds is a historical reference,
not the goal or stopping criterion. Pooled per-witness error percentiles remain
separate secondary evidence.

The [predeclared protocol](protocol.json), [population provenance](evidence/provenance.json),
[recomputed analysis](analysis.json), [selected numeric observations](evidence/),
and [reviewed source snapshots](reviewed-code/manifest.json) travel together.
Per-age prefix CSVs and the scientific plot data are regenerated from the
numeric records. The final schematic is the last page and describes the audited
current v34 implementation (measured cohorts remain v33): BLE-only timing, rate-only production HOLD, separate
temperature shadow, 48 frozen offset clocks, mixed short/adaptive lanes,
per-age rolling decisions, scope reset, expiry and stale display behavior.

From the repository root with Python 3.9 or newer:

```sh
python3 papers/mcxo-holdover-duration/tools/verify.py
```

The standard-library verifier uses only the included public files. It
reconstructs numerical frozen-clock prefixes, verifies END outcome bits and
copied-status snapshots, independently accounts for each age's prehistory,
and checks quantiles, selected output statistics, source hashes and edition
identities. It performs no hardware operations. Synthetic cases distinguish
prefix maxima from endpoints/full-run peaks, include witness overshoot,
retain unknown expiry, and refuse short-lane substitution.

To regenerate derived tables without changing the edition receipt:

```sh
python3 papers/mcxo-holdover-duration/tools/build_analysis.py --write
```

Build with public Tectonic 0.17.0 and its public typesetting bundle:

```sh
mkdir -p artifacts/mcxo-duration-build
tectonic --keep-logs --outdir artifacts/mcxo-duration-build \
  papers/mcxo-holdover-duration/main.tex
pdfinfo artifacts/mcxo-duration-build/main.pdf
pdftoppm -scale-to 1600 -png artifacts/mcxo-duration-build/main.pdf \
  artifacts/mcxo-duration-build/page
```

First use may download public fonts/packages. The plots use standard PGFPlots;
the final vector schematic uses TikZ. Inspect every rendered page before
replacing the supplied PDF. Different engines/bundles may change pagination
and PDF bytes. No private dependency, sibling checkout, enrolled image or
credential is needed for the public numerical replay or typesetting.

Original raw consoles and private device/enrollment identifiers are excluded.
Selected numeric records remove their wire wrapper and use common integer
coordinate origins, preserving elapsed ages and all reconstructed errors.
Original capture SHA-256 identities and every selection/transformation are
recorded in provenance. Public replay reproduces the selected numerical
evidence, not USB acquisition or an independent physical reference calibration.
Overlapping runs are correlated. Sampling does not bound gap interiors or
establish calibrated UTC, electrical output, temperature-range performance,
or hardware source-transition acceptance.

Paper, LaTeX, figures, selected original evidence records, derived CSVs and
`analysis.json`: **CC BY 4.0**, credit Jonathan Yantis. Executable code, source
snapshots, this README, protocol and manifests: **MIT**. Third-party cited
works retain their terms. See [repository licensing](../../LICENSING.md).
