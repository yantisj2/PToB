# Pi gateway dashboard product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Confirmed: lightweight Python service with plain HTML, CSS and JavaScript,
served by the Raspberry Pi 5. No frontend build runtime is required on the Pi.

## Users

The gateway developer checks receiver acquisition, clock discipline and timing
service health remotely, including from a phone, before an Ethernet bench test.

## Product Purpose

Show whether time is moving from the HAT nRF52840 through PPS into chrony and
onward through NTP and PTP. Make missing evidence visible and make it possible
to distinguish a functioning gateway from services merely running.

## Operating Context

Regular Raspberry Pi 5 with Particle M-HAT and B504e. GPIO16 receives PPS via
module D2/RTS. NTP supplies coarse UTC. Wi-Fi is used for current service tests;
Ethernet hardware timestamp testing comes later. Desired host: pi.ptob.dev.
The user explicitly chose Tailscale-only remote access.

## Capabilities and Constraints

Live status must come from the installed device and carry observation age.
Receiver admission, PPS edges, selected chrony source, NTP replies and PTP
transport are separate observations. Unknown accuracy must remain unknown.
The nRF runs an experimental authenticated periodic-advertising leaf;
it exports admitted timing and hardware-scheduled PPS when its clock offer is valid. Wi-Fi PTP uses
software timing and an application-specific timescale. Ethernet PTP is prepared
but its cable test has not run. The dashboard must not start firmware updates,
reset radios or change clocks merely by being opened.

## Brand Commitments

PToB. Clear technical English, explicit units and candid evidence limits.

## Evidence on Hand

Private device receipts, chrony output, PPS counters, verified nRF startup and
bounded Wi-Fi NTP/PTP wire tests exist locally. They contain device identifiers
and raw lab observations and must not be copied into tracked UI fixtures.
No independent UTC or sub-microsecond output qualification exists for this port.

## Product Principles

- Lead with the actual timing path and the first unavailable stage.
- Separate liveness, selection and measured accuracy.
- Preserve unavailable and stale data rather than filling it with plausible values.
- Keep diagnostics readable and exports useful without exposing credentials.

## Timing Investigation

One-second recorded snapshots compare chrony offsets/RMS/source scatter,
receiver fit RMS/drift and integer-nanosecond GPIO PPS observations. The operator
can inspect samples, switch windows and export all snapshots. Missing data,
context changes and population counts remain explicit. History queries are read
only; live collection does not depend on opening the page.
