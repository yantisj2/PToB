# Research papers — drafts

These are working drafts by **Jonathan Yantis**, imported unchanged from the
latest matching proposal and principal research-preprint source/PDF pairs on
9 October 2026. Both editions are dated **2 October 2026**. They are not stable
papers, peer-reviewed publications, or adopted standards.

| Draft | Read | Edit |
| --- | --- | --- |
| PToB: A Passive Time Service for Diverse BLE Edge Devices — candidate protocol specification and profile proposal | [PDF](ptob-proposal.pdf) | [LaTeX](ptob-proposal.tex) |
| Passive Precision Time over BLE: Prospective Edge Calibration, Causal Publication, and Service Availability | [PDF](ptob-preprint.pdf) | [LaTeX](ptob-preprint.tex) |

Both papers and their original figures are **CC BY 4.0**, attributed to Jonathan
Yantis. See [the license notice](LICENSE.md) and [project credits](../CREDITS.md).

## Scope and evidence

The evidence cutoff is **2026-10-02T17:28:13.400616Z**. These papers describe the
research and protocol at that cutoff; they do not establish current v12/R2
firmware behavior or qualification. Later R2 findings require a new edition or
explicit addendum. The separate oscillator/PLL follow-up is outside this import.

The [initial import receipt](imports/2026-10-09.json) records source/PDF hashes,
the source checkpoint, original build identity, and the evidence-manifest hash.
The original build records matched both imported pairs and the PDFs matched the
website assets. These checks establish file identity, not scientific validity.

The manuscripts retain historical references to `paper/evidence.json`,
`paper/proposal-wire-manifest.json`, and `protocol/` in the original research
archive. Those artifacts and raw datasets are **not included yet**. Typesetting
is self-contained; independent reproduction of the research results is not yet
available from this repository alone. Evidence import is a separate curated step.

## Work on the next draft here

These LaTeX files are now the working manuscript sources for this repository.
Make future paper edits here and preserve changes in Git; do not silently replace
them with later private copies. Import private findings as reviewed, attributed
changes with their evidence. Keep the baseline import receipt immutable.

For each new edition:

1. Update the source and its draft date/version; retain author and license credit.
2. Record material changes, evidence additions, and limitations in CHANGELOG.md.
3. Compile, render, and inspect every changed page; check references and layout.
4. Update the corresponding PDF and add a new manifest binding source, PDF,
   compiler, and evidence cutoff. Do not label an old PDF as the new source.
5. Commit the matching source, PDF, manifest, and changelog together. If a source
   change cannot yet be exported, explicitly mark its PDF as stale.

Use Git history for prior drafts rather than accumulating `final-v2` filenames.
A stable edition or publication release requires an explicit editorial decision.
Local commits do not imply approval to push or publish.

## Build the manuscripts

Each `.tex` file is standalone, including bibliography and vector graphics;
there are no sibling-repository inputs. The original export used Tectonic 0.17.0.
With Tectonic available, run from the repository root:

```sh
mkdir -p artifacts/papers
tectonic --outdir artifacts/papers papers/ptob-proposal.tex
tectonic --outdir artifacts/papers papers/ptob-preprint.tex
```

Tectonic may download its public TeX bundle on first use. Inspect the generated
PDFs before replacing the checked-in copies. Different TeX engines or bundle
versions may change pagination or PDF bytes. The imported PDFs are the original
exports, not replacements generated during migration.

## Cite a draft

Jonathan Yantis. *PToB: A Passive Time Service for Diverse BLE Edge Devices.*
Candidate protocol specification and profile proposal, draft, 2 October 2026.

Jonathan Yantis. *Passive Precision Time over BLE: Prospective Edge Calibration,
Causal Publication, and Service Availability.* Research preprint, draft,
2 October 2026.

Include the repository commit or edition manifest when citing a specific version.
No DOI or publication venue has been assigned here.
