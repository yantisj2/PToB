# W12 radio and capture ownership

Copyright (c) 2026 Jonathan Yantis. MIT.

The NimBLE host event loop owns BLE HCI commands, G1 admission and relay state.
The LoRa worker runs on core 1 with priority 1 and owns SPI/DMA, passive packet
decoding, replay floors and the LoRa fit. Main/UI code requests policy changes
and reads immutable snapshots. It does not run SPI, HMAC or fitting while holding
the cross-core publication lock.

## Cancellation and physical capture

A LoRa revocation advances a cancellation serial once and retains its original
200 ms drain start. Repeated OFF observations cannot postpone that deadline.
The worker must retire the previous capture, protocol model and DMA ownership
before a later ON can start. A timed-out or uncertain DMA cannot acknowledge OFF
or be reused. The main update gate retains its independent original two seconds.

Accepted mode changes are command intent, not source leases. The latest explicit
mode remains queued until the worker consumes it in the matching cancellation
serial; revocation discards earlier intent. Clock observations and authenticated
leases still expire at their original deadlines.

BLE uses MCPWM group 1 channel 0. LoRa borrows channel 1 with separate counter
extension and an explicit capture-incarnation cookie. Channel 2 supplies bounded
CURRENT latches. GNSS uses group 0. A callback released from the fine-clock lock
retains the original cookie; the LoRa owner rejects delivery from an earlier
capture even if a later capture is already active. LoRa start/stop leaves BLE's
capture channel and association history intact.

The radio output pads retain their input path, including after RadioLib changes
direction. ESP-IDF returns zero for an output-only pad, so that value cannot
prove RESET is asserted. OFF acknowledgement requires the readable RESET pad
to be low; a high pad retains the original drain timeout and cannot acknowledge.

## BLE admission ownership

Discovery scanning remains stopped while an outgoing initiation, established
ACL, incoming ACL or quarantined ACL owns the controller slot. That ownership
includes a pending disconnect and its original drain. The host worker stops an
existing scanner before admission control; GAP callbacks only record owners.
The central scan-start gate applies to every caller. Upstream periodic reception
keeps its own authenticated scope and is not stopped by this scheduling rule.

Scan cancellation can yield to a delayed incoming CONNECT. Before installing an
outgoing peer, the worker rechecks the full incoming/quarantine owner predicate,
shutdown state and admission policy. Deferral does not consume a new incarnation
or silently retire the incoming owner. Only actual closure releases the slot;
exchange, attempt, drain and listening-window deadlines retain their original
bounds.

## Relay capability recovery

The paused connectable capability advertisement uses handle 1. The periodic
relay train uses handle 0. Capacity recovery may recreate only stopped handle 1,
once within the original 500 ms attempt budget, after checking ACL and teardown
ownership. Removal and later reconfiguration are separate host turns. A complete
new controller owner resets retry history; a reconfiguration within the same
attempt does not reset its deadline. Failed commands never count as publication.
Incoming quarantine blocks CAP data/configure/remove/start just like an admitted
ACL. Recheck ownership after each yielding SDK boundary, including data/configure
and immediately before start. An intervening ACL does not restart the attempt
budget. Other errors and exhausted recovery keep the existing fatal path and evidence.
The pause checks both the local flag and NimBLE's public active-instance state.
A delayed completion cannot make a later SDK-owned CAP safe to remove merely
by clearing the local flag. Busy removal is never an OFF acknowledgement.

## Verification limits

`firmware/w12/tools/lora_tests.py`, `fine_tests.py`, `rc7_permission_e2e.py` and
`dual_display_tests.py` exercise actual owner code at synthetic SDK boundaries.
They cover delayed commands, cancellation handoffs, old callbacks, wrap, DMA
failure, source expiry, held MTU/service responses, inline incoming CONNECT,
quarantined CAP recovery and periodic-train preservation. ASan/UBSan and these
interleavings do not prove absence of all races. Native size/stack checks and
exact-device runtime verification remain required. No checksum or simulator
qualifies independently measured RF/UTC accuracy.
