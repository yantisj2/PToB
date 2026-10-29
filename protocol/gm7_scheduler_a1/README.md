# GM7 scheduler amendment A1 — ACTIVE discovery diversity

Copyright (c) 2026 Jonathan Yantis. MIT.

**Reviewed offline engineering amendment; no flash or hardware acceptance.**
A1 supplements implementation2's secondary scheduling policy without changing
any wire layout, authentication or authority rule. Frozen draft1, implementation2
and the firmware-clearance report remain unchanged. The manifest binds the exact
implementation2 manifest; this amendment is within functional offline A scope.

## Common schedule

Use absolute Unix UTC microseconds to compute `frame = floor(UTC_us/250000)`.
Do not use boot uptime, mesh birth, local event counters or time since discovery.
A secondary opportunity is reserved when `frame % 4 == 0`. Its common bit index
is `(frame // 4) % 64`, counting identity bits from least significant bit0.
Full device identities are unsigned nonzero64-bit values, never shortened aliases.

Keep primary control nominal38ms and secondary nominal43ms within each frame,
with implementation2's complete-job2ms, lateness2ms and guarded RX limits.
At a reserved secondary opportunity an eligible ACTIVE whose identity bit is1
transmits a byte-identical copy of its ordinary primary ANNOUNCE from that frame.
Bit0 listens throughout the control RX window. All ACTIVE nodes listen whenever
not transmitting; other nodes retain required control listening. Primary delivery
is not a prerequisite to receiving its secondary copy. Duplicates do not renew
sequence, original timestamp, source permission, expiry or nomination.

Suppress positive discovery for the entire pending YIELD event, after WITHDRAW,
or whenever local serving/source permission is invalid. A reserved opportunity
then remains listening, not spare ANCHOR/diagnostic capacity. Never duplicate a
YIELD into this slot. Its original primary copies remain at0/250/500ms and its
original handoff boundary remains750ms. WITHDRAW repeats likewise remain exactly
the three primary copies, with immediate positive-output cancellation. No late
job is moved into a later frame. The policy helper's ordinary-secondary result
only identifies reservation class; it never overrides source/withdrawal fences.

For two distinct full identities, within64 successive reserved opportunities
there is a differing bit: one transmits while the other listens. The worst-case
schedule opportunity is64seconds, not an RF-success or global merge deadline.
Multiple transmitters can collide; losses and adversarial RF remain possible.
Duplicate identities remain an identity-ambiguity fault, not a tie to solve here.

## Response admission and retry

Reserved secondary windows are unavailable to ANCHOR and diagnostics. Admit a
request response only to the next frame's nonreserved secondary opportunity,
within the original250ms RTT/deadline and normal bounded queue. Never delay a
reply by a frame to accommodate this reservation. Primary control retains priority.

After a failed request, wait at least2s from its actual attempted transmission.
Rotate the retry frame residue through0,1,2,3 using a local nonwrapping attempt
counter; choose the earliest frame of that residue at or after the2s threshold.
The executable example uses offset90ms. In production select a valid rotating
STATUS slot plus bounded jitter first, then choose the earliest eligible frame
using that actual offset; preserve the same residue and min2s rule. Inter-retry
wait remains less than3s. Three of every four residue attempts can obtain the
next secondary window, assuming next-window admission capacity. Requests use
fresh nonce/sequence; retries never revive an old request. Keep bounded fair
round-robin pending-peer admission and per-peer counters so a busy peer cannot
monopolize responses. This is bounded scheduling fairness, not guaranteed radio
or queue service under overload. Overflow fails closed; it must not reset a
counter in a manner that locks every retry onto a blocked residue. The ordinary
STATUS cadence remains about2s; additional bootstrap retry is separately bounded.

## Assumptions and fallback

The pairwise64second argument requires the same absolute frame/bit index, phase
uncertainty within the receive guard, overlapping primary-channel coverage and
supported complete-event airtime. Functional UTC alone does not establish that
precision enclosure. Derive/check the phase from physical timing evidence;
independently started meshes can have uncertain relative phase. A UTC step or
loss of trusted indexing cancels aligned pending work without sliding deadlines.

When those assumptions are unavailable, require broad passive discovery: listen
across feasible primary advertising channels and AUX follow events throughout
otherwise free radio time, rather than restricting discovery to28..58ms. Required
local timing/control transmissions retain priority; diagnostics are deferred.
Rotate scan channels with bounded dwell and record gaps/preemption. This fallback
may run concurrently with valid local serving; it grants no remote permission.
Do not claim a deterministic discovery bound for unsynchronized meshes from this
model. Hardware integration must demonstrate actual channel/phase coverage and
account for single-radio blind intervals. Broad discovery cannot substitute for
required aligned HOT control RX, nor justify extending handoff/blackout deadlines.

## Verification

Run `python3 -B protocol/gm7_scheduler_a1/verify.py` from repository root.
Four offline tests cover distinct IDs (including identical low bits and adversarial
high-bit differences), arbitrary bit-cycle starts, invalid IDs, event suppression,
retry residue/admission fairness and unchanged primary deadlines. The manifest
verifier is read-only and rejects changed or extra artifacts. These tests model
schedule opportunities only; no firmware build, radio or hardware was operated.
