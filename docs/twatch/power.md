# T-Watch Ultra power plan

Copyright (c) 2026 Jonathan Yantis. MIT.

Revision one prioritizes continuous timing reception. BLE listens at 100%; LoRa
runs passive continuous RX on its own SPI host. Neither source is suspended when
the display sleeps. This sets a real power floor: deep-sleep battery figures from
the vendor do not describe this receiver workload.

## First revision

- Default to 40% panel brightness and a 30-second display timeout; selectable
  brightness 10–100% and timeout 15/30/60/90 seconds.
- Digital/settings render at 1 Hz or on interaction. Instrument requests smooth
  updates at up to 20 Hz while awake, bounded by actual render/transfer time;
  no frame queue accumulates. Stop panel rendering during display sleep.
- Keep the framebuffer and one 411,640-byte immutable Instrument dial layer in
  PSRAM. Warm the dial once during first presentation; subsequent face swipes
  reuse it. Ordinary redraws restore only tiles touched by the previous moving
  indications and transfer the union of old/new changed tiles; use a bounded
  8-row internal DMA strip. Bound SPI and
  I2C transactions; fence a failed display transfer instead of reusing in-flight
  buffers. The UI runs at low priority on the core opposite BLE.
- Disable the main GNSS BLDO1 supply. Preserve backup and CPU rails. SD, audio,
  NFC supplies are off. Enable the sensor’s ALDO4 at 1.8 V for Bosch
  glance detection; only gesture events are subscribed. Do not rewrite battery gauge calibration,
  charge voltage or charge current during app startup.
- A [wrist raise](wrist-wake.md) wakes the last face for 5 seconds; repeated
  motion does not extend the glance. Touch restores the normal timeout.
- Persist user settings only on changes. Do not persist radio candidates,
  clock fits or timing permission. Reboot requires fresh authenticated admission.
- Keep the timing counter continuous. CPU scaling/light sleep/deep sleep are
  deferred until capture continuity, radio deadlines and wake behavior are tested.

## Measure before optimizing

Record battery current, voltage, display duty cycle, uptime, reset reason and
source continuity for screen-on, screen-off, BLE acquisition, BLE lock and LoRa
fallback. Include display interaction and worst-brightness runs. Measure charging
and battery-only operation separately; a USB power reading is not battery drain.
Record missing intervals and watchdog events. Compare timing residuals and loss
rates with display activity; report battery lifetime only from actual discharge.

Then consider lower brightness, partial panel updates and a verified light-sleep
policy. Deep sleep is an explicit receiver-off mode and must never masquerade as
continuous LoRa fallback. GNSS shadow PPS remains a later opt-in revision with
verified PPS routing and a power budget; it cannot silently discipline BLE.

## Radio power implementation

BLE Settings OFF now drains receiver ownership, stops the NimBLE host and
successfully disables/deinitializes the controller before acknowledging OFF.
The previous activity-only stop left the controller initialized. This change
establishes controller state, not measured battery savings. LoRa remains
independent and uses continuous boosted RX (`SetRx(0xffffff)`); its worker still
wakes every 2 ms. The window scheduler is observation-only: radio sleep is not
enabled.

### 1. BLE controller OFF and restart

A watch-owned lifecycle drains scanning, connections, periodic synchronization
and callbacks before retiring host event owners. A separate worker stops the
host and calls the pinned ESP-IDF NimBLE teardown path. Controller memory is
not irreversibly released, so Settings ON can rebuild the host without reboot.
A timeout remains a visible fault, never a false OFF acknowledgement. Host-stack
memory is reserved before SDK initialization, and the temporary lifecycle worker
retires before ordinary reception. Same-boot restarts use a fresh admission ID.

Eligible startup failures receive one bounded radio cleanup. The supervisor
retries only after physical OFF is proven and the 90-second fault interval has
elapsed. First and latest errors remain distinct; a dedicated first-fault flash
record survives recovery and ring wrap. An unsafe or failed cleanup remains
contained instead of rebooting the watch or pretending the radio is OFF.

