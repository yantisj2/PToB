# Research papers — drafts

These are working drafts by **Jonathan Yantis**. The latest proposal and research
preprint are explicit **12 October 2026 V14 successors**, imported on October 13.
They remain local discussion/research drafts, without standards adoption or
firmware qualification. Their source and reviewed PDF bytes are unchanged.

| Latest draft | Read | Edit |
| --- | --- | --- |
| Passive Precision Time over BLE: V14-RC1 Candidate Service and Profile Proposal | [PDF](ptob-v14-sig-proposal.pdf) | [LaTeX](ptob-v14-sig-proposal.tex) |
| Passive Precision Time over BLE: V14 Boundary Clocks, Causal Timestamping, and Prospective One-Relay Validation | [PDF](ptob-v14-preprint.pdf) | [LaTeX](ptob-v14-preprint.tex) |

The V14 evidence cutoff is **2026-10-12T18:26:44.293859Z**, with specification
freeze `7995249` and selected application **0.14.43** at
`566ea2029c7ffc84a7d321df427c5c811fac2730`. The [edition receipt](imports/v14-2026-10-13.json)
records exact source/PDF identity, original build/review records, rights, selected
scientific exports, and exclusions. Both PDFs matched the website copies when
imported; no website deployment was performed.

The [V14 evidence collection](../evidence/v14/2026-10-12/README.md) provides
sanitized samples, figure/table exports, claim provenance, and offline checks.
It retains invalid references, expired maps, and missing opportunities. The
proposal's [known normative issues](../evidence/v14/2026-10-12/provenance/ptob-v14-sig-issues.json)
remain open. [Research status](../docs/V14_RESEARCH_STATUS.md) distinguishes those
results from current firmware and calibrated output accuracy.

## Historical drafts

The earlier proposal and principal research preprint retain their unchanged
**2 October 2026** editions. The
oscillator/PLL follow-up uses the amended **3 October 2026** source and a matching
PDF exported on **9 October 2026**. None is a stable paper, peer-reviewed
publication, or adopted standard.

| Draft | Read | Edit |
| --- | --- | --- |
| PToB: A Passive Time Service for Diverse BLE Edge Devices — candidate protocol specification and profile proposal | [PDF](ptob-proposal.pdf) | [LaTeX](ptob-proposal.tex) |
| Passive Precision Time over BLE: Prospective Edge Calibration, Causal Publication, and Service Availability | [PDF](ptob-preprint.pdf) | [LaTeX](ptob-preprint.tex) |
| Causal Software Clock Discipline over Wireless Physical Events: Native Lock Evidence, Operating Tiers, and a Tunable-Oscillator Extension | [PDF](oscillator-pll-research-20261003.pdf) | [LaTeX](oscillator-pll-research-20261003.tex) |

All five papers and their original figures are **CC BY 4.0**, attributed to Jonathan
Yantis. See [the license notice](LICENSE.md) and [project credits](../CREDITS.md).

## Scope and evidence

For the later periodic-advertising direction and planned mesh/client service
split, read [From v12 to R2](../docs/V12_TO_R2.md). That architectural note does
not revise these draft editions or their experimental claims.

The first two papers have evidence cutoff **2026-10-02T17:28:13.400616Z**. They
describe the research and protocol at that cutoff; they do not establish current v12/R2
firmware behavior or qualification. Later R2 findings require a new edition or
explicit addendum. Their [historical evidence collection](../evidence/papers/2026-10-02/README.md)
preserves the original protocol and firmware context.

The [initial import receipt](imports/2026-10-09.json) records source/PDF hashes,
the source checkpoint, original build identity, and the evidence-manifest hash.
The original build records matched both imported pairs and the PDFs matched the
website assets. These checks establish file identity, not scientific validity.

The manuscripts retain historical archive paths. The October 2 evidence and
wire manifests now live under [evidence/papers/2026-10-02/](../evidence/papers/2026-10-02/README.md),
with the documented raw-fragment omission in the evidence manifest. Referenced
protocol headers and full raw archives are not included.

The oscillator/PLL [edition receipt](imports/oscillator-pll-2026-10-09.json)
resolves the prior mismatch: the original 16-page PDF preceded the virtual-lock
and SoftPLL amendments. The current 17-page PDF is exported from that amended
source, whose content is unchanged. It preserves the October 3 experiment and
its limitations; no later v12/R2 results were inserted. Its [separate evidence
collection](../evidence/papers/oscillator-pll-2026-10-03/README.md) includes derived
samples and reproducible per-capture error metrics. The historical private-archive
extraction script and raw captures are not included.

All sources are self-contained for typesetting. Full independent reconstruction
of every physical experiment is not possible from this checkout alone. Historical
references describe their edition, not the current repository layout or status.

## Work on the next draft here

The V14 LaTeX files are the working successor sources for this repository;
earlier editions retain their historical scope.
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
tectonic --outdir artifacts/papers papers/oscillator-pll-research-20261003.tex
tectonic --outdir artifacts/papers papers/ptob-v14-sig-proposal.tex
tectonic --outdir artifacts/papers papers/ptob-v14-preprint.tex
```

Tectonic may download its public TeX bundle on first use. Inspect the generated
PDFs before replacing the checked-in copies. Different TeX engines or bundle
versions may change pagination or PDF bytes. The October 2 and V14 proposals/preprints
retain original exports. The oscillator/PLL PDF was regenerated during migration
to match its existing amended source.

## Cite a draft

Jonathan Yantis. *Passive Precision Time over BLE: V14-RC1 Candidate Service and
Profile Proposal.* Discussion draft, 12 October 2026.

Jonathan Yantis. *Passive Precision Time over BLE: V14 Boundary Clocks, Causal
Timestamping, and Prospective One-Relay Validation.* Research preprint, draft,
12 October 2026.

Jonathan Yantis. *PToB: A Passive Time Service for Diverse BLE Edge Devices.*
Candidate protocol specification and profile proposal, draft, 2 October 2026.

Jonathan Yantis. *Passive Precision Time over BLE: Prospective Edge Calibration,
Causal Publication, and Service Availability.* Research preprint, draft,
2 October 2026.

Jonathan Yantis. *Causal Software Clock Discipline over Wireless Physical Events:
Native Lock Evidence, Operating Tiers, and a Tunable-Oscillator Extension.*
Amended research draft, 3 October 2026; matching PDF exported 9 October 2026.

Include the repository commit or edition manifest when citing a specific version.
No DOI or publication venue has been assigned here.
