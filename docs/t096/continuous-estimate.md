# Continuous local USB estimate

Copyright (c) 2026 Jonathan Yantis. MIT.

T096 0.15.68 adds an explicitly requested local projection beside the strict
UTS1 timing observations. An acquired map can advance during missed corrections,
control refusal, parent search and original source/session expiry. This is an
unqualified estimate, not a new observation or an extension of root authority.
Legacy USB, BLE relay and downstream admission retain their existing gates.

## UTE1 v1

Vendor IN request `0x57`, type `0xc1`, value zero, selected timing interface,
length 256 returns `UTE1`, schema 1. `0x56` remains the 32-byte UEP1 diagnostic.
An upgraded helper probes once; unsupported firmware falls back to `0x53` on
the next sample. Successful negotiation replaces the existing high-rate SOF
request, with no second high-rate poll. Old UTS1 decoders reject this magic.

Offsets 8–32 carry silicon, MCU boot, USB/capture epochs and sequence as UTS1.
At offset 36 the independent bits are: 1 strict captured observation, 2 local
estimate present, 4 coherent counter observation, 8 original session current,
16 original root envelope current. Bits 1 and 2 have different meanings; a
held estimate is never reconstructed as a valid UTS1 observation. A missing
coherent counter observation is unknown, not proof of counter failure.

The SOF tick/current tick/estimate UTC are at 48/56/64. The kind is 1 at 72.
Offsets 80–228 preserve UTS1 source, immutable map, original strict interval,
path and original envelope metadata. Projection beyond that strict interval
is authorized only as a local estimate. The tail adds last physical discipline
tick (232, u64), original session deadline (240, u64), tracking state (248,
u32: 0 absent, 1 under 250 ms, 2 held), and estimate reason (252, u32: 0 present,
2 USB observation unavailable, 3 no applicable projection). All integers are
little endian. Bounds equal to UINT32_MAX remain unknown; the host currently
labels all continuous estimates UNQUALIFIED regardless of numeric metadata.

A retained old map keeps its old source scope. A new map cannot apply before
its publication interval; counter/capture discontinuity and arithmetic failure
fence projection. Polls and control traffic do not reset discipline age. A
source change creates a provenance segment; it cannot transfer phase authority.

From 0.15.71, preparing a same-scope discipline leaves the previously published
map usable until its own original deadlines or replacement at publication.
UTE1 may describe that older map when it covers the captured SOF, including its
original discipline tick and original source/session deadlines. It does not
substitute newer metadata or backdate the new map. Incompatible capture, root,
boot, path or UTC-offset changes retain the preparation fence. A concurrent
publication or withdrawal still invalidates an unfinished response.

## Host lifecycle

The client retains the selected transport and counter/SOF fit during degraded
quality. Causal SOF/Mach mapping anchors the local presentation when available;
the startup fallback uses an explicitly unqualified request bracket. Owner queue
arrival does not set phase. Compatible updates slew at no more than 500 ppm.
Larger phase changes require three coherent distinct maps spanning at least
125 ms before slow recovery; these are engineering gates, not accuracy bounds.
The display exposes HOLDOVER, LOCAL ONLY, age and unknown uncertainty. Source
changes and disputed transitions remain visible.

The existing owned chrony process may free-run during same-scope quality/expiry
loss; measurement feeds pause. The estimate path never sends extrapolations as
refclock observations. Real source/timebase changes retain their fences. This
change does not start a daemon or configure network/OS time services itself.

## Recovery and limits

Authenticated ordinary zero-source JOIN replies are temporary NO_SOURCE,
separate from a positive follower and inconsistent boot/scope. Warm control
retries retain fit and PA receive synchronization, use 20–40 second jitter,
and allow at most two starts per rolling minute. Successful legacy renewal
remains 300 seconds within its original 360-second context. The proposed
900/600-second negotiated contract and quality-bearing BLE relay extension
require a coordinated GM/peer upgrade and are not active here.

A passive 152-byte capsule freezes at the first post-acquisition discipline loss per capture
lifecycle, including prepare lazy/queued cancellation and available native
outcomes/owner/generation. Later losses remain in cumulative counters. READY,
HCTO, failed-event RSSI and a ticker collision winner are unavailable; no timer
or PPI ownership is added. Continuous estimates do not prove improved RF
reception, calibrated UTC accuracy or mixed-chain continuity.

Run `usb_sof_tests.py`, `rc_gm_admission_tests.py`, `rc_durability_tests.py` and the
full firmware production-body suite, then the macOS helper/client self-tests.
Hardware acceptance must bind the clean source, image, exact device and complete
capture populations separately from these offline checks.
