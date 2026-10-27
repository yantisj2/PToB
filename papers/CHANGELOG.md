# Manuscript history

## 2026-10-26 — latest multipath and indoor PNT drafts approved for public review

- Publish Jonathan Yantis's nine-page *Reducing Timing Tails and Multipath
  Sensitivity in an Operating BLE Precision Mesh* and the separate *A Shared
  Time Base for Indoor PNT*, with source, PDF, analysis/verification and licenses.
- Preserve reviewed manuscript/PDF bytes and original pending-approval receipts.
  Separate publication records bind the current approval and ancillary README
  and verifier changes. Preserve the previously approved PToB-IPS edition.
- Export only the multipath receipt's 18 explicitly selected historical source
  files with unchanged bytes and recorded provenance, so reproduction needs no
  unpublished branch. These are frozen research references, not current builds.
- Retain failures, tails, missing/invalid records and population boundaries.
  All papers remain research drafts; no new hardware experiment, demonstrated
  multipath cause, localization qualification or stable promotion is claimed.

## 2026-10-26 — PToB-IPS public draft review approved

- Jonathan Yantis approved the 15-page theoretical IPS paper for integration
  into main and GitHub public review. Preserve the reviewed LaTeX, PDF and
  analytical JSON bytes, with matching preparation and publication receipts.
- Add the paper to the research catalogue and root research section. Publish
  the standalone analytical/evidence verifier and build instructions. Update
  only its identity adapter and publication README; the analysis is unchanged.
- Keep the original preparation receipt immutable. Record approval and the
  ancillary changes separately. The earlier sensor-fusion draft remains local;
  its preservation seals are provenance metadata rather than public dependencies.
- The paper proposes surveyed beacons, GNSS-like joint position/clock estimation,
  aligned periodic advertising timers and a 16/8/4 Hz receive calendar. Clock
  averaging, native capture resolution, bias floors and motion remain theoretical
  budgets. No IPS implementation, positioning qualification, new hardware
  measurement, firmware change, stable promotion or submission is asserted.
- Publish only the paper and research integration. T096 branch work, other local
  firmware commits, private working material and unrelated changes stay local.

## 2026-10-24 — low-level ESP32-S3 public draft approved

- Jonathan Yantis approved the seven-page instructional paper for public review,
  its addition to the timing research page, and discreet links from the ESP32
  schematic and hardware guide. Publish the matching source, PDF, manifest,
  selected aggregate supplement and verifier together.
- Keep the approved PDF and LaTeX bytes unchanged. Record publication approval
  separately from the immutable preparation receipt. The paper remains a draft;
  approval does not establish calibrated UTC or full timing qualification.
- The authorized source push includes W12 0.2.10 and T096 0.15.169, including the
  UTC-basis fix, regressions and documentation. Private enrollment, images and
  raw laboratory records remain excluded.
- Publish the later post-169 W12 baseline as a separate dated evidence collection:
  all 600 W12 and 600 downstream PPS records, 120 selected TX records and 600
  GNSS predictions. Preserve invalids and tails, with reproducible statistics;
  the approved paper's earlier populations and bytes remain unchanged.

## 2026-10-24 — low-level ESP32-S3 implementation draft

- Add a distinct seven-page instructional draft and matching LaTeX/PDF; preserve
  the reviewed 22 October empirical paper and its manifests unchanged.
- Explain driver-owned continuous MCPWM capture, counter extension, exact packet
  joins, narrow IRAM clock apertures, capture-frozen causal predictions, dense
  channel-aware fitting, bounded GNSS shadow curvature and four-projection
  programmed TX estimates. Keep source expiry and activation races explicit.
- Select sanitized aggregate observations with explicit CC BY 4.0 licensing;
  retain source populations, downstream missingness, all tails, unknown RF/UTC
  bounds and FAIL full certification. Public raw-row recomputation is unavailable
  for the new observations. Add an offline identity/accounting/equation verifier.
- Record the T096 whole-second source repair without claiming fresh hardware
  results. Current source pins and this paper await separate publication.
  No hardware action, source push, deployment or draft promotion accompanies
  this preparation.

