# Research papers — drafts

**Latest main preprint, approved for public review, 30 October 2026:**
[PToB Viability: Rooted BLE Timing Meshes for Heterogeneous Edge Networks](ptob-viability-preprint.pdf)
is the 17-page synthesis of the source-scoped clock service, causal event
maps, heterogeneous relays, USB delivery and measured holdover.
[LaTeX source](ptob-viability-preprint.tex),
[source and evidence audit](editions/viability-2026-10-30-audit.json) and
[publication approval](editions/viability-2026-10-30-publication.json)
accompany the unchanged reviewed manuscript. It remains a preprint draft
and has not been submitted to arXiv.

**Approved experimental research draft, 30 October 2026:**
[Empirical Sub-Microsecond Holdover on BLE-Trained Software Clocks](mcxo-holdover-duration/ptob-empirical-sub-microsecond-holdover.pdf)
measures the longest supported hold duration at a fixed sampled 1 microsecond
threshold using P99 across frozen run-prefix maxima. Separate W12 devices,
observation windows, rolling populations, longer failures and unknown-expiry
display dips remain visible. A separately labeled offline one-second grid
refines the frontier on fixed mature long-run cohorts.
[Source, selected evidence and offline replay](mcxo-holdover-duration/README.md)
accompany the approved draft. LaTeX is canonical; selected inputs are archived
under `evidence/w12/`, and large derived tables regenerate locally.
[Approval and packaging](mcxo-holdover-duration/publication.json) preserve the
reviewed bytes; GitHub publication and firmware qualification remain separate.

**Public review draft, 26 October 2026:**
[Reducing Timing Tails and Multipath Sensitivity in an Operating BLE Precision Mesh](ble-tail-multipath/main.pdf)
reviews capture, event association, channel-aware fitting and source ownership,
then proposes placement, workload, estimator and recovery tests. The nine-page
draft separates delivery latency, correction age, clock error and availability,
preserving distinct W12/N30 populations and unexplained tails.
[Source and reproduction](ble-tail-multipath/README.md),
[analytical supplement](ble-tail-multipath/analysis.json),
[prepared edition](editions/ble-tail-multipath-2026-10-26.json) and
[publication record](editions/ble-tail-multipath-2026-10-26-publication.json)
accompany the unchanged manuscript. No new experiment, demonstrated multipath
cause or later-image qualification is claimed.

**Public review draft, 26 October 2026:**
[A Shared Time Base for Indoor PNT](indoor-pnt/main.pdf) investigates event timing
beneath heterogeneous navigation sensors, with selective UWB and other positioning
observations. It separates sensor-motion and radio-ranging budgets and proposes
causal clock bridges, sensor fusion and independent validation.
[Source and reproduction](indoor-pnt/README.md),
[prepared edition](editions/indoor-pnt-2026-10-26.json) and
[publication record](editions/indoor-pnt-2026-10-26-publication.json) preserve its
scope. Indoor localization, UWB integration and GNSS equivalence remain unqualified.

**Approved public research draft, 26 October 2026:**
[PToB-IPS: Toward GNSS-like Indoor Positioning with Surveyed BLE Timing Beacons](indoor-ips/main.pdf)
proposes surveyed timing beacons and a GNSS-like position/receiver-clock solve.
Aligned periodic advertising timers, assigned offsets and coordinated 16/8/4 Hz
rates let one receiver track multiple trains in distinct receive windows.
The theoretical study quantifies averaging, bias floors, capture resolution,
geometry and motion, with a roadmap for commodity MCU robotics and drone PNT.
UWB appears only as an optional intermediate reference or service.
[LaTeX source](indoor-ips/main.tex), [build and verification](indoor-ips/README.md),
[analytical model output](indoor-ips/analysis.json),
[prepared edition](editions/indoor-ips-2026-10-26.json) and
[publication approval](editions/indoor-ips-2026-10-26-publication.json)
accompany the unchanged reviewed paper. No positioning accuracy, IPS firmware,
GNSS equivalence or deployed-device compatibility is qualified. The original
preparation receipt preserves its pending-approval checkpoint; the separate
publication receipt records approval for public draft review.

