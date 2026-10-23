# N30 packet-window discovery protocol

Copyright (c) 2026 Jonathan Yantis. MIT.

Version0.2.17 adds a default-OFF packet and callback journal. It changes neither
the delivered G1 clock nor either screen's causal quality. The optional reviewed
RW238/237 GPIO4/5 profile is required; no arbitrary signal or pin is accepted.
The existing aggregate correction remains specific to the legacy backend.

The fresh ticket/factory/boot-bound console admits these commands:

```
window start 600000 8
window packet
window edges 0
window ack 1
window stop
```

Start accepts1–600seconds and1–8 planned windows. The original deadline is never
renewed. Select the first complete matched periodic report after each arm with
the original controller event candidate divisible by16. Selection does not use
residual, RSSI, channel, marker presence or pulse geometry. Failed windows count
toward the requested population. Reports queued before the latest arm are counted
separately. ACK permits the next window under the same counter incarnation.

The worker retains an eight-entry exact capture(epoch,sequence) anchor cache.
The main task copies one immutable full AD158/190 packet before diagnostic
logging, with CRC32/IEEE over all AD bytes. An existing corrected-clock Pair is
joined by exact capture epoch/tick/event/channel; missing or ambiguous joins are
explicit. These observer hooks do not replace the existing timing recorder.

A CURRENT80/GPTimer20/CURRENT80 bracket locates a broad candidate interval around
the original descriptor coordinate. Its fixed search radius is3100microseconds
plus the original descriptor enclosure. This is an association search, never an
RF error bound or a fine estimator phase map. All captured edge times use one
continuous MCPWM1 counter and fixed origin. LoRa's group0 owner is unchanged.

One fixed64-edge slot backfills the original32-callback history and retains
per-channel predecessor/successor fences. Callback sequence is export order,
which need not be raw-time order across channels. Missing prehistory, capacity,
counter faults, pending capture and delayed callbacks inside sealed intervals
remain explicit. At sealing, channels0/1 and their interrupts are quiesced;
CURRENT2 continues. ACK clears owned stale RAW and rearms channels0/1 with a fresh
coverage boundary. Eight sticky late-invalidation bits survive ACK and later slots.

Packet and edge responses each contain ten nonce/CRC-bound rows. Packet frames
carry STATUS/PACKET/STAMP/BRIDGE/PAIR/CONTRACT; edge frames carry STATUS/four EDGE
rows/CONTRACT. Both also contain the four existing compact identity/command/UI
rows. Edge reads are non-consuming. Export every retained edge before ACK, then
retain the final invalidation mask and stop/release result.

The N30 Stamp uses its existing four padding bytes for original PDU payload
length(octets), raw AdvMode/ExtHdrLen byte, and raw16-bit controller status.
They come from already-loaded validated descriptor locals, with no new hardware
read and no Stamp/Report growth. Extended flags remain unknown. Stamp PHY retains
its existing controller encoding and validity flags. AD length is not PDU length;
raw status is not relabelled as a new CRC guarantee.

The first eight windows are discovery evidence. Complete callback export cannot
prove that a physical edge did not coalesce in the single hardware latch. There
is no nearest-edge rule or selected polarity. Match independent TC04 raw ADDRESS
audit records only by exact upstream packet tuples and complete-AD CRC; local
counter epochs differ. Drain its small ring concurrently and retain all gaps.
Endpoint validation, an independently evaluated fine estimator and production
handoff require subsequent evidence; this protocol grants no RF/UTC qualification.

Offline checks:

```
python3 firmware/n30/tools/window_tests.py --out artifacts/window-tests
python3 firmware/n30/tests/status_budget_test.py
```

Version0.2.18 raises the original cohort limit to128 while retaining one64-edge
slot and one continuous counter. All128 sealed intervals retain late-invalidation
fences through stop and ACK. `late_words` carries four unsigned32-bit words
(IDs1..32,33..64,65..96,97..128); `late_count` counts distinct invalidated IDs,
and `late_callbacks` counts matching callback invalidations. `late_mask` remains
the first word and `late` retains the first eight IDs for compatibility.

`window late <after_id>` returns four nonconsuming `N30_WINDOW_LATE0..3` rows
under the `WINDOW_LATE` CRC envelope. Each present row contains its window ID
and immutable first invalidating callback sequence. IDs are ascending, and
all physical coalescence remains unknown. Future callbacks beyond the greatest
sealed raw80 upper bound skip the retained-fence scan. This is still an observer;
the delivered model, legacy offset and UI quality source are unchanged.
