# Publication review

Copyright (c) 2026 Jonathan Yantis. MIT.

The **4 November 2026 [sparse LoRa timing paper](../papers/lora-sparse-timing/sparse-one-way-lora-clock-transfer.pdf)**
is approved for public draft review. Its [approval record](../papers/lora-sparse-timing/publication.json)
binds the unchanged reviewed seven-page manuscript under descriptive PDF/LaTeX
filenames, with the original preparation record preserved. The calibrated
900-PPS T096 result, bandwidth-screen limits and incomplete populations remain
unchanged. This repository packaging is committed locally; the user deferred
the push until the firmware fixes are ready. No website deployment, new capture,
final-paper promotion or hardware qualification accompanies this approval.

The **3 November 2026 [Starlink hybrid timing paper](../papers/starlink-wifi-hybrid-timing/starlink-wifi-hybrid-timing.pdf)**
is approved for public draft review. The [approval record](../papers/starlink-wifi-hybrid-timing/publication.json)
binds the unchanged reviewed seven-page manuscript and PDF under their named
canonical paths. Its selected two-run measurements, full estimator matrix,
prior GNSS phase calibration and historical failures remain explicit.
The [previous PDF review link](../papers/starlink-ntp-frozen-clock/main.pdf)
is retained as a byte-identical copy. This approval
covers repository research packaging; hardware qualification and website
publication remain separate.

The **2 November 2026 [Pi/M-HAT gateway draft](../papers/pi5-mhat-gateway/main.pdf)**
and its complete final measured population are approved for public draft review.
The [approval record](../papers/pi5-mhat-gateway/publication.json) preserves the
reviewed source/PDF and records authorization for GitHub main publication,
with unpublished LoRa development excluded. The package includes the Pi target, separate setup guide,
scripts and generic monitoring screenshots; private access details and raw lab
captures are excluded. Ethernet/USB/reverse-mode accuracy remains unqualified.

The **30 October 2026 [PToB Viability preprint](../papers/ptob-viability-preprint.pdf)**
is approved for repository publication for public draft review. Its
[publication receipt](../papers/editions/viability-2026-10-30-publication.json)
binds the unchanged reviewed source/PDF; the
[source and evidence audit](../papers/editions/viability-2026-10-30-audit.json)
records the final reference and reproduction checks. It remains a draft
and has not been submitted to arXiv.

The [16 October public-source review](PUBLIC_RELEASE_REVIEW.md) records completed
checks and fixes. The repository is ready for experimental source and research
publication within that scope.
A public source release and an install-first firmware release have different
acceptance requirements. Local migration/builds do not imply approval to publish.

## Source and research review

- Review each target README, selected-source manifest, preserved third-party
  notices and build/test records. Resolve licensing questions before distribution.
- Check the complete proposed Git history and release assets for credentials,
  real enrollment, generated headers, enrolled binaries and private raw logs.
  `.internal/` and `artifacts/` must remain untracked.
- Reproduce pinned dependency acquisition and documented builds from a clean
  checkout without a private sibling repository. Local cache builds are useful
  evidence but do not close the clean-host gate.
- Confirm root/target links and commands, role descriptions and shared registry
  behavior. Separate current operating instructions from frozen research.
- Review paper source/PDF pairs and their immutable edition manifests together.
  Preserve draft status, original source/evidence identities, missing populations,
  retained failures and limits on independently verified accuracy.
- Agree the public repository URL, default branch, release description and rights
  notices before publishing. Keep old-repository redirects pointed at the actual
  canonical repository; local moved notices are not remote archival.

## Install-first release gates

Generic images and runtime enrollment are not yet supplied. Before offering
end-user downloads, implement and validate generic-device provisioning, storage,
key replacement, identity binding, app-only installation, rollback and recovery.
Verify that public binaries contain no deployment key. Define supported hosts and
boards, signing/checksum verification and an actionable failure path. Publish
hardware acceptance by exact image/configuration, not by a family name alone.

The two-GM setup and experimental relays also need dedicated source-loss,
recovery, continuity and timing qualification before claims of reliable field
failover or calibrated precision. macOS private chrony observations do not qualify
LAN distribution or system-clock discipline.
