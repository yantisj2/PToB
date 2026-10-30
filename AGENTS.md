# Repository guidance

This repository contains curated publication material. Read README.md and the
relevant collection README before changing it. Do not infer current firmware
acceptance or precision qualification from a historical document or simulator.

## Local work

Mesh selection must be discovered from the local area, never pinned to a single
mesh ID in a production image. Keep trusted credentials separate from runtime
mesh selection and require fresh authenticated admission after a context change.

When present, read `.internal/README.md` and `.internal/PLAN.md` for the active
local migration assignment. `.internal/` is ignored private working material:
never stage, force-add, publish, or copy it into public documentation. Its absence
in a fresh clone is expected. Keep private keys, enrollment, raw lab logs, and
unreviewed imports out of tracked files.

## Build output retention

Native builders use `tools/firmware_artifacts.py` to release compiler output and
retain the latest two successful bundles per device/profile/configuration, with
installed/recovery pins preserved. See `docs/firmware-storage.md`. Do not archive
whole compiler trees or bypass this lifecycle. After using historical builders
without the hook, run the shared plan/apply routine from the owning repository
once those builders have stopped. Preserve receipts, failure logs and evidence;
use ignored `artifacts/local/firmware-retention.json` for explicit candidate pins.

## Publication discipline

- Import explicitly selected files with recorded provenance; no wholesale sync
  from a private workspace and no private Git history import.
- Keep paper editions, wire profiles, source versions, and evidence populations
  distinct. Preserve failures, missing data, denominators, and limitations.
- Keep target and client READMEs focused on current capabilities, specifications,
  setup, controls and limits. Put release/version narratives and dated results
  in changelogs or linked evidence; use Git for superseded instructions. Protocol
  identifiers needed for compatibility belong in the current specifications.
- Separate model, controller simulation, hardware observation, relative timing,
  and independently measured output. Checksums establish identity, not accuracy.
- Use public, obtainable dependencies and reproducible commands. Do not require
  a sibling private checkout or private credentials for ordinary verification.
- Licensing rule: original code and ordinary repository documentation use MIT;
  original papers (PDF and LaTeX) and their figures use CC BY 4.0. Credit Jonathan
  Yantis. Original imported evidence records and derived CSV datasets use CC BY
  4.0; snapshot code uses MIT. Preserve third-party terms. See LICENSING.md.
  Record any future dataset license explicitly.
- Papers remain drafts until explicitly promoted. Develop their sources here,
  preserve prior editions in Git, and commit source/PDF updates with matching
  manifests and a changelog entry. Never silently overwrite a reviewed edition.
- Use evidence/README.md to select the appropriate historical collection. Frozen
  model documents and historical experiment records are context, not current
  operating instructions or hardware authorization. Do not rewrite their hashes.
- Validate changes with relevant checks; document checks that cannot yet run.
- Do not push, publish releases, deploy, operate hardware, or alter the private
  laboratory as part of repository preparation unless separately authorized.

## T096 development authority

PToB owns normal T096 RC7 development. GM and RC are the ordinary mesh
profiles; DC is a separate flash-only GNSS/USB image with Bluetooth disabled.
Keep DC out of the runtime GM/RC switch. USB source streaming is standard. Read firmware/t096/README.md and
docs/t096/build.md before firmware changes. Keep device/group enrollment and
secret-bearing images under ignored local artifacts. Hardware operations still
require explicit scope, exact identity, ownership, recovery and review checks.

## W12 development authority

PToB owns normal W12 native G1/RC7 development. Read firmware/w12/README.md and
docs/w12/build.md before W12 changes. RC is the supported public profile; BLE50
is client-only and the optional BLE100 estimated relay remains unqualified. Do
not invent GM support or infer GNSS wiring from an alias. All targets share
registry/; real factory bindings, enrollment and images remain ignored locally.
Hardware work requires its separately authorized exact-device scope and guards.

For routine app-only updates, do not back up the full flash or copy the installed
application on every flash. Reuse the identified, verified recovery image. Keep
exact-device ownership, bounded application drain, app-only write bounds, actual
application readback and protected-prefix comparison. Preserve a partial final
sector only when needed to keep bytes outside the application unchanged. A full
flash backup requires a separate explicit operator request.

## SS3 and macOS development authority

PToB owns SS3 native G1/RC7 and the macOS USB/chrony client. Read
firmware/ss3/README.md or clients/macos/README.md and the corresponding build
guide before changes. SS3 is M5Stack StickS3 K150, RC BLE100 with experimental
estimated TX; its CDC console is not a macOS UTS1 timing source. macOS consumes
compatible T096 USB timing and owns an unprivileged local chronyd -x instance.
Normal client launch operates hardware/starts that daemon; offline self-tests do
neither. Network serving and OS clock discipline are not configured. Preserve
third-party chrony terms, shared registry format, source expiry and unknown bounds.

## N30 offline development

Read firmware/n30/README.md and docs/n30/build.md before N30 changes. N30 is an
offline RC-only ESP32-S3 target with no GNSS. Reuse W12 timing behavior and
preserve W12/T096 firmware. N3-1 and N3-2 receive their shared-map entries only
after exact factory identity verification; never invent identities. Native builds
and both portrait previews are offline preparation, not hardware acceptance.
Hardware testing/installation remains deferred until arrival and separate scope.
