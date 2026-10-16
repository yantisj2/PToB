# T096 local source cutover — 16 October 2026

Copyright (c) 2026 Jonathan Yantis. MIT.

PToB is the source home for normal T096 RC7 development, beginning with the local
0.15.54 migration change. GM and RC are the only public profiles; USB source
streaming is standard. The prior application commit is recorded in
[baseline.json](../../firmware/t096/baseline.json), and the explicit extraction
allowlist, original hashes and transformations are in the
[import receipt](../../firmware/t096/imports/2026-10-16.json).

The former laboratory tree is the historical source reference. Future T096
changes belong here; the laboratory consumes identified PToB commits and returns
reviewed patches and separately curated evidence. Private keys, enrollment,
device inventories and raw captures remain outside public source. W12, SS3 and
macOS client ownership have not moved yet.

This source-authority change is separate from GitHub publication and a public
install-first release. P1 runtime provisioning, generic hardware install/recovery,
clean-host dependency acquisition and wider hardware qualification remain pending.
TC-02/TC-04 validation is a bounded migration check, not independent UTC metrology.
The later research drafts remain frozen for their separate publication review.


## Shared naming successor

Version 0.15.55 at source commit
`cfe75eb91486b76bff3e2603e4ceec64e4c44a57` adds the
[shared registry](../../registry/README.md). The public example covers all four
target families with synthetic IDs. Actual enrollment, the deployment registry
and enrolled binaries remain ignored local artifacts. T096 consumes the shared
format now; the other target adapters have not migrated yet.

The migration host checks passed for core protocol/clock/journal behavior,
USB SOF capture/worker/source paths, watchdog lifecycle, nine GM/RC lifecycle
checks, eight UTC/UI path cases, and USB EP0 recovery fixtures. Eight offline P1
storage-model tests also passed; they do not test installed storage firmware.
The registry test exercises the production display function, supplied-name
precedence and unknown-ID fallback. All eight locally configured aliases also
passed through that same production function with the generated deployment map.

Earlier imported test failures remain in local logs: obsolete radio fixtures,
private dependency assumptions and removed lab-name expectations were corrected
for the current public source. A TC-04 0.15.54 post-readback observation stopped
on a host ownership-scan timeout. Neither that incomplete observation nor later
success erases the failure. The image and 672 protected blocks had passed actual
readback before the host timeout.

Both 0.15.55 candidates pass portable layout inspection. Their kernel configuration
is byte-for-byte identical to the reviewed 0.15.53 configuration. Static
unallocated RAM remains 2,284 bytes; this is not measured stack headroom.

## Attached-unit validation

TC-04 and TC-02 received 0.15.55 RC-default images from the commit above, each
with the same eight-entry local display registry and USB source stream enabled.
Both passed fresh source/silicon/boot attestation, application-only installation,
actual `CURRENT.UF2` application readback, exact comparison of all 672 protected
blocks, and an identical-image return followed by RC startup. Old applications
and protected snapshots are retained privately for recovery. No preservation
waiver, mass erase or OS clock change was used.

| Unit | Enrolled UF2 SHA-256 |
| --- | --- |
| TC-04 | `093392396faf587fb9d2ab77bbdbf04d09333fcb29fe1142d70f91c98430f5a8` |
| TC-02 | `660305b1b1e7acec695b1b81c321ec6eeb42d92b14384ee5bab3e755ab227452` |

These enrolled binaries are not public downloads. Hashes identify the tested
images; they do not establish accuracy. TC-02's old inventory was stale: fresh
runtime attestation identified 0.15.49, and its prewrite bytes were checked against
that image. The older 0.15.44 lookup attempt timed out without writing; recovery
validation used the matching archived host closure, not weakened current guards.

TC-04's bounded 60-second service observation completed with 41/41 sampled
software-lock indications and an advancing accepted counter. A separate
20-second USB-only client observation exited successfully with 19 valid and two
unavailable source-status observations. The log retained its shutdown
“capture ended; retrying” event. NTP and chrony were disabled. This is a finite
functional observation, not continuous-availability or independently measured
UTC/output qualification; raw captures and enrollment remain private.

TC-02’s separate bounded 60-second service observation completed with 45/45
sampled software-lock indications and an advancing accepted counter. Both units
were left running with diagnostic captures closed. Field behavior outside these
finite observations remains to be evaluated.
