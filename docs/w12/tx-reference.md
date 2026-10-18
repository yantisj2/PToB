# W12 relay timestamp reference

Copyright (c) 2026 Jonathan Yantis. MIT.

RC7 receiver discipline associates each correction with its original packet's
ADDRESS endpoint. T096 connects RADIO ADDRESS to TIMER3 CC1 using PPI and keeps
that capture unchanged. Its hardware TX and RX detector equivalence remains
uncalibrated. W12's estimated publisher previously projected its programmed
half-slot start directly into that correction, without converting endpoints.

For LE 1M, the preamble has 8 bits and the access address has 32 bits. Their
nominal duration is `(8+32)/1 Mbit/s = 40 microseconds`, or 800 local 20 MHz
ticks. These packet fields are defined by the [Bluetooth Link Layer
specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/low-energy-controller/link-layer-specification.html).
Nordic defines [RADIO ADDRESS](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/radio.html)
as the address sent/received event. An original packet-start timestamp used at
this later receiver endpoint makes the receiver's mapped clock about 40
microseconds early, with the same negative sign in a relative PPS shadow.

The pinned S3 ROM event-start path at `0x40021ab0` copies the environment's
half-slot cookie at +4 into the scheduler request and sets its fine half-us
field to zero. The existing ROM fingerprint guards that handoff. The TX helper
maps this cookie through its Bluetooth/local-counter bracket; it is a
programmed schedule, not an RF latch. W12 receive normalization separately
removes the pinned 1M sync-position surplus above 40 microseconds, leaving its
nominal ADDRESS endpoint. No 40-microsecond correction belongs in that receive
path or in T096's raw ADDRESS capture.

The shared W12 publisher now projects both the raw programmed start and the
nominal ADDRESS tick through the same frozen publication model. It emits the
ADDRESS projection when no provisional profile applies. The conversion is
limited to the explicitly configured 1M publisher; unsupported PHYs, future or
overflowing endpoint ticks and invalid projections are refused. The existing
original source envelope and 500-millisecond inherited-age limit still apply.
Both endpoints and final output must remain inside the original source
envelope. Parent/source/authentication/lease, train, frozen-payload and
precision gates retain their independent roles.

A schema-1 provisional aggregate profile was defined as a **total** relative to
the old raw programmed start. Preserve this meaning: `emitted = raw + total`.
The nominal ADDRESS component is part of that total. Diagnostics decompose it
as `base = ADDRESS projection - raw projection`, `residual = total - base`.
Without an applicable profile, residual is zero. A legacy +47,000 ns profile
therefore retains exactly its prior emitted timestamp with a residual near
+7,000 ns, rather than becoming +87,000 ns. Clock slope and phase slew can
change the projected base by a few nanoseconds; use the actual projected
difference. Existing profile provenance/scope is unchanged. This compatibility
does not establish that an aggregate calibration transfers to another device,
root boot, receiver or evidence population.

`W12_G1_TX_EST` keeps the raw `tick20` with `PROGRAMMED_HALF_SLOT` provenance.
Its version-2 `W12_G1_TX_PATH` companion retains that raw tick/projection and
emitted timestamp, plus `address20`, `address_ns`, `base_ns`, `residual_ns` and
`applied_ns`. The configured `path_offset_ns` is the applicable legacy total;
zero means no provisional profile. Match complete publisher/root/boot/epoch,
train, publication, event and channel tuples. Missing or dropped records remain
missing. Earlier version-1 records have the earlier semantics.

The nominal conversion repairs a source reference mismatch. It supplies no
measurement of schedule-to-preamble delay, TX/RX front-end bias or independent
UTC error. Those remain unknown, including any residual after the conversion.
A constant reference repair cannot fix missing corrections or reduce residual
jitter. Per-device profiles, GNSS indicators and raw receive/clock fitting are
unchanged. BLE50 has no relay publisher; all BLE100 W12 identities use the same
conversion regardless of display alias or GNSS indicator configuration.

`rc7_tx_reference_test.cpp` executes publisher encoding, original/correction
matching and the production receiver map at synthetic ADDRESS/PPS ticks. It
covers four identities, positive/negative/default legacy profiles, RC2
forwarding, clock slope/slew, source/age/PHY refusal and counter arithmetic.
Host and native permission checks are synthetic; a candidate still needs its
own bounded hardware observation. A paired direct/relay shadow comparison with
matched source/boot/receiver context can measure remaining relative bias,
while independent RF/output metrology is needed for absolute precision.