**Approved public research draft, 24 October 2026:**
[Low-Level Timing on ESP32-S3: Event Capture, Clock Bridges, and Causal Validation](ptob-esp32s3-low-level-timing.pdf)
is a separate seven-page implementation guide, with
[LaTeX source](ptob-esp32s3-low-level-timing.tex),
[edition receipt](editions/s3-low-level-2026-10-24.json), and
[selected aggregate supplement](supplements/README.md).
It explains continuous capture, narrow IRAM clock bridges, causal dense fitting,
bounded curvature and programmed TX estimates. Its source and verification commands accompany this publication. The
aggregate-only evidence retains its reproduction limits,
unscorable downstream records, tails and failed full certification. It does not
replace the 22 October paper or claim new producer-fix hardware results.
The [publication approval](editions/s3-low-level-2026-10-24-publication.json)
records public-draft approval without changing the prepared edition’s byte identities.
The distinct [post-repair W12 baseline](../evidence/s3/2026-10-24-w12-utc-basis/README.md)
publishes all selected PPS/TX rows and an offline verifier for later website
results. It does not silently replace this approved paper's earlier aggregates.

These are working drafts by **Jonathan Yantis**. The latest approved main
preprint and W12 holdover-duration study have **30 October 2026** editions.
The BLE timing-tail/multipath, indoor PNT and theoretical PToB-IPS studies
retain their **26 October** editions. The low-level
ESP32-S3 implementation guide retains its **24 October** edition. The robotic
gPTP/TSN study and ESP32-S3 empirical timing study retain their **22 October**
editions; the earlier reviewed
edition remains preserved in Git. The shared time base and
Bluetooth timecode compatibility study retains its **22 October** edition.
The awake timing information and power scheduling pilot retains its
**19 October 2026** edition. The
USB/chrony and White Rabbit papers retain their **15–17 October** editions.
The main viability synthesis is **approved for public draft review**.
The V14 service/profile proposal has a **30 October author-review draft**;
its companion boundary-clock study retains its **12 October** edition.
All remain discussion/research drafts, without standards
adoption or firmware qualification. Earlier import receipts retain their original
byte identities; later editions are recorded separately.

## Main paper: PToB Viability

**[PToB Viability: Rooted BLE Timing Meshes for Heterogeneous Edge Networks](ptob-viability-preprint.pdf)**
is the starting point for the project's research: protocol architecture,
experimental evidence, scaling limits and a proposed path to PTP edge extension.
Read the **30 October 2026 approved public-review preprint** or edit its
[LaTeX source](ptob-viability-preprint.tex). The 17-page revision adds ESP32-S3
event capture, the W12/T096 relay baseline, USB RC/DC comparison, awake sampling
and measured frozen-clock holdover. It updates GM7/RC7 architecture, prior art,
application boundaries and public reproduction references. Separate cohorts,
tails, invalid/missing observations and unqualified UTC/output remain explicit.
The [edition manifest](editions/viability-2026-10-30.json) preserves the prior
15-page edition through its Git revision and binds the current source/PDF and
selected inputs. Run `python3 tools/verify_viability_paper.py` for identity and
claim checks; add `--replay` for the supporting offline numerical analyses.
The separate [publication approval](editions/viability-2026-10-30-publication.json)
records approval for public draft review. The manuscript and preparation
receipt retain their earlier author-review labels as the preserved review
checkpoint; their bytes are unchanged. The
[final source and evidence audit](editions/viability-2026-10-30-audit.json)
records reference, link and reproduction checks. This remains a draft
toward arXiv; it has not been submitted.

## Supporting research

