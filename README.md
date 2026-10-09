# PToB — Precision Time over BLE

PToB explores precision time distribution over Bluetooth Low Energy: passive
reception, explicit timestamp provenance, and clock quality appropriate to each
device. This repository brings together the draft protocol proposal, research
papers, and supporting evidence for public review.

**Research in progress.** These are drafts, not peer-reviewed papers or an adopted
Bluetooth SIG or IEEE standard. Results apply to their stated experiment and
implementation; simulation and hardware measurements are kept separate.

| Paper | Edition | Read |
| --- | --- | --- |
| Passive time service and protocol proposal | 2 October 2026 draft | [PDF](papers/ptob-proposal.pdf) |
| Prospective edge calibration, causal publication, and service availability | 2 October 2026 preprint | [PDF](papers/ptob-preprint.pdf) |
| Software clock discipline, native lock, and tunable-oscillator extension | 3 October 2026 amended draft; matching PDF exported 9 October | [PDF](papers/oscillator-pll-research-20261003.pdf) |

[Editable sources, edition history, and build instructions](papers/README.md)
accompany the papers. Future draft revisions are preserved in Git.

| Evidence collection | What it contains |
| --- | --- |
| [Earlier paper evidence](evidence/papers/2026-10-02/README.md) | Original claim mappings, calibration, populations, and protocol/firmware identities at the October 2 cutoff |
| [Oscillator/PLL evidence](evidence/papers/oscillator-pll-2026-10-03/README.md) | Separate hardware-derived cohorts, causal-error samples, sparse lock observations, and retained failures |
| [v12 model](evidence/v12/model/README.md) | Frozen numerical model, tests, synthetic results, and experiment receipts |
| [R2 simulation](evidence/v12/r2/simulation/README.md) | Connected-control and P1 controller/PHY simulation, including failed cases |
| [R2 hardware](evidence/v12/r2/hardware/README.md) | Dated T096 installation, startup, UI, and service records with unresolved issues |

Start with the [claim and evidence index](evidence/README.md) for scope and
reproduction limits. Private raw captures are not included. Low relative timing
error, software lock, and simulation success do not establish calibrated absolute
UTC or independent electrical-output accuracy.

The [T096 R2 firmware example](firmware/t096/README.md) is being prepared; a
standalone buildable firmware release is not yet included.

Project website: [ptob.dev](https://ptob.dev). Review comments should identify the
paper edition or repository commit and the relevant claim or evidence record.

By **Jonathan Yantis**, with [acknowledged Codex assistance](CREDITS.md).
Original code is [MIT](LICENSE); papers, figures, and the included original
evidence datasets are **CC BY 4.0**. See [licensing and attribution](LICENSING.md).
