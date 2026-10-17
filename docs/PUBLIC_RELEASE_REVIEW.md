# Public source release review — 16 October 2026

Copyright (c) 2026 Jonathan Yantis. MIT.

**Ready to publish for experimental source and research review.** This conclusion
covers the selected source, documentation and draft papers. It does not approve
enrolled binary downloads, general firmware installation, OS clock discipline or
new hardware qualification. No push, release, device operation or live chrony
service was performed for this review.

## Privacy and provenance

Reviewed all unique blobs reachable from the proposed branch, including older
versions, for known local deployment PSK encodings and device IDs, private-key
blocks, common access-token patterns, private checkout paths and private network
addresses. No credential or deployment-identity findings remained. The only
path-pattern hit was the privacy check's own regex. These checks are bounded
screening, not proof that every possible secret format can be detected.

No private planning, build outputs, enrolled images or raw capture logs are
tracked. Ten tracked PDFs were checked for private metadata paths, embedded
attachments and local-file links; no findings. Author attribution and intentional
historical unit aliases remain public. Original evidence paths identify excluded
historical archives rather than promising those files in a clone.

W12, SS3 and macOS imported-file hashes match their manifests. The T096 receipt
records an earlier extraction stage, not the final public application; its
[provenance note](../firmware/t096/imports/README.md) makes this distinction
explicit. Actual build receipts bind the tested public source and image bytes.
The original research/evidence manifests and paper editions remain unchanged.

## Licensing and navigation fixes

Included the full Apache-2.0 license for explicitly marked imports and a companion
copy of Intel's TinyCrypt BSD-3-Clause notice. The root licensing policy now
identifies those exceptions to MIT and distinguishes separately acquired GPL
chrony. Existing per-file notices remain intact. Original papers, figures and
evidence retain CC BY 4.0. Future binaries require a linked-dependency and
corresponding-source review of their own.

Repaired the sole missing Markdown destination with an explicitly labeled
historical-plan navigation note, preserving the frozen document's bytes/hash.
All local Markdown destinations and the entry-page section anchors were checked.
Seven external links used by entry/build documentation returned HTTP 200. This
was not a re-audit of every historical bibliography URL. The main README features
PToB Viability and links to each target, setup, security and porting guide.

## Fresh-checkout build validation

A clean clone used synthetic enrollment and the public example registry. Public
SDK sources, archives and tools were acquired into fresh isolated directories;
no private sibling source or existing private SDK checkout was required. This
ran on an existing Apple Silicon macOS development host with its documented
system prerequisites, not a newly installed OS.

| Target | Source revision | Result |
| --- | --- | --- |
| T096 GM and RC | `36d6ec9` | Both builds and immutable artifact/layout inspections pass using newly acquired Zephyr/SDK pins |
| W12 RC BLE100 | `736fe26` | Build and source/runtime/layout inspection pass using newly acquired ESP-IDF and toolchain |
| SS3 RC BLE100 estimated relay | `36d6ec9` | Build and source/runtime/layout inspection pass using the same newly acquired public SDK |
| macOS client | `36d6ec9` | Native build and offline source/decoder/CLI/terminal/analysis checks pass |
| Chrony 4.9 | Pinned public archive | Fresh download/hash check and local build pass; version command only, no daemon start |

The first fresh W12 configuration exposed an unused component manager's denied
host process-enumeration call. The builder now disables that manager, matching
SS3: this selection has no managed components and uses the pinned SDK. The
corrected clean-checkout build passed. The failed attempt is retained privately.
A separate T096 inspection invocation initially used a Python without pyelftools;
the inspector also lacked a command-line entry point, so direct execution did
not invoke its validation function. Added and documented that entry point, then
verified both real builds in the dependency environment and checked rejection
of a missing argument and an invalid artifact receipt. The builder already called
the validation function during the successful builds above.

T096 core, USB, watchdog and runtime-role checks passed. W12 host/vector checks
and SS3's eight production programs passed; each ESP target passed 36 permission
model cases and nine native cases. macOS passed its 321 native checks, strict USB
decoder, 23 CLI, 47 synthetic chrony UI, 10 display-hold and eight analysis tests.
Evidence verification passed 38 historical hashes, 33 V14 hashes and 23 recent
research hashes, with the documented derived populations/model outputs. Registry
and offline provisioning checks also passed. No result is a new physical test.

## Publication boundary

Publish the reviewed source branch and documentation to the configured PToB
GitHub repository, retaining history and notices. Keep ignored local output out
of uploads and release assets. The source changes above are committed locally;
publication itself still needs the user's instruction.

