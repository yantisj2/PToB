# W12 source cutover

Copyright (c) 2026 Jonathan Yantis. MIT.

On 16 October 2026 PToB became the home of normal W12 native G1/RC7 development,
following the T096 source cutover. The selected W12 source is isolated application
commit `48b9ab700d178e06f34c9d981d859a8c5c0d97cf`. No private Git history,
enrollment, installed images or raw laboratory logs are imported. The explicit
[source allowlist](../../firmware/w12/imports/2026-10-16.json) records source bytes
and public transformations. PToB successor versioning begins at **0.1.0**;
the full PToB commit and runtime build binding identify each candidate.

The extraction keeps current native G1/RC7 receive/permission/controller logic,
the display/GNSS compatibility include boundary and selected host fixtures. It
replaces hardcoded bench bindings and display names with ignored local device
inputs and the common registry. Only RC is exposed; BLE100 estimated relay is
explicitly experimental, BLE50 is client-only, and actual TX capture remains
unavailable. LoRa/WiFi/PTS/GNSS and donor output corrections are disabled.

Build inputs, tests and remaining installation limitations are documented in
[the build guide](build.md). Historical paper editions and experiment populations
remain unchanged. Runtime FOLLOW, LOCK or relative-fit statistics do not qualify
independent electrical output or absolute UTC accuracy. Checksums identify
bytes and do not establish accuracy.

Existing local legacy/laboratory discovery points carry moved notices that
point here. Historical source and private recovery records remain preserved.
This cutover does not publish a release or authorize broader hardware work.

The selected sanitizer-backed host runner passes 136 recorded compile/run
checks, including 24 startup-boundary and 117 parent-selection/history assertions,
receiver-reference refusal, current source-expiry/reset behavior, display aliases,
console bounds and the estimated relay model/client codec. A separate focused
permission runner passes 36 model cases and nine actual-native cases. These are
synthetic boundaries, not the full independent hardware suite.

Initial broad historical tests retain failed immediate-discovery/root-rotation
expectations that predate the current three-second startup collection contract;
they are outside the selected passing runner. The retained startup fixture tests
the actual wait, fresh-GM preference, owned scanning and finite fallback. A
capture fixture also needed an explicit synthetic1M receiver reference: missing
or unknown references still fail closed. All failed development runs remain
local and ignored. Early build failures included dependency/host setup, relocated
include paths, a compile-time legacy credential stub bound, and a post-build
check that confused an SDK hidden WiFi build symbol with runtime activation.
The final check examines the ELF for WiFi activation and NVS write dependencies.
No failed/development image is approved for installation.

## Attached-unit validation

One factory-bound C-2 received version 0.1.0 from PToB source commit
`bbcd843de5263394bda5511ea6e65b1ddbab0b73`, using the shared local registry and
BLE100 estimated-relay configuration. The reviewed 882,368-byte application has
SHA-256 `ab9258a434d6b57dd7b8ea62e2a968236ea2e395b93ea1c004f5f07aaa6b7b06`.
The enrolled image remains private; this hash identifies the tested bytes.

The guarded procedure verified the current runtime and factory, bounded original
update drain, actual preservation/recognition of the old application, application-only
write at `0x10000`, actual new application readback, and exact comparison of the
64 KiB bootloader/partition/NVS prefix. The prefix comparison was performed after
the write and before the first application boot; **postboot prefix preservation
was not observed**. No preservation waiver, mass erase, partition write or failed
installation retry was used. The supported stock watchdog reset returned the
application to a fresh source/build/boot-bound startup. Two startup frames were
healthy but had no FOLLOW state yet; they remain separate from the later run.

The separate normal-console observation lasted **90.59 seconds**, with **36/36
sampled FOLLOW states**, zero sampled faults, and API/quiet-fault counters remaining
zero. Matched receive count advanced 1,095→2,300 and authenticated GR count
1,111→2,333; GR refusals remained zero. Cumulative join attempts advanced 6→14
while accepts advanced 5→12. These different start/end populations are retained;
no complete-admission success rate is inferred. The observer sent status requests
only: no RF, capture, profile, reset or OS clock commands.

C-2 was left running. Owned raw writers and serial handles closed, and a fresh
read-only inventory plus both coordination-lock checks confirmed release. No
other unit was operated. Raw logs, actual identities, group enrollment, registry,
rollback and protected snapshots remain ignored local artifacts. The finite
sampled behavior does not establish continuous availability, long-term reliability,
physical radio duty, qualified relay TX, absolute UTC or independently measured
output accuracy. The full independent hardware suite and generic public install/
recovery acceptance remain pending.
