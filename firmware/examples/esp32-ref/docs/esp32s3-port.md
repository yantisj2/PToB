# ESP32-S3 timestamp adapter

The generic controller accepts `(local capture, reference time, availability,
channel, saved prior prediction)`. This file explains the isolated adapter that
produces those inputs. It is an experimental implementation for the exact
public ESP-IDF/controller pin, not a portable BLE timestamp API.

The implementation is newly written from documented research methods and ABI
facts. It includes no W12/N30 operational sources. Study the
[PToB ESP32-S3 architecture](https://ptob.dev/hardware/esp32-s3/timing/), the
[low-level implementation paper](../../../../papers/ptob-esp32s3-low-level-timing.pdf)
and the pinned public Espressif SDK alongside `ports/esp32s3/radio.c`.

## Clock domains

The default GPTimer runs continuously at 20 MHz from XTAL with 50 ns ticks.
The optional `mcpwm80` port uses a continuous 80 MHz APB capture timer with
12.5 ns ticks. Its GPIO-free CURRENT latch and bounded wrap extension live in
`counter.c`. It keeps the finer coordinate through the fit and clock actuator.
The port checks the driver ABI and hardware rate. Controller RX descriptor
timestamps have a 500 ns grid in either profile. Resolution is not an
accuracy bound. Coarse `esp_timer` microseconds check counter progress and gross
rate only; they never steer or replace its phase coordinate.

Bracket a ROM Bluetooth-clock read between local latches. Retain the bracket
and refuse widths above 20 us. Map the descriptor's 28-bit half-slot
clock and descending half-microsecond fine field into the local coordinate.
Normalize the pinned LE1M sync-position excess relative to nominal ADDRESS.
No empirical W12 receive offset is imported. Actual detector/path bias and the
mapping aperture remain unknown physical uncertainty.

For transmission, retain the controller's selected immutable payload and its
event metadata. Convert its **programmed** half-slot start into local time and
add nominal LE1M preamble/access-address duration, 40 us. This is a programmed
ADDRESS estimate, not a measured RF latch. HMAC and checksums do not change that.

## Beyond the 500 ns receive grid

A faster local timer alone leaves the controller descriptor's 500 ns receive
grid in place. The experimental W12 fine-clock profile gets its phase from a
different measurement. It routes the radio's RX-active signal through GPIO16
to an 80 MHz MCPWM capture channel and keeps the descriptor for packet identity
and for locating the edge. An accepted fine timestamp needs one unambiguous
edge pair for the exact supported packet and PHY. The raw edge has 12.5 ns
ticks, which the W12 rounds into its existing 50 ns clock coordinate. See the
[W12 fine-clock profile](../../../../docs/w12/build.md#fine-clock-test-profile).

The phase convention from the falling edge is empirical and specific to the
packet class. The marker's offset from the on-air address event and the path
delay still need independent characterization. Other techniques in the same
research include using one continuously running counter for event and current
time, measuring the width of cross-clock reads, and scoring each new sample
against a prediction made before it updates the clock fit.
[Low-Level Timing on ESP32-S3](../../../../papers/ptob-esp32s3-low-level-timing.pdf).

The companion results paper compares the fine marker with descriptor timing
on held-out packets and reports missing observations and the tails of the
relative pulse-per-second (PPS) measurements.
[Precision Bluetooth Timing on ESP32-S3](../../../../papers/ptob-esp32s3-timing-preprint.pdf).

## Narrow ABI boundary

All raw layout accesses are in `radio.c`:

| Fact | Pinned interpretation |
| --- | --- |
| Dispatch byte offsets 0x150 / 0x160 / 0x2a8 / 0x2f0 | Periodic RX ISR / AUX packet / descriptor free / SDK periodic TX handler |
| `lld_sync_env` / `lld_per_adv_env` / sync-position table | Runtime addresses checked against the pin |
| RX descriptor | Complete stable AUX_SYNC_IND, valid status, payload and event/channel |
| TX environment offsets 4 / 76 / 78 / 80 / 82 / 83 | Schedule cookie / event / data offset / length / activity / state |
| Control structure | Matching event, format, first descriptor, access address and channel map |

The builder checks the SDK revision and controller archive hash. Runtime checks
require the expected unmodified dispatch targets, core 0 and 240 MHz. Hooks call
the original SDK handler exactly once with unchanged arguments. Payload and
relevant metadata are re-read for stability. Radio, EM, descriptor, scheduler
and payload registers are never written by the observer. Installation/restoration
changes only the four guarded dispatch slots while their lifecycle is owned.

The baseline accepts a plain single PDU, without chains or channel-map ACAD.
Unsupported packet forms are refused and remain missing timing opportunities.
RX requires one matching candidate and exactly one freed packet in the ISR.
The 16-entry queue, finite time windows, generation fences and IRQ cycle ceiling
contain observation failures. The original controller still runs on refusal.

IRQ code and arithmetic live in IRAM, its retained data in internal RAM. Timing
availability withdraws on counter, association, radio or owner failure. Stop first
receives SDK controller acknowledgments, stops the host, restores the dispatch,
then releases timer/output/controller resources. A separate task lets the UI
enforce its original two-second deadline even if an SDK call blocks.

## Limits

No source-proportional RF calibration, independently measured output accuracy,
measured energy savings or current-image hardware acceptance is claimed. A new
SDK or SoC needs a new reviewed adapter, guards and native checks. Other ESP32
families cannot inherit this S3 ROM layout. The optional MCPWM output has an
unmeasured initial software synchronization aperture and requires its own test.

Copyright (c) 2026 Jonathan Yantis. [MIT](../LICENSE).
