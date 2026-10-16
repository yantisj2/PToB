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

Attached-unit validation results will be recorded after the exact reviewed
candidate is built and the guarded C-2-only procedure completes. No broader
hardware scope or public release follows from this cutover.

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
