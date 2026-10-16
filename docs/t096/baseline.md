# T096 migration source selection

Status: selected development input, **not a public firmware release**.
Copyright (c) 2026 Jonathan Yantis. MIT.

The selected source is `1a16110a662ae66109b21bafa9958baf48d0465a`, from the
T096 R2 application. [The selection manifest](../../firmware/t096/baseline.json)
records the application tree and hashes of the inspected source inputs.
Application source is now included. No enrolled binary or private enrollment is
distributed here. See [build instructions](build.md).

## Why this checkpoint

The historical exact review binds this commit to the 0.15.53 USB relay build.
Its records describe application readback, protected-region preservation,
unattended return and bounded USB smoke observations. It includes the preceding
USB EP0 recovery and UTC-translation corrections. These records support selecting
an input for migration; they do not qualify arbitrary T096 hardware, public
provisioning, continuous availability or independent UTC accuracy.

The 0.15.48 cohort candidate was rejected after regressions, including a USB
startup failure and rollback. It is not the fallback public baseline. The
October 12 papers' 0.14.43 source remains a distinct research anchor.

## Current profiles

RC7 is the common application. It has exactly two user-selectable profiles:
**GM** and **RC**. USB source streaming is a standard part of both builds and is
not an optional third profile. The source retains runtime switching with the
existing ownership/drain rules. Build-time enrollment selects the cold-boot role.

The source reference used version 0.15.53 for its USB build; the public cutover
application is 0.15.54. Earlier version labels and diagnostic compile branches
are historical implementation details, not supported public profiles. The public
builder always applies `rc_dynamic.conf`, `rc_unicast.conf`, `watchdog.conf` and
`rc_hf_synth.conf`, and enables the USB source/SOF/reference interface together.

New public provisioning will change code and configuration. Its builds need new
identities and validation; historical images and results cannot certify them.

## Outstanding constraints

- GM source-validity loss and multi-GM HOT behavior remain unresolved in the
  reviewed records. Full independent qualification remains open.
- The USB review reported 2,284 static unallocated RAM bytes. This is not measured
  stack headroom. Provisioning must avoid adding a permanent large buffer/thread;
  a new link map and stack/resource review are mandatory.
- The build takes an explicit local device ID, label and battery configuration.
  It refuses other device IDs; hardware compatibility and update/recovery still
  need per-board verification. Alias changes do not establish board capabilities.
- The public builder uses pinned public dependencies and local build-time
  enrollment. Mutable P1 provisioning remains a separate implementation task.
- Some inspected files carry Apache-2.0 notices. Preserve them while establishing
  file-level provenance; the repository's MIT default does not erase inherited
  terms. Resolve rights before importing the application for publication.

See the [public provisioning design](provisioning-design.md), [PSK guide](../security/psk.md)
and [identity/name guide](../security/identity-and-names.md). The offline storage
model is a design aid, not firmware or hardware acceptance.
