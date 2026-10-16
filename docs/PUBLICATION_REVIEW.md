# Publication review

Copyright (c) 2026 Jonathan Yantis. MIT.

The repository can be reviewed as an experimental source and research project.
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
