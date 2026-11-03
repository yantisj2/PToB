# T096 local BLE holdover

Copyright (c) 2026 Jonathan Yantis. MIT.

RC7 maintains a separate local BLE-trained clock for the watch and diagnostic
HOLD tests. The live BLE/relay/USB observation clock keeps its original source
and permission gates. Local HOLD has unknown uncertainty and is not exported
as an authenticated observation. GM/DC/PC discipline and LoRa's delivered
clock are unchanged.

## Frequency and continuity

A65-point history thins accepted physical pairs by450ms, never by residual.
Channel-centered16/32s fits require32 points,85% span and8 repeated-channel
degrees of freedom. Both rate guards must be<=10ppb, agree within5ppb and
improve on the fast-rate guard. The median with the fast rate must remain
within15ppb; adjustment is capped at5ppb. Insufficient, stale or rejected fits
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

The boot/source-bound `holdover` console query returns physical pair coordinates,
fit choices, actual local output map and two shadow lanes. One lane probes2s;
the other cycles12/32/64/120s. Each freezes candidate and fast-baseline clocks
at the same delivered phase. Later accepted BLE observations score them before
training either rate or live map. Earlier1us crossings remain in each prefix
maximum; gaps>250ms are counted without interpolation. Missing endpoint witnesses
expire as unknown one second after the probe. Source changes terminate the old
population. Timing events retain START/END/RESET identities and outcome maxima.

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