## 2026-10-22 — ESP32-S3 follow-up repository publication

- Jonathan Yantis approved committing and pushing the measured-results follow-up,
  matching source/PDF, selected evidence and research-hub links to GitHub with
  the N30 firmware changes. Retain draft status, all reported failures and the
  distinction between internal causal RMS and downstream relative PPS error.
  This does not promote a stable edition or submit the paper to a venue.

## 2026-10-22 — ESP32-S3 empirical fine-capture follow-up draft

- Replace the working source/PDF with an eight-page research-draft follow-up;
  preserve the previous reviewed edition and manifest in Git and link its bytes
  from the new `s3-timing-2026-10-22-fine.json` receipt.
- Lead with measured output, complete-window coverage and all retained tails.
  Remove general synchronization theory and the formal numerical-clock model;
  retain the implemented method, reproducible accounting and concrete next steps.
- Quantify four eligible excursions above 1 us (39.302% of uncentered squared
  error) without removing them. Add public recomputation of that tail accounting.
- Report exact packet-associated empirical RX-fall capture, one continuous
  counter and a separate sparse causal comparison, with both target failure
  and unavailable cases retained. RF END/ADDRESS and physical accuracy remain
  unqualified; TX remains a programmed software estimate.
- Add the completed fixed 660-second observation: 657 eligible, three invalid,
  zero nominal missing, observed post-window fence; RMS 166.590 ns and maximum
  1591 ns. Keep the old incomplete window and prior best short legacy result
  distinct. Preserve all tails and the internal-quality/output distinction.
- Record 51016 ns as a new provisional offset selected after reusing the
  completed cohort for calibration; a separate fresh N31 first-48 transfer retained 48 eligible + 16 invalid,
  mean +115.167 ns, RMS 150.954 ns and maximum 362 ns. Same-total transfer
  retained positive bias and failed the sub-100 ns target; no retuning.
  Add sanitized rows, reproducible standard-library verification and proposed
  endpoint/TX/causal/tail-reduction gates. No stable promotion, submission,
  commit or push is authorized by this local preparation.

## 2026-10-22 — ESP32-S3 public draft review

- Jonathan Yantis approved publishing the reviewed ESP32-S3 timing study and
  its selected sanitized evidence alongside the robotics paper, so the cited
  source populations and offline checks are publicly obtainable.
- Publish the matching source/PDF, immutable receipts and verifier together.
  Preserve the original incomplete 660-second outcome and all evidence limits.
  This approval does not publish N30 firmware development, qualify hardware,
  submit either paper or promote a stable edition.

## 2026-10-22 — robotic gPTP/TSN public draft review

- Jonathan Yantis approved adding the robotic gPTP/TSN paper to the public
  research collection. Publish the reviewed PDF, matching LaTeX, immutable
  edition receipt and offline verifier together, with catalogue and root links.
- Retain research-draft status, historical evidence populations and all stated
  limits. This approval does not qualify interoperability, standards conformance
  or hardware, promote a stable edition, or authorize manuscript submission.

## 2026-10-22 — robotic gPTP/TSN edge-adaptation research draft

- Add a standalone paper and matching PDF in the Bluetooth timecode study's
  style, with a dated primary-source review, original architecture figure,
  proposed domain/source/event/quality contract and ROS 2 acquisition adapters.
- Distinguish wired-domain ingress, later isolated Ethernet egress and the
  unimplemented BLE media adaptation; derive motion, holdover and schedule
  examples and propose independent validation with complete failure accounting.
- Add an offline analytical verifier and source/PDF/build/review receipt.
  Preserve separate historical populations and prior editions. No firmware
  change, new hardware measurement, conformance claim, submission, push or
  public-review promotion accompanies this local preparation.

## 2026-10-22 — ESP32-S3 precision timing research draft

- Add an eight-page draft with matching LaTeX/PDF and a selected, sanitized
  evidence supplement for causal S3 timing, aggregate relay calibration and
  a prospective 660-second relative-PPS attempt. Retain its incomplete ending,
  invalid reference record, nominal missing opportunities and earlier failures.