| Draft | Read | Edit | Edition |
| --- | --- | --- | --- |
| Empirical Sub-Microsecond Holdover on BLE-Trained Software Clocks | [PDF](mcxo-holdover-duration/ptob-empirical-sub-microsecond-holdover.pdf) | [LaTeX](mcxo-holdover-duration/ptob-empirical-sub-microsecond-holdover.tex) | 30 October 2026, approved research draft; compact selected evidence and offline prefix replay |
| Reducing Timing Tails and Multipath Sensitivity in an Operating BLE Precision Mesh | [PDF](ble-tail-multipath/main.pdf) | [LaTeX](ble-tail-multipath/main.tex) | 26 October 2026, public review draft; current mechanisms, analytical examples and proposed validation, no new hardware experiment |
| A Shared Time Base for Indoor PNT: PToB Sensor Fusion, Selective UWB Augmentation, and a Validation Programme | [PDF](indoor-pnt/main.pdf) | [LaTeX](indoor-pnt/main.tex) | 26 October 2026, public review draft; proposed architecture and analytical budgets, no localization qualification |
| PToB-IPS: Toward GNSS-like Indoor Positioning with Surveyed BLE Timing Beacons | [PDF](indoor-ips/main.pdf) | [LaTeX](indoor-ips/main.tex) | 26 October 2026, approved public research draft; surveyed beacons, aligned multi-rate advertisements and theoretical positioning/refinement limits |
| Low-Level Timing on ESP32-S3: Event Capture, Clock Bridges, and Causal Validation | [PDF](ptob-esp32s3-low-level-timing.pdf) | [LaTeX](ptob-esp32s3-low-level-timing.tex) | 24 October 2026, seven-page implementation guide; relative measurements and calibration limits remain distinct |
| A Shared Time Base for Robotic gPTP and TSN Systems: PToB Edge Adaptation, Compatibility Boundaries, and a Validation Programme | [PDF](ptob-gptp-tsn-robotics-preprint.pdf) | [LaTeX](ptob-gptp-tsn-robotics-preprint.tex) | 22 October 2026, proposed wired-domain ingress, wireless acquisition timestamps and staged validation |
| Precision Bluetooth Timing on ESP32-S3: Causal Clock Discipline and Hardware Event Capture | [PDF](ptob-esp32s3-timing-preprint.pdf) | [LaTeX](ptob-esp32s3-timing-preprint.tex) | 22 October 2026 follow-up, empirical fine capture and completed 660 s relative PPS, target unmet |
| A Shared Time Base for Bluetooth Timecode Ecosystems: PToB Architecture, Compatibility Boundaries, and a Validation Programme | [PDF](ptob-bluetooth-timecode-preprint.pdf) | [LaTeX](ptob-bluetooth-timecode-preprint.tex) | 22 October 2026, proposed media contract and compatibility layers |
| How Much Timing Information Is Enough? Awake Hardware Evidence and Wake/Listen Energy Models for PToB | [PDF](ptob-power-scheduling-preprint.pdf) | [LaTeX](ptob-power-scheduling-preprint.tex) | 19 October 2026, awake PC117 pilot and hypothetical energy models |
| Beyond USB Packet Arrival: Frame-Identified Time Transfer to macOS | [PDF](usb-macos-time-transfer-preprint.pdf) | [LaTeX](usb-macos-time-transfer-preprint.tex) | 17 October 2026, shared-bus RC/DC revision |
| From Virtual Clock Discipline to Physical Synchronization: White Rabbit Lessons for PToB and Future Tunable Targets | [PDF](white-rabbit-study-20261015.pdf) | [LaTeX](white-rabbit-study-20261015.tex) | 15 October 2026 |

The robotics gPTP/TSN paper was **approved for public draft review on
22 October 2026** and remains a research draft. It has a
[22 October edition manifest](editions/gptp-tsn-robotics-2026-10-22.json) and
[offline analytical verifier](../tools/verify_robotics_edition.py). In the style of
the timecode study, it proposes a common domain/source/event/quality contract,
an Ethernet-PHC-to-radio clock bridge, ROS 2 acquisition-time adapters, explicit
motion/holdover/schedule budgets and independent validation gates. Its dated
primary-source review uses IEEE 802.1AS-2025 and the published IEC/IEEE 60802-2026
profile; full normative conformance remains unaudited. Historical PToB populations
remain separate. All robotics adapters, gPTP interoperability, Ethernet egress,
TSN conformance and physical actuation qualification remain future work. This
edition adds no hardware experiment, firmware change, submission or promotion
to a stable paper. The immutable edition receipt records local preparation
before the GitHub push; the [manuscript history](CHANGELOG.md) records the
subsequent public-review approval.