External command posters and BLE timestamp readers are fenced before teardown.
Restart rebuilds host events and requires fresh synchronization and authenticated
mesh admission. The old BLE-to-counter mapping is discarded when the controller
clock is recreated; the continuous timing counter and learned oscillator rate
remain intact. No MCU reset or LoRa stop is used for a normal BLE transition.
The retained display clock does not grant fresh timing authority.

Enabled-controller modem sleep remains a separate experiment requiring fine
capture continuity checks. USB diagnostics expose requested and actual states.

### 2. Learn and shadow LoRa receive windows

The watch's fixed-channel schedule8 is **30 s per timing pair**, **360 s between
periodic META** (12 beats), with SYNC at nominal slot +7 s and FOLLOW nominally
2.5 s later; the existing FOLLOW acceptance limit is 4.5 s. Launch delay,
packet airtime, header capture and processing deadlines must remain distinct.
These values come from `firmware/t096/protocol/lora_passive_schedule.h`, not the
ordinary non-fixed profile's 300 s metadata cadence.

Start in continuous RX. Require authenticated META and at least four successful
pairs in the same emitter/boot/stream/context before predicting sleeps. Use
physical receive timestamps and authenticated cursor progression, not UTC modulo
30 s or unauthenticated RF activity. First run the scheduler in shadow mode,
recording whether its proposed windows would have captured every actual packet.

Initial conservative proposal: RX from **4 s before predicted SYNC until 8 s
after it**, covering both packets. Derive extra margin from measured scheduling
residuals, elapsed oscillator drift, TCXO/startup time and task wake latency;
never shrink below the initial margin in the first release. Keep RX through any
in-flight header/payload or pending FOLLOW deadline. Add an overlapping window
for META approximately 7 s ahead of SYNC on its scheduled beat. Unknown META
phase means continuous RX until learned; skipped/retried or manually requested
META must not be assumed to follow the regular phase.

A missed pair, out-of-window packet in shadow mode, stale metadata, counter/map
fault or changed emitter/boot/context returns immediately to continuous RX.
Relearn before sleeping again. This also discovers replacement transmitters
whose phase differs from the old source. Source selection remains independent:
LoRa follows this policy whether BLE is selected or OFF. The displayed held
clock keeps running during reacquisition, with stale quality withdrawn.

### 3. Sleep the SX1262 without discarding timing state

Use configuration-retaining radio sleep between windows, with a timer wake and
bounded NSS/BUSY/TCXO startup, rather than invoking the existing LoRa OFF/reset
path each cycle. Preserve authentication, replay floors, estimator, continuous
capture counter and its epoch. Refresh the capture-to-reference mapping before
RX and refuse precision if its mapping is stale; never reuse an unverified old
map. Clear retired IRQ state and verify boosted-gain configuration on wake.
Keep boosted RX initially to avoid trading field sensitivity for power.

Replace the 2 ms worker polling during sleep with deadline/event waits. Keep
maintenance/OFF/fault notifications immediately actionable, and prevent the
heartbeat watchdog from treating deliberate sleep as a stalled worker. This
first step sleeps only the radio; MCU deep sleep and changes to the continuous
timebase remain out of scope.

Semtech's [SX126x reference driver](https://github.com/Lora-net/sx126x_driver/tree/v2.3.2)
provides sleep, wake HAL and RX duty-cycle primitives. Prefer host-scheduled
windows for our two-packet protocol; autonomous `SetRxDutyCycle` requires separate
preamble and timeout analysis. Semtech also lists
[AN1200.36 on its SX1262 page](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262).
Chip typical currents are not watch-level battery measurements.

### Validation and rollout

First qualify real BLE controller OFF/ON, then shadow LoRa scheduling, then
opt-in radio sleep. Test late/early packets, skipped cycles, missing FOLLOW/META,
new transmitters, stale callbacks, wake failures and OFF/maintenance while asleep.
Compare continuous and windowed RX for accepted pairs, missing samples, RMS,
holdover, source transitions and clock continuity. Add RAM counters for RX/sleep
residency, wake lateness, missed windows, fallback reasons and emitter changes;
read detail over USB, with no periodic flash logging.