Public generic provisioning, installation/recovery and enrolled-image-free
release packaging remain future work. Other build hosts, a freshly installed OS,
field reliability, multi-GM failover continuity, independently calibrated output
and UTC accuracy remain unqualified. Those limits are already explicit in the
target guides and do not prevent an honest experimental source publication.

## Stability update — 16 October 2026

The user subsequently authorized integration and public repository publication
of the accumulated fixes and latest bug-fix results. T096 0.15.69, W12 0.1.7 and
the macOS continuity/recovery changes are integrated, preserving the completed
development histories. The [changelog](../CHANGELOG.md) and
[curated stability collection](../evidence/stability/2026-10-16/README.md) record
implemented changes, exact historical populations, failed trials, skipped
verification and remaining limits. The USB lifecycle repair is proposal-only.
TC-04's persistent USB failure is explicitly unresolved.

Fresh offline publication checks on the integrated tree passed:

- T096 core, USB source/race/SOF and EP0, admission, delivery, durability,
  publication, watchdog, health and runtime-profile runners; nine logger tests.
  The older publication fixture was updated with the newly required source and
  parent incarnation fields. Its negative fixture still fails at the intended
  immediate-publication assertion. No production change was needed for that
  fixture correction.
- W12: 160 recorded native host build/test steps; selected permission checks
  with 36 model, 14 native and 49 server cases, including the stack-budget gate.
- SS3: eight production model/renderer/owner programs, including the integrated
  strongest-nearby-relay display regression.
- macOS: isolated source build, 8,796 native checks, ownership/strict-decoder
  checks, 23 CLI, 47 UI, 10 readout, eight analysis and eight ChronyControl
  adapter checks. The adapter's temporary Unix-socket test initially encountered
  the sandbox restriction and passed outside it. It starts no chronyd and opens
  no USB. The user's running binary/helper were not rebuilt or replaced.
- Historical, V14 and research manifests/derived checks, the new stability
  summary verifier, provisioning and registry tests. Original import manifests
  remain historical; ordinary development changes are recorded in Git.

The selected firmware production files match the earlier clean v69 and W12
0.1.7 build snapshots. This publication pass ran native checks rather than new
SDK firmware builds or hardware trials. It does not extend the earlier
clean-checkout build record to every later configuration.

The proposed public history and final source/documents were screened for known
local enrollment encodings, device identities, private paths and common secret
patterns. The existing privacy checker's own path regex was the sole benign
pattern finding. Local Markdown destinations resolve. No private planning,
raw lab captures, enrollment or enrolled images are selected for publication.
The user-supplied ChronyControl screenshot is a separate selected documentation
asset: its title bar was cropped with permission, graph pixels checked unchanged,
provenance recorded and the author's page linked. It does not change the
structured evidence populations.

The live chrony session, devices, source roles and background logging were not
operated by this publication work. No binary release or website deployment is
included. Durability, independent UTC/output metrology and HOT failover remain
unqualified.

## USB comparison publication preparation — 17 October 2026

The current source includes the separate T096 DC image, shared RC/DC immutable
USB publication, receiver RMS measurement and revised USB/macOS research draft.
DC remains outside the normal GM/RC switch. The current production/test source
is unchanged from the checked `6c6f1b2` integration; its clean GM/RC/DC builds,
profile-switching and host test results remain separate from this data review.
This preparation did not rebuild SDK images or perform hardware operations.

The [current data supplement](../evidence/usb/2026-10-17/README.md) attaches all
11,520 USB samples and 354 status points in the already selected concurrent
180-second window. Public CSV verification reproduces RX RMS/mean/sigma,
warm-up/rejection ledgers, direct comparison RMS/mean/tails, tracking-readout
statistics and sampled delivery counts. Changed-data and changed-hash negative
checks are rejected. Tracking medians are explicitly status-sampled cached
readouts; all numerical results are unchanged.

Historical evidence, V14, research, stability and current USB verification pass.
The existing paper source compiles with the editor and Tectonic; its matching
PDF has nine inspected pages with unchanged typography and fewer words. Edition
hashes bind source, PDF, summary and the new data manifest. Earlier editions and
frozen import/evidence hashes remain intact. Eight paper PDFs were screened for
private metadata paths, attachments and local-file actions, with no findings.

The proposed history and tracked/unignored files were screened for known local
enrollment encodings, deployment identities, private paths and common credential
patterns. The sole pattern finding was the existing privacy checker's literal
path regex. Local Markdown targets resolve. No raw captures, enrollment, compiled
firmware, private planning or temporary build files are selected for publication.
These are bounded checks, not proof against every possible secret encoding.

The comparison used a shared USB bus. Clean-bus repetition, independent UTC and
endpoint calibration, and complete extended-trial review remain pending. This
is preparation for experimental source/results publication; it does not promote
draft papers, qualify generic binary downloads or deploy the website.
