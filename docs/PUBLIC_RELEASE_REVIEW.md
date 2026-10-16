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
