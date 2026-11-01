# Trustworthy Hardware Timestamps in Zephyr

**Jonathan Yantis · 1 November 2026 · seven-page research draft approved for
public review.**

Read the [PDF](main.pdf) or edit the canonical [LaTeX](main.tex). The paper
examines event association, counter continuity, bounded readers, clock bridges
and source publication ordering in PToB's Zephyr/nRF52840 implementation. Its
targeted prior-art review identifies a practical contribution to Zephyr's
evolving event-capture infrastructure: inspectable integration contracts,
failure fixtures and a proposed accuracy/availability/cost evaluation.

The [publication receipt](publication.json) records subsequent author approval.
The approved PDF and LaTeX are unchanged from the reviewed candidate, including
their earlier author-review and pending-library-inclusion labels. Those labels
preserve the review checkpoint; this README and the separate receipt record the
later approval. This remains a draft, without submission or peer review.

## Scope and evidence

The source case study uses revision
`a9ae0294cd8fc5b9c919665094a489708442a49b` and pinned Zephyr 4.1.0 dependencies.
The [edition manifest](edition.json) binds the public package; the unchanged
[source identity record](source-identity.json) identifies all 25 inspected
repository files. The [primary-source ledger](source-ledger.json) records 23
online sources and access limitations; the manuscript has 21 bibliography
entries. Online status is dated to 1 November 2026.

The separate [12 October V14 collection](../../evidence/v14/2026-10-12/README.md)
provides historical relative-PPS evidence: **602 = 436 usable + 65 invalid
reference + 3 expired + 98 unobserved** opportunities. The 62.503871 ns RMS
belongs to the 436 usable observations. Invalid-reference excursions, missing
witnesses and fixed historical calibration remain explicit. This is neither a
new hardware experiment nor current-firmware, calibrated UTC or electrical
output qualification. The source-ordering regression is described but was not
rerun for this edition. Comparative hardware measurements remain proposed work.

## Verify and rebuild

From the repository root, Python 3.9+ and its standard library suffice:

```sh
python3 tools/verify_zephyr_timing_paper.py
python3 tools/verify_zephyr_timing_paper.py --replay
```

The first command checks package identities, approval bindings, citation
resolution and inspected source identities. It uses current files when their
hashes match, otherwise the recorded revision in this repository's public Git
history. Include that history when cloning; no private checkout or credentials
are required. `--replay` also runs the existing V14 archive verifier, which
recomputes the selected historical populations and checks their import receipt.
Neither command operates hardware or reruns the native source-ordering test.

Rebuild into an ignored output directory, preserving the approved PDF:

```sh
mkdir -p artifacts/zephyr-paper-rebuild
tectonic --keep-logs --outdir artifacts/zephyr-paper-rebuild papers/zephyr-timing/main.tex
```

The reviewed PDF used public Tectonic 0.17.0 and its default v33 resource bundle.
The first build may download public TeX resources. All figures and references
are embedded in the source. Compiler/resource changes can change PDF bytes;
rebuilding does not replace the approved edition or qualify its measurements.

The unchanged [preparation receipt](preparation.json) records the earlier local
review checks and their scope. Its file inventory includes authoring logs,
notes and a packaging helper that are not distributed here; those are historical
locators, not public package dependencies. Only the five explicitly selected
review files listed in `edition.json` were copied into this publication package.

Original paper, LaTeX and figure: **CC BY 4.0**, Jonathan Yantis; see the
[paper license](../LICENSE.md). Illustrative pseudocode, ordinary documentation,
metadata and verification code: **MIT**. Cited works and inspected source files
retain their existing terms. Website integration is separate.
