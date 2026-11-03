# T096 local BLE holdover

Copyright (c) 2026 Jonathan Yantis. MIT.

RC7 maintains a separate local BLE-trained clock for the watch and diagnostic
HOLD tests. The live BLE/relay/USB observation clock keeps its original source
and permission gates. Local HOLD has unknown uncertainty and is not exported
as an authenticated observation. GM/DC/PC discipline and LoRa's delivered
clock are unchanged.

## Frequency and continuity

A 65-point history selects the first real accepted pair per 500 ms bucket, never
by residual. Channel-centered16/32s fits require24/32 points,85% span and8 repeated-channel
degrees of freedom. Short/long rate guards must be<=20/10ppb, agree within10ppb and
improve on the fast-rate guard. The median with the fast rate must remain
within three fast guards(bounded15..250ppb); adjustment is capped at150ppb.
Insufficient, stale or rejected fits
retain the fast rate. Fits refresh at most once per second outside interrupts,
spinlocks and formatting. Integer arithmetic uses checked, centered coordinates.

The local map preserves the previously delivered time and slews phase toward
the fresh clock. A discrepancy above1ms reacquires explicitly. During loss it
freezes frequency and residual phase, expiring120s after its last model update.
Parent/root identity, boot/epoch, capture and UTC-coordinate changes fence the
history. Credentials and fresh authenticated admission remain separate.
Temperature never steers this clock; nothing is learned into flash.

All new owner state, two paired trials and the published snapshot are bounded
by1280 bytes. The native build must also retain the existing1024-byte static
RAM floor, stacks, controller queues and USB buffers. Exact linked resources and
runtime high-water observations remain separate checks.

## Shadow and finite output tests

The boot/source-bound `holdover` console query returns committed rate-input
coordinates, all-witness coordinates/cursor, fit choices, actual local output
map and the exact frozen entry maps of two shadow lanes. `holdover data` is a
smaller read-only latest-witness snapshot. It cannot capture bursts losslessly.
The raw witness cursor counts consumed physical pairs, separately from packet
and protocol acceptance counts. Gaps in capture remain missing records.
One lane probes2s;
the other cycles12/32/64/120s. Each freezes candidate and fast-baseline clocks
at the same delivered phase. Later accepted BLE observations score them before
training either rate or live map. Earlier1us crossings remain in each prefix
maximum; gaps>250ms are counted without interpolation. Missing endpoint witnesses
expire as unknown one second after the probe. Source changes terminate the old
population. Timing events retain START/END/RESET identities and outcome maxima.
Completed endpoints stay visible for500ms before the slot is reused.

`holdover 8000fa00` selects64000ms paired shadow probes with staggered starts;
the high bit selects measurement only, with1000..120000ms supported. High bit
alone restores the default lanes. A measurement-mode change closes old trials
as unknown and begins a separate population without changing the delivered
clock. Host-side overlapping frozen clocks can measure a denser population
using the same committed output maps and subsequent witness trace, using no
additional MCU storage. Their populations and correlation must stay explicit.

`holdover 00002ee0` requests a12000ms finite local-output freeze while BLE stays
a witness. Durations are eight hexadecimal digits,1000..120000ms; zero stops.
Admission needs a fresh settled model. `test` fields are active, witnesses,
maximum absolute error, first crossing(age_ms+1, zero absent), gaps, signed result
and pending request. A return reanchors from the actually delivered local time.
This affects the local estimate, not RF permissions or strict USB observations.

Existing explicitly enabled PPS shadow sessions label this estimate
`BLE_LOCAL_HOLD`. PPS never enters its frequency, phase, rate selection or
acceptance. The witness map's120s diagnostic lifetime adds no source lease.
PPS-shadow and BLE-relative results remain separate populations.

Host analysis must use worst sampled absolute error from entry through a real
witness at or after each age. Require100 outcomes per age and retain the latest
128; failed and unknown outcomes count against empirical99% passing. Keep
devices, boots and contexts separate. Report all tails, witness gaps and
overshoot. Overlapping runs are correlated; sampled success is not a continuous
bound or independently calibrated UTC/output accuracy.

`holdover_replay.py` independently projects the recorded integer clocks,
checks the frozen on-device prefix maxima, and compares committed rate inputs
against floating-point channel-centered fits. A separate prospective host
population freezes100 actually delivered models two seconds apart after32 s
of captured warm-up. Every later accepted witness is scored without training
those clocks. Missing witness-cursor records make affected prefixes unknown;
an earlier observed failure remains a failure. This denser host population is
explicitly correlated and is separate from the two on-device lanes.

```sh
python3 firmware/t096/tools/holdover_replay.py capture.jsonl \
  --out holdover-replay.json
```

For a complete trace, `holdover capture 000a1220` enables660000ms of accepted
witness capture in the existing eight-record radio-audit ring. Zero stops;
the maximum lease is900000ms. `holdover trace <8hex cursor>` drains up to eight
records, reporting the next cursor, produced count and overwritten-record gaps.
The capture command returns the initial cursor without discarding existing audit
records. Each row contains audit sequence, witness sequence, tick, parent ns,
availability tick, channel, sampled-for-fit flag, capture epoch and context.
Ordinary thinned radio-audit records continue; only explicitly leased HOLD
records bypass thinning. No additional ring is allocated: smaller bounded
indices fund the lease within the existing1108-byte audit storage. Formatting
and host history remain outside the timing owner. Drain faster than incoming
records and retain sequence gaps. Missing
prehistory or missing records cannot certify full independent replay. The
production phase correction is an existing empirical calibration input, not a
new fitted offset or an accuracy bound.

A prospectively declared `--entry-wait-ms 1000` cohort takes the first eligible
fresh, settled model within one second of each scheduled start. An unmet deadline
is an unknown outcome. Eligibility uses only entry freshness and delivered phase;
later errors never select starts. This cohort differs from the original immediate
entry rule (the default); retain both rules and their failures separately.

## Offline checks and LoRa reuse

```sh
python3 firmware/t096/tools/holdover_tests.py --deps artifacts/deps \
  --out artifacts/t096-holdover-tests
python3 firmware/t096/tools/lora_runtime_tests.py --deps artifacts/deps \
  --out artifacts/t096-holdover-witness-tests
```

The portable `holdover.h` core owns no hardware, allocation or source permission.
`r2_hold_from_lora` converts LoRa's correction-rate multiplier to the inverse
RC clock convention while preserving its independently authenticated phase.
Tests exercise both signs and the full LoRa rate range. Its runtime integration
must retain separate transport provenance, freshness and phase qualification;
BLE-relative performance does not qualify LoRa or its slow cadence.