- Compare with a separate historical Nordic population and distinguish
  production controller timestamps from the default-OFF 80 MHz observer.
  Packet endpoint, path-delay and independent output qualification remain open.
- Include an offline statistical verifier, exact edition/source/PDF identities
  and provenance. No submission, push or promotion of the research draft.

## 2026-10-22 — Bluetooth timecode public draft review

- Jonathan Yantis approved the 22 October Bluetooth timecode paper for public
  draft review alongside the repository's other research. Publish the reviewed
  PDF, matching LaTeX, immutable edition receipt and offline verifier unchanged.
- Link the paper from the root research section and record its public-review
  status in the paper catalogue. This approval does not promote a stable edition,
  submit the manuscript to a venue or qualify the proposed compatibility layers.

## 2026-10-22 — shared time base and Bluetooth timecode compatibility

- Added a standalone research draft and matching PDF proposing a time/quality/event
  contract, exact rational media coordinates and ecosystem-specific presentation
  adapters, with an original vector architecture figure.
- Added a dated primary-manufacturer/standards survey, explicit proprietary-interface
  and legacy-quality limits, separate historical PToB evidence populations, an
  unknown-preserving error budget and staged independent recorded-event validation.
- Added an offline arithmetic/edition verifier and a new source/PDF/build/review
  manifest. Preserved prior editions and draft status; no firmware change, new
  hardware measurement, vendor integration, submission, push or promotion.

## 2026-10-19 — awake timing information and wake/listen power potential

- Added the new standalone research draft, matching PDF, four reproducible vector
  figures, explicit energy/drift scenarios and concrete physical sleep test protocol.
- Added selected sanitized PC117 derived observations, failures, denominators,
  coverage, claim provenance and public offline checks; earlier populations remain
  separate and original seals unchanged.
- Bound source/PDF, build identity and evidence in a new edition receipt. Retained
  draft status; no physical power/sleep test, arXiv submission or release.

## 2026-10-16 — USB/chrony, White Rabbit and viability research

- Added three unchanged source/PDF pairs: October 15 USB/chrony (11 pages),
  October 15 White Rabbit study (10 pages), and October 16 viability (15 pages).
- Preserved draft status and older editions; added explicit selection/provenance,
  three private-field redactions, matching hashes and destination visual review.
- Added delivery/tracking CSV checks and reproducible hypothetical model tables.
  Raw captures remain excluded; no accuracy, continuity or target acceptance upgrade.

## 2026-10-13 — V14 successor papers and research

- Added the October 12 V14-RC1 proposal (22 pages) and V14 research preprint
  (19 pages), preserving their source/PDF bytes and draft status.
- Added an immutable import receipt and separate V14 scientific exports,
  literature/build/review provenance, and the frozen RC1 contract and vectors.
- Preserved all observed PPS classifications, missing opportunities, expiry
  counters, and normative implementation issues; no precision claim was upgraded.
- Added public offline population/document checks and figure reproduction that
  require no private checkout. Earlier manuscripts and manifests are unchanged.

## 2026-10-09 — oscillator/PLL and evidence import

- Added the amended October 3 oscillator/PLL source unchanged.
- Exported a matching 17-page PDF, replacing the preceding 16-page snapshot
  for this repository; virtual-lock and proposed SoftPLL text are now included.
- Added the edition receipt, historical paper evidence, and PLL derived samples.
- Kept v12 model, R2 simulation, and R2 hardware records in separate collections.
- The October 2 proposal/preprint bytes and scientific claims are unchanged.

## 2026-10-09 — initial repository baseline

- Imported the latest matching proposal and main research preprint pairs,
  both dated 2026-10-02, unchanged in content and PDF bytes.
- Retained draft status, author credit, AI assistance disclosure, and the
  2026-10-02 evidence cutoff. No v12/R2 results added to either paper.
- Applied CC BY 4.0 through the companion license notice and repository policy.
- Established local Git history as the record for subsequent manuscript edits.
- Supporting research datasets and protocol implementation remain pending.

See [the immutable import receipt](imports/2026-10-09.json).
