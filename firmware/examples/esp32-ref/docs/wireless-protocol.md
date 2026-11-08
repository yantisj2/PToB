# Example 01 wire and session contract

This is a separate teaching protocol. It does not join operational PToB G1/RC7.
The UUID identifies the example family, not a runtime-selected mesh or leader.
All integers are little-endian. PHY is LE 1M, with one unchained 190-byte Service
Data AD record per timing publication. Periodic interval is 62.5 ms (16 Hz).

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 2 | AD length 189 and Service Data type 0x21 |
| 2 | 16 | Example service UUID, exact bytes in `ref_wire.c` |
| 18 | 4 | ASCII `REF1` |
| 22 | 4 | Version 1, kind (beacon=1/challenge=2), reserved zero, correction count 0..2 |
| 26 | 8 | Sender device ID |
| 34 | 8 | Random sender boot/session |
| 42 | 4 | Nonzero increasing publication |
| 46 | 8 | Admission reply recipient, or zero |
| 54 | 8 | Challenge/echo nonce, or zero |
| 62 | 32 | Two correction slots, unused slots zero |
| 94 | 80 | Reserved zeros |
| 174 | 16 | HMAC-SHA256 over bytes 0..173, truncated to 16 bytes |

Each 16-byte correction contains publication (4), periodic event counter (2),
RF channel (1), TX provenance (1; currently programmed estimate=1), and relative
time in signed nonnegative nanoseconds (8). It refers to an earlier publication
in the same signed sender/session. The receiver matches publication **and**
event counter **and** channel. A repeated payload can have a different radio
event; it cannot substitute for the event named by a correction.

Missing, duplicate, stale, unsupported and ambiguous observations never become
zero-error measurements. Pending originals and consumed tombstones occupy 16
fixed slots with a 250 ms association horizon. Capture-generation changes clear
the join and PLL. Follow-ups arrive later; their own arrival is not the timed event.

A follower discovers an eligible local advertiser by service/SID, establishes
one periodic sync, verifies HMAC, selects its source session and generates a
fresh random challenge. Its separate example advertisement carries that nonce.
The leader scans authenticated requests and rotates echoes in its common timing
train. The follower grants six seconds of local admission after a matching echo.
Each nonce can renew that admission once; repeated echoes cannot extend it.
New challenges occur every two seconds. A source/boot/capture-context change
withdraws the old model and requires fresh admission. Up to eight requesters
have fixed reply slots in the leader's baseline implementation.

Admission freshness and clock freshness are different. A new physical original
with a valid matching correction may renew the clock's 500 ms original-based
deadline. Replayed publications, admission echoes and polling cannot renew it.
The example has one leader, no relay leases, no UTC translation and no automatic
leader election. Group HMAC proves group-key possession; it is neither encryption
nor independent proof of an individual member's identity.

Only the NimBLE host task owns radio APIs and session/PLL state. IRQ hooks retain
bounded read-only event records; they do no crypto, allocation or formatting.
Busy data updates retry the same pending publication without restarting a train.
Queue loss is counted. A radio reset or adapter fault withdraws usable timing.

Copyright (c) 2026 Jonathan Yantis. [MIT](../LICENSE).