The ESP32-S3 follow-up is a **research draft approved for repository publication**, with a
[new edition manifest](editions/s3-timing-2026-10-22-fine.json) and
[selected sanitized follow-up](../evidence/s3/2026-10-22-fine/README.md).
The paper leads with measured results and retained tails, followed by the
implemented method, fault repairs and concrete remaining improvements; it drops
the general synchronization theory and numerical-clock model. Its completed
660-second window has 657 eligible and three invalid records,
zero nominal missing and an observed post-window fence. Actual relative-PPS
RMS is 166.590 ns, with a 1591 ns maximum; near-50 ns internal causal quality
is a different metric. A new 51016 ns offset reuses that window for calibration
and a fresh N31 transfer has 48 eligible / 64 records, +115.167 ns mean and
150.954 ns RMS; the same total is not mean-zero portable and the output target
still fails. Estimated TX and empirical RX endpoint
semantics still limit the output target. The [previous reviewed receipt](editions/s3-timing-2026-10-22.json)
and [incomplete original cohort](../evidence/s3/2026-10-22/README.md) remain
unchanged in Git. Prior public-review approval is recorded in the manuscript
history; this replacement has no stable promotion or submission.

The Bluetooth timecode paper was **approved for public draft review on
22 October 2026** and remains a research draft. It has a
[22 October edition manifest](editions/bluetooth-timecode-2026-10-22.json)
and [offline analytical verifier](../tools/verify_timecode_paper.py). It proposes
a shared time/quality/event contract, exact rational frame and sample mapping,
and ecosystem-specific adapters, with a dated manufacturer-source survey and
staged independent recorded-event validation. Commercial Bluetooth interoperability,
LTC/MTC generation, TAI firmware support and capture/genlock qualification remain
future work. Earlier evidence populations stay separate; no new hardware test,
vendor integration, submission or draft promotion accompanies this edition.
The immutable edition receipt records preparation before the GitHub push;
the [manuscript history](CHANGELOG.md) records the subsequent public-review approval.

The power scheduling paper has a [19 October edition manifest](editions/power-scheduling-2026-10-19.json)
and [sanitized offline supplement](../evidence/power/2026-10-19/README.md).
It reports all six PC117 runs, 42 refreshes in 120 windows, missing BLE coverage,
independent local PPS fractional phase and the single brief post-turnover interval.
RF remained on; scheduled training fractions are not measured airtime or energy.
Sleep implementation, power metering, physical wake and cold-start qualification
remain future work. Its arXiv-targeted preparation does not constitute submission.

The USB paper now has a [17 October edition manifest](editions/usb-macos-2026-10-17.json),
replacing its main results with a fixed concurrent 180-second RC/DC window.
The bus was shared, not clean; isolated-controller validation and completed
extended trials remain pending. USB replies, causal RX prediction, chrony
estimator, delivered agreement and sampled availability retain separate counts.
The [sanitized observation supplement](../evidence/usb/2026-10-17/README.md)
includes every sample in that window and offline summary recomputation.
The [original edition receipt](imports/research-2026-10-16.json) binds the earlier
three source/PDF pairs and selected research exports, preserved through Git. The
[research collection](../evidence/research/2026-10-16/README.md) includes USB delivery
and tracking samples and reproducible hypothetical viability tables. It retains
withheld samples, startup excursions and the distinction between relative clock
agreement and independent UTC. White Rabbit comparisons propose future work;
viability projections are not supported hop counts or measured energy savings.
Original internal paths in the manuscripts identify historical evidence; public
counterparts and exclusions are listed in the collection. No new hardware tests,
website deployment or manuscript submission accompanied this import.

