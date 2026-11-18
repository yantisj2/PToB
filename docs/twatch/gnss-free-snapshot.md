# GNSS-free common-clock snapshots

Copyright (c) 2026 Jonathan Yantis. MIT.

`clock read` returns the separate `TWATCH_GNSS_FREE_SNAPSHOT_V1` contract in a
four-row Console V3 response. It requires the existing fresh nonce, ticket,
factory/boot binding, response deadline and CRC checks. It neither enables GNSS
nor manufactures a PPS observation. The main owner publishes one bounded PSRAM
snapshot; a read does not train the clock or renew a radio's permission.

BLE, LoRa and the common clock are projected at the same causal 20 MHz counter
tick after both receiver snapshots are available. Fields retain full integer UTC,
original model availability and expiry, observation IDs, counter/capture and owner
generations, source/parent/root lineage, model rates, conversion enclosures,
calibration matches, correction amounts and refusal reasons. A correction amount
is policy metadata when UTC is zero/unprojectable; count actual correction
application only for a nonzero mapped projection, and eligibility separately. LoRa metadata
creation/expiry and physical-pair counts remain distinct from projections. An
unknown LoRa depth is explicitly marked unknown.

The `*_causal_event` records retain the last eligible advancing physical
observation's pre-update innovation and its original tick, scope and observation
ID. Polls can repeat that event; analyzers must deduplicate it. Live projection
disagreement is also exported, including repeats; it is controller-input
consistency, not an independent timing error. Missing or ineligible projections
are unavailable evidence. No phase-modulo PPS statistic appears in this contract.

The optional exact-device profile field `study_fixed_priors` defaults to `false`.
An enabled, reviewed study profile may set it to `true` to carry its existing
fixed engineering priors onto unmatched authenticated paths. Such a path remains
`calibration_match=false`, with an unknown calibrated output bound. No new
membership, authentication or permission is granted. Existing receiver-side BLE
adjustment and incremental correction remain separate, so an already applied
device-input adjustment must not be added again.

For GNSS-free work keep Shadow closed and verify the receiver rail, UART and PPS
capture are off. The historical `pps read` payload is a different contract at
its captured edge and must not be treated as a current or independent witness.
Use the target's exact-device hardware guards, diagnostic archive and app-only
readback procedure for separately authorized installation. All raw captures,
enrollment and device-bound profiles remain ignored local artifacts.
