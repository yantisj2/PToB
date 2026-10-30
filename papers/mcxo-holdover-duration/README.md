# Empirical sub-microsecond holdover duration

Jonathan Yantis · 30 October 2026 · approved research draft.

[Read the seven-page paper](ptob-empirical-sub-microsecond-holdover.pdf) or edit its canonical [LaTeX source](ptob-empirical-sub-microsecond-holdover.tex).
[Approval and current package identities](publication.json) record the author's
approval without changing the reviewed manuscript, PDF, result text or figures.
The paper's author-review label describes its preparation checkpoint; it remains
a research preprint, without peer-review or firmware qualification.

The target is the longest experimentally supported sampled sub-1 µs P99 hold
duration. The fully replayed device-grid result reaches **64 s** (128 prefixes,
P99 760 ns); a separate fixed mature cohort passes **75 s** (P99 983 ns) and
fails at 76 s. Later rolling windows end at 24 s. These populations remain
distinct. Temperature is shadow-only; BLE-relative sampling does not establish
a continuous bound, calibrated UTC/output or physical GM-transition acceptance.

## Repository layout

| Location | Contents |
| --- | --- |
| `ptob-empirical-sub-microsecond-holdover.tex`, `results.tex`, `ptob-empirical-sub-microsecond-holdover.pdf` | Approved LaTeX manuscript, generated result text and matching PDF |
| `figures/` | Vector plot source and only the small CSV inputs used by the paper |
| [Dated evidence collection](../../evidence/w12/2026-10-30-holdover/README.md) | Compact summary, checksummed archive, protocol, provenance and exact source snapshots |
| `tools/` | Standard-library numerical replay, table generation and verification |
| [Prepared edition](edition.json) | Unchanged historical preparation seal; paths describe the original loose layout at commit `87b314f` |
| [Current package](publication.json) | Approval, new file locations, archive identity and unchanged reviewed content |

Large per-age prefix CSVs and the full analysis are generated locally rather
than committed. The archive preserves every selected input byte, including
failures, unknowns, gaps, censored runs, separate devices and firmware versions.
The final schematic remains the last page and audits embedded v34; the fresh
measured cohorts remain v33.

## Reproduce

From the repository root, with Python 3.9 or newer:

```sh
python3 papers/mcxo-holdover-duration/tools/verify.py
python3 papers/mcxo-holdover-duration/tools/build_analysis.py --write
```

Verification reads the archive without extracting it into the checkout,
recomputes numerical prefixes and exact status snapshots, checks the original
analysis/table hashes, and runs estimand and archive-integrity regressions.
The second command writes expanded analysis, per-age prefix CSVs and plot tables
under ignored `artifacts/mcxo-holdover-duration/`. Use `--out` for another local
output directory. No hardware, private checkout or credential is needed.

Build the LaTeX with public Tectonic 0.17.0:

```sh
mkdir -p artifacts/mcxo-duration-build
tectonic --keep-logs --outdir artifacts/mcxo-duration-build \
  papers/mcxo-holdover-duration/ptob-empirical-sub-microsecond-holdover.tex
pdfinfo artifacts/mcxo-duration-build/ptob-empirical-sub-microsecond-holdover.pdf
pdftoppm -scale-to 1600 -png artifacts/mcxo-duration-build/ptob-empirical-sub-microsecond-holdover.pdf \
  artifacts/mcxo-duration-build/page
```

The unchanged reviewed paper retains its original `main.tex` build example;
use the renamed source path above for the current checkout.

First use may download public fonts/packages. Inspect every rendered page before
replacing the supplied PDF; other engines or bundles can change pagination and
bytes. Preparation and approval have not pushed this package to GitHub.

Paper, LaTeX, figures and selected/derived data: **CC BY 4.0**, credit Jonathan
Yantis. Code, source snapshots, ordinary documentation and manifests: **MIT**.
See [licensing](LICENSE.md) and the archive's per-member license index.