## V14 proposal and boundary-clock study

| Draft | Read | Edit |
| --- | --- | --- |
| Passive Precision Time over BLE: V14 Candidate Service and Profile Proposal | [PDF](ptob-v14-sig-proposal.pdf) | [LaTeX](ptob-v14-sig-proposal.tex) |
| Passive Precision Time over BLE: V14 Boundary Clocks, Causal Timestamping, and Prospective One-Relay Validation | [PDF](ptob-v14-preprint.pdf) | [LaTeX](ptob-v14-preprint.tex) |

The **30 October 2026 author-review proposal** describes the current
GM7 broadcast election/HOT succession, RC7 dynamic paths and coarse TZ/DST
metadata. It contains no design-history comparison or new empirical accuracy
claim. Its [edition manifest](editions/v14-sig-2026-10-30.json) binds the matching
source/PDF and reviewed public source snapshots. Run
`python3 tools/verify_v14_proposal.py` for offline identity and coarse-wire checks.
Publication/submission approval is pending; the paper remains a proposal.

The unchanged **12 October boundary-clock study** has evidence cutoff
**2026-10-12T18:26:44.293859Z**, with specification
freeze `7995249` and selected application **0.14.43** at
`566ea2029c7ffc84a7d321df427c5c811fac2730`. The [edition receipt](imports/v14-2026-10-13.json)
records the original proposal/study source/PDF identities, build/review records,
rights, selected scientific exports and exclusions. The prior proposal remains
in Git at the revision named in the new edition manifest. The original receipt
is immutable; both imported PDFs matched the website copies at import.

The [V14 evidence collection](../evidence/v14/2026-10-12/README.md) provides
sanitized samples, figure/table exports, claim provenance, and offline checks.
It retains invalid references, expired maps, and missing opportunities. The
frozen RC1 proposal's [known normative issues](../evidence/v14/2026-10-12/provenance/ptob-v14-sig-issues.json)
retain their original scope. These historical results do not qualify GM7,
current firmware or calibrated output accuracy.

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

All original papers and their figures are **CC BY 4.0**, attributed to Jonathan
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

The LaTeX files in this directory are the working sources for this repository;
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
tectonic --outdir artifacts/papers papers/usb-macos-time-transfer-preprint.tex
tectonic --outdir artifacts/papers papers/white-rabbit-study-20261015.tex
tectonic --outdir artifacts/papers papers/ptob-viability-preprint.tex
tectonic --outdir artifacts/papers papers/ptob-power-scheduling-preprint.tex
tectonic --outdir artifacts/papers papers/ptob-bluetooth-timecode-preprint.tex
```

Tectonic may download its public TeX bundle on first use. Inspect the generated
PDFs before replacing the checked-in copies. Different TeX engines or bundle
versions may change pagination or PDF bytes. The October 2 manuscripts and V14
boundary-clock preprint retain original exports. The V14 proposal PDF matches
its 30 October source. The oscillator/PLL PDF was regenerated during migration
to match its existing amended source.

## Cite a draft

Jonathan Yantis. *A Shared Time Base for Bluetooth Timecode Ecosystems: PToB
Architecture, Compatibility Boundaries, and a Validation Programme.* Research
draft, 22 October 2026. Use `python3 tools/verify_timecode_paper.py` to verify its
analytical examples and edition identity; this is not device interoperability
or physical output validation.

Jonathan Yantis. *How Much Timing Information Is Enough? Awake Hardware Evidence
and Wake/Listen Energy Models for PToB.* Research draft, 19 October 2026.
Use `python3 tools/verify_power.py` to recompute its selected derived-data results
and verify its edition. No sleep energy or calibrated output qualification is claimed.

Jonathan Yantis. *Passive Precision Time over BLE: V14 Candidate Service and
Profile Proposal.* Author-review discussion draft, 30 October 2026.

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
