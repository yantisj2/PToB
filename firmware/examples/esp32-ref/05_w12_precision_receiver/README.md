# 05 · W12 precision receiver

Planned ESP32-REF lesson for the Meshnology W12. The goal is to deliver a
passive BLE clock with sub-microsecond phase error against independent PPS.
The lesson will have its own app and board adapter. [Example 02](../02_minimal_receiver/README.md)
teaches the copyable descriptor receiver and provides the comparison path.

## Capture the radio edge

Route the ESP32-S3's `RW_RX_ON` signal through W12 GPIO16 to MCPWM group 1
channel 0. Capture both edges with the continuous 80 MHz counter and read
current time from the same counter. The receive descriptor identifies the
packet, event and channel and locates the search interval. A unique rising and
falling pair supplies the fine phase. Require the reviewed BLE 1M `PDU191`
format with no extended header. Count missing, late or ambiguous associations
and refuse them. Confirm the board revision and GPIO16 ownership before use.
[Native W12 route and capture rules](../../../../docs/w12/build.md#fine-clock-test-profile).

The native W12 uses falling edge minus 1568 µs as a packet-specific phase
convention and rounds its 12.5 ns captures into a 50 ns clock coordinate.
Characterize the remaining offset from the T096's hardware ADDRESS timestamp
on the actual board and path. Keep the measured correction an explicit input
with its own provenance. A board, packet class or RF path change needs fresh
evidence. [Low-level timing paper](../../../../papers/ptob-esp32s3-low-level-timing.pdf).

## Let the servo settle

Keep the BLE observations, published clock and PPS witness distinct. Reuse the
reference collection's clock arithmetic and passive two-step matching. Compare
descriptor phase and RX-active phase on the same packets, with the same servo
settings. Then compare the short fit with a longer causal history. The native
fine-clock fit retains 64 points, which spans about four seconds at the nominal
16 Hz cadence. Score predictions before applying the observation that tests them.

For the first bench run, record acquisition from startup and give the servo
120 seconds of continuously valid training on one authenticated timing scope
before scoring. Restart the warmup after loss of clock validity or a source,
train or capture change. Set an acquisition deadline in advance and record
time to first UTC, time to qualify and any failure to settle by that deadline.
Source permission still expires on its original deadline.

## Measure accuracy and coverage

1. Begin with a strong, stable link to a serving T096. Verify the GPIO16 marker
   association and collect a calibration run against independent PPS. Freeze
   the correction before opening the validation window.
2. Start at the first PPS opportunity after warmup and record 600 consecutive
   opportunities with the frozen correction. The witness observes the clock
   outside the BLE fit. Record witness and electrical uncertainty. Keep every
   missing sample in the denominator and every error tail in the result.
3. Require at least 99% valid coverage and an absolute P99 below 1 µs. Report
   signed bias, RMS, P99, maximum and longest outage. Repeat with representative
   app load. Record cold-start, weak-link and reacquisition runs separately.

The October 24 native W12 baseline used a 120-second warmup and a correction
frozen in advance. Its stable window had 300 of 300 eligible PPS observations,
38.312 ns RMS and 97 ns absolute P99. Across both windows it retained 522 of
600, including all unavailable records in the denominator. The experiment
measured relative phase on its specific setup. The new example needs its own
independent PPS result and uncertainty budget. [October 24 W12 evidence](../../../../evidence/s3/2026-10-24-w12-utc-basis/README.md).

Copyright (c) 2026 Jonathan Yantis. [MIT](../LICENSE).
