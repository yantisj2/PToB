# Indoor PNT research draft

Jonathan Yantis · 26 October 2026 · public research draft for review.

[Read the PDF](main.pdf), [edit the LaTeX](main.tex), or inspect the
[edition receipt](../editions/indoor-pnt-2026-10-26.json).
The [publication record](../editions/indoor-pnt-2026-10-26-publication.json)
records approval to commit and publish the draft. Reviewed LaTeX and PDF bytes
remain unchanged.

The paper proposes shared PToB event time beneath inertial, visual, wheel,
pressure, magnetic, acoustic and optional UWB/other positioning observations.
It uses existing dated relative-timing evidence and analytical examples. It
contains no new indoor localization experiment or demonstrated UWB integration.
Measured timing, GNSS comparisons, sensor acquisition, radio ranging and spatial
positioning retain distinct endpoints and uncertainty requirements. Cost inputs
and pilot targets are explicitly hypothetical.

From the repository root, using a public Tectonic installation:

```sh
mkdir -p artifacts/indoor-pnt-build
tectonic --outdir artifacts/indoor-pnt-build papers/indoor-pnt/main.tex
python3 tools/verify_indoor_pnt_paper.py
```

The source embeds its bibliography and vector figures and has no private inputs.
Tectonic may fetch public typesetting dependencies on first use. The build creates
`artifacts/indoor-pnt-build/main.pdf`; inspect the rebuilt PDF before replacing
the matching review copy here. Other engines or bundles may produce different
PDF bytes or pagination. The local preparation used Tectonic 0.17.0 and Poppler
for all-page visual review. Python 3.9+ and its standard library suffice for
the offline verifier. These commands do not open hardware.

The verifier checks the analytical examples, selected public CSV statistics,
bibliography and edition file identities. Existing evidence verifiers provide
additional population and provenance checks:

```sh
python3 tools/verify_w12_utc_results.py
python3 tools/verify_s3_fine_paper.py
python3 tools/verify_v14.py
```

Public derived data cannot recreate private raw capture or independently
establish physical calibration. The historical V14 source/profile, W12 windows,
N30 cohorts and stable-window subset are not pooled. GNSS time-transfer
equivalence, absolute UTC, indoor positioning accuracy, fine UWB synchronization,
energy/cost savings and interoperability remain unqualified.

This paper is available for public review as a research draft, without stable
promotion or submission. Matching source, PDF, verifier, edition receipt and
changelog travel together. The immutable preparation receipt retains its earlier
pending-approval checkpoint; the publication record records subsequent approval.
Preserve earlier editions through Git.

Original manuscript, LaTeX and vector figures: CC BY 4.0, credit Jonathan Yantis.
This README and the verifier: MIT. Cited material retains its original terms;
the existing evidence keeps its own recorded licenses. See
[repository licensing](../../LICENSING.md).