The initial ordinary window is 12/30 s RX, or a theoretical 60% reduction in RX
residency before META windows and recovery overhead. It is not a 60% reduction
in total watch power. Measure battery-side current with display asleep for BLE
ON, true BLE OFF with continuous LoRa, and true BLE OFF with windowed LoRa.
Retain continuous RX as the fallback until live transmitter and field tests pass.

## Shadow scheduler diagnostics

The shadow-only scheduler leaves continuous boosted RX unchanged. It observes
only authenticated/admitted META, SYNC and completed pairs through the actual
receiver. Four consecutive pairs and two META events approximately 360 seconds
apart are required before it predicts sleeping. The `lora diag` USB response
adds `TWATCH_LORA_SHADOW`, version 1, with `shadow_only=1` and `sleeping=0`.

`tracking`, `meta_phase`, `qualified`, `next_sync_us` and `next_meta_us` describe
learning. `would_rx` is the proposed state only. `covered` and `outside` score
packets against the forecast before the packet changes it; `misses` counts
expired expected pair windows. `rx_us` and `sleep_us` are sampled virtual
residency while the receiver is active; OFF/fault time is separate `inactive_us`.
Service gaps longer than 250 ms go into `unknown_us`, not savings.
Reasons are 0 learning, 1 tracking, 2 OFF/fault, 3 changed context, 4 missing
pair, 5 outside window, 6 cadence mismatch, 7 clock reversal, 8 META unavailable.
All counters are RAM-only. No state enables radio sleep or changes timing leases.

Offline checks: `python3 firmware/twatch/tools/lora_shadow_tests.py --out
artifacts/watch-lora-shadow-check`. The actual authenticated event bridge is
also exercised by `radio_tests.py`. Live shadow coverage needs a running TX and
at least two metadata broadcasts before any proposed savings can be evaluated.

## BLE controller lifecycle diagnostics

`TWATCH_UI` includes the requested BLE power state, actual SDK controller state,
host-run state and lifecycle stage on every CRC-framed response. OFF is only
acknowledged after host exit and successful controller deinitialization.
Stages are 0 ACTIVE, 1 DRAINING, 2 PREPARING, 3 HOST_STOP, 4 HOST_EXIT, 5 DEINIT,
6 OFF, 7 INIT, 8 SYNC and 9 FAULT. SDK controller values are 0 IDLE/deinitialized,
1 initialized and 2 enabled. A stage timeout retains its original failure and
never masquerades as OFF. Status includes generation, SDK return code, failed
stage and the original deadline. `ble_sync_site` identifies failures in address
selection (1), the mesh random address (2), or client admission (3);
`ble_sync_rc` retains the original result. `ble_power_worker` and
`ble_power_alloc_failures` expose temporary-worker lifetime and allocation retries.

Settings uses WAIT during transitions and FAULT on lifecycle failure. The
shutdown worker runs separately from the main timing loop and exists only while
SDK transition work is needed. It releases its 4096-byte stack before host Sync
and in settled states; task allocation retries keep the original deadline. External command,
host event and BLE timestamp readers are fenced before host destruction.
The continuous watch and LoRa counters, display-only held time and learned
oscillator estimate remain independent of controller lifecycle. ON requires
fresh host synchronization and authenticated source admission.

The bounded `mobile_capture.py --ble-cycle --radio-only --seconds 120` check
requires both receivers initially ON, verifies controller IDLE plus host exit
for OFF, then controller ENABLED plus running host for ON. LoRa must remain
active without faults. Clock validity and backward steps are recorded throughout;
that finite observation is not a current measurement or precision qualification.

## Display progress diagnostics

On-demand `TWATCH_UI` status reports `ui_phase`, `ui_phase_age_us`, `ui_loops`
and `ui_loop_max_us`. These RAM-only counters are published independently of the
rendered snapshot, so a stalled UI cannot appear healthy merely because its last
frame said `ready=1`. Phases: 0 not started, 1 initialization, 2 input, 3 IMU,
4 PMU, 5 settings/panel state, 6 presentation clock, 7 calendar, 8 battery,
9 rendering, 10 transfer, 11 waiting, 12 shutdown, 13 stopped, 14 failed.
The live power-cycle harness rejects an awake USB display snapshot over three
seconds old. A finite passing capture does not establish indefinite stall freedom.
