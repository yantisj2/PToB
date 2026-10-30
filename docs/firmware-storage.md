# Firmware build storage

Copyright (c) 2026 Jonathan Yantis. MIT.

Native firmware builds automatically release compiler intermediates and rotate
image retention after their existing native validation succeeds. All five target
builders, including both CoreS3-SE entry points, use `tools/firmware_artifacts.py`.
No hardware is opened, no recovery backup is created, and no image is published.

## Retention

Keep the latest two successful builds for each exact device, profile and selected
radio/clock configuration. Version precedes receipt modification time when
ordering builds. The just-completed output is also protected for that invocation,
even when rebuilding an older branch; pin it explicitly if it must survive later
rotation. Development and offline builds have separate retention groups;
they cannot replace deployment candidates. A failed build never replaces a good
image. There is no new group for every version, commit, enrollment or calibration.

Current-image and recovery JSON records under `artifacts/local/` pin their
referenced bundles, including references through other JSON records and symlink
aliases. These safety pins can retain more than two images. To protect an
additional candidate, create ignored `artifacts/local/firmware-retention.json`:

```json
{
  "pins": [
    {"path": "artifacts/build-rc-01", "reason": "Reviewed candidate awaiting installation"}
  ]
}
```

Remove an explicit pin only when its reason no longer applies. A historical
reference inside a current/recovery record is conservatively treated as a pin;
changing installed-state records is a separate operator decision. The cleanup
report lists unresolved historical references without fabricating their files.

Retained bundles stay at their existing paths. Cleanup removes their private
Python/CMake caches and unbound compiler object, archive and dependency files.
It preserves receipt-bound artifacts, generated sources, headers, effective
configuration, images, symbols, maps, stack-usage evidence and source snapshots.
This retains compatibility with existing native inspectors.

Retired builds lose their generated `target/` contents and root T096 UF2, except
GCC `.su` stack evidence and compiler/CMake logs. Their source snapshots, local
inputs, receipts and logs remain. `storage-retired.json` records the removal separately; historical receipt
hashes are never rewritten. A retired receipt describes historical output and
must not be treated as an available or installable image.

SDKs/toolchains, enrollment, calibration, experiment logs, recovery/readback files
outside build directories, and unrelated artifacts are not cleanup targets.
Keep using a shared pinned SDK through `--deps` or `--idf`; do not acquire another
SDK for every run. No unbounded compiler cache is introduced by this tool.

## Existing-output cleanup

From the repository root, generate and inspect a plan before applying it:

```sh
python3 -B tools/firmware_artifacts.py plan \
  --out artifacts/storage-plan.json
python3 -B tools/firmware_artifacts.py apply \
  --plan artifacts/storage-plan.json --out artifacts/storage-result.json
```

The default plan compacts retained builds and retires older unpinned images.
Use `plan --mode compact --out artifacts/storage-plan.json` to release only
unbound intermediates and caches while preserving every image.

Each plan records the build receipt digest, candidate-file inventory digest,
allocated bytes, retention groups, protected references and skipped builds.
Apply regenerates the plan under a shared exclusive lock and rejects stale
inputs before deleting anything. It rechecks current/recovery records and each
candidate build before changing it. A changed file, new build, changed pin,
symlink deletion target, path escape or tracked file prevents that deletion.
Skipped or interrupted operations are recorded in the result and private journal.
An interrupted retirement can resume with a newly generated plan.

Ordinary plans exclude receipts modified within the last hour. The public
builders hold the same lock throughout native compilation and cleanup, and
exempt only their own newly completed output from that age guard. Old builders
in historical checkouts do not take this lock: stop those processes before a
manual migration. Frozen checkouts nested under `artifacts/` use the outer
repository's lock, current/recovery records and retention lifecycle; `--repo`
also resolves to that outer owner. Independent checkouts outside that tree use
separate locks. Builds refuse to start with less than 2 GiB free.

Cleanup is triggered by builds, not by a background scheduled task. Failed builds
with a newly written `FAILED` receipt can release their generated output while
preserving diagnostic logs. Cleanup errors never rewrite a successful native
receipt as failed, and are reported for attention.

Normal builders use the existing catalog under the shared lock, register their
new output and rotate only its exact device/profile/configuration group. Fresh
pin, receipt, candidate-inventory and retained-image checks still run before
deletion. Other groups and uncatalogued output are untouched. A missing or
invalid catalog falls back to discovery. Explicit `plan`/`apply` still discovers
the entire tree and rejects newly appeared builds; use that reconciliation after
historical builders have stopped. Catalogs are performance aids, not image or
hardware acceptance. Each builder records cleanup duration separately in
`storage-timing.json` without rewriting its immutable native receipt.

Optional W12 compiler caches are separate from retained image bundles. They
contain only disposable local compiler results, remain owner-only and ignored,
and use ccache's 512 MiB limit per host/native lane with explicit cleanup after
each successful run. They never replace the latest-two, installed or recovery
pins, and they do not retain whole compiler trees.

## Verification

```sh
python3 -B tools/test_firmware_artifacts.py
python3 -B tools/firmware_artifacts.py inspect artifacts/build-rc-01
```

The storage inspector verifies retained artifact/source hashes and application
identity from the immutable receipt, using frozen sources rather than today's
application checkout. It rejects retired builds. It is an identity check, not a
replacement for target-specific layout, runtime-binding, SDK or hardware checks.
Run the target's existing native inspector in its documented environment when
those checks are required. The early CoreSE UI format lacks a frozen source copy;
its storage check covers the recorded application identity only.

Applied cleanup journals, latest/previous storage pointers and the build index
live under ignored `artifacts/.firmware-retention/`. These pointers describe
retained builds, not installed devices. All generated reports and images remain
private; byte counts describe unlinked file allocation, and APFS snapshots or
shared extents can make filesystem free-space changes differ.
