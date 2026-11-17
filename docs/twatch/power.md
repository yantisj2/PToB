# T-Watch Ultra power plan

Copyright (c) 2026 Jonathan Yantis. MIT.

BLE listens at 100%; LoRa learns authenticated transmit windows on its own SPI
host and sleeps between them. Unknown or missed schedules use continuous RX.
Display sleep does not stop the clock or either enabled receiver policy. MCU
deep-sleep battery figures do not describe this workload.

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
- Gate the main GNSS BLDO1 supply to the awake, visible Shadow face. Preserve backup and CPU rails. SD, audio,
  NFC supplies are off. Enable the sensor’s ALDO4 at 1.8 V for Bosch
  glance detection; only gesture events are subscribed. Do not rewrite battery gauge calibration,
  charge voltage or charge current during app startup.
- A [wrist raise](wrist-wake.md) wakes the last face for 5 seconds; repeated
  motion does not extend the glance. Touch restores the normal timeout.
- Persist user settings only on changes. Do not persist radio candidates,
  clock fits or timing permission. Reboot requires fresh authenticated admission.
- Keep the timing counters continuous. CPU frequency scaling uses an 80 MHz
  minimum and 240 MHz maximum with explicit BLE and panel holds. MCU light
  sleep and deep sleep remain disabled.

## CPU, console and panel power

The CPU uses ESP-IDF frequency scaling between 80 and 240 MHz. Active tasks
can still run at 240 MHz; 80 MHz is the eligible idle floor, not a promise that
all screen-off work runs at 80 MHz. BLE capture keeps its existing 240 MHz
requirement. An additional watch hold boosts before SDK/capture initialization
and releases only after actual controller, host and capture retirement. The UI
holds 240 MHz during presentation and releases it after panel sleep. The XTAL
20 MHz coordinate and APB 80 MHz capture clocks remain continuous. No timing
phase correction, LoRa schedule or IMU configuration changes with this policy.

With VBUS absent, console input performs no driver reads, routine USB text and
hex rendering are skipped, and output blocks on task notification. Empty output
also blocks on USB. Pending output retains its existing bounded retry and
expiry. Cable transitions abandon queued/partial output with explicit drop
counters, disarm diagnostic capture, and invalidate partial input lines; old
responses do not replay after reconnect. The existing UI PMU observation updates
VBUS state every 500 ms, so disconnect handling has that observation latency.
Persistent fault recording remains independent of USB. A charge-only supply
still counts as VBUS; it is not evidence that a host is reading diagnostics.

Panel sleep sends Display Off and Sleep In, waits 120 ms, then lowers only the
XL9555 display-enable output. ALDO2 and the CST9217 touch controller remain
powered; touch reset and touch polling are preserved. Wake raises display enable,
sends Sleep Out and waits 120 ms before transferring fresh pixels and issuing
Display On. Do not substitute an ALDO2 cutoff: the vendor documents increased
leakage with that approach. Physical touch reliability and energy savings require
field observation.

`TWATCH_UI` adds `cpu_mhz`, `cpu_ble`, `cpu_panel` and `cpu_rc`. The frequency is
a sample taken during an active status task; it does not measure idle residency
or current. `cpu_rc` retains the first power-manager failure. Source permissions,
counter-wrap service and the main timing loop's cadence are preserved.

Offline regressions include the actual power-manager calls, owner holds, failed
acquire/release, repeated BLE startup/teardown, panel command ordering, USB
dormancy, partial-frame disconnect, reconnect and original response expiry:

```sh
python3 firmware/twatch/tools/energy_tests.py --out artifacts/watch-energy-01
python3 firmware/twatch/tools/console_input_tests.py --out artifacts/watch-input-01
```

## Measure before optimizing

The Battery app records gauge percentage every 30 seconds, including display
sleep, and integrates actual panel-on time into minute buckets in PSRAM. Its
aligned 1/6/12/24-hour charts default to one hour and retain a rolling 24-hour
boot session. Partial-minute window edges are proportional. System uptime is
shown separately. Neither gauge history nor display duty cycle measures current,
energy or battery endurance. The upper-left PMU key explicitly sleeps/wakes the
panel even on USB; this does not change either radio's runtime policy.

Record battery current, voltage, display duty cycle, uptime, reset reason and
source continuity for screen-on, screen-off, BLE acquisition, BLE lock and LoRa
fallback. Include display interaction and worst-brightness runs. Measure charging
and battery-only operation separately; a USB power reading is not battery drain.
Record missing intervals and watchdog events. Compare timing residuals and loss
rates with display activity; report battery lifetime only from actual discharge.

Then consider lower brightness, partial panel updates and a verified light-sleep
policy. Deep sleep is an explicit receiver-off mode and must never masquerade as
continuous LoRa fallback. The Shadow face requests GNSS only while visible and awake, with verified main-rail
OFF on exit/sleep and independent capture retirement. Its reception, physical PPS
routing and power budget remain hardware checks; it cannot discipline BLE.

## Radio power implementation

BLE Settings OFF now drains receiver ownership, stops the NimBLE host and
successfully disables/deinitializes the controller before acknowledging OFF.
The previous activity-only stop left the controller initialized. This change
establishes controller state, not measured battery savings. LoRa remains
independent and uses boosted RX (`SetRx(0xffffff)`) inside guarded windows,
with configuration-retaining sleep between them. Its worker polls at 2 ms while
receiving and 50 ms while asleep; OFF and maintenance notifications interrupt
the wait. The MCU and timing counter remain running.

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

### 2. Learn receive windows

The fixed-channel schedule8 uses a **30-second timing-pair cycle** and periodic
**META every 360 seconds**. The watch starts in continuous RX and requires four
consecutive authenticated pairs plus two META observations one cycle apart in
the same transmitter/boot/context. Physical receive timestamps set the phase;
UTC modulo arithmetic and unauthenticated RF activity do not qualify a schedule.

Wake starts **2 seconds before predicted SYNC or META**, including radio startup.
Once an authenticated SYNC/FOLLOW pair completes, sleep immediately unless the
META window is still outstanding; authenticated META likewise completes its own
window. SYNC alone, bad authentication or an incomplete packet cannot end RX.
If data is missing, listen through **8 seconds after SYNC** or **2 seconds after
META**, then discard the forecast and remain in continuous RX. Overlapping
windows merge. A pending header/payload blocks sleep.

A single missed pair or META window discards the entire forecast, including its
META phase. The radio remains in continuous RX until four consecutive pairs and
two periodic META observations qualify again. Reacquisition can therefore take
more than six minutes. A changed emitter/boot/context, irregular cadence,
out-of-window packet, invalid capture map or worker gap over 250 ms also cancels
the forecast. This policy applies whether BLE or LoRa supplies the display.
It never extends source permissions or timing evidence.

### 3. Configuration-retaining radio sleep

Only the SX1262 sleeps. The capture timer/epoch, clock estimator, authentication
and replay floors remain alive. The worker continues bounded counter-map upkeep
at 50 ms rather than polling every 2 ms. Source loss retains the watch clock
under the existing HOLD policy, with unknown accuracy.

The owner enters standby and rechecks pending IRQ state before `SetSleep(0x04)`.
It does not wait for BUSY after sleep: BUSY remains high until wake. SPI stays
quiet for at least 500 µs. Wake asserts NSS before the bounded BUSY wait, verifies
standby status, restores/verifies boosted gain, and rearms continuous RX.
The two-second lead includes startup; no timing calibration is changed.

OFF/maintenance uses the existing reset and bounded capture retirement instead
of pretending sleep is OFF. Wake/SPI failures use the existing radio-only fault
path; they do not reboot the watch or discard its retained time. A header racing
standby or an unexpected IRQ during sleep cancels the schedule and resumes RX.

Protocol commands follow Semtech's
[SX126x reference driver](https://github.com/Lora-net/sx126x_driver/tree/v2.3.2)
and the SX1261/2 command-interface sleep/wake requirements. This is host-scheduled
sleep, not `SetRxDutyCycle`. No Semtech driver source is copied into this owner.

### Diagnostics and validation

`lora diag` reports `TWATCH_LORA_SHADOW version=2 shadow_only=0` for the learned
forecast and `TWATCH_LORA_SLEEP version=1` for actual owner state. The latter
includes `sleeping`, `active`, `sleeps`, `wakes`, `refused`, `sleep_us`, `rx_us` and
`transition_us`. Residency is sampled software state, not measured current.
All counters are RAM-only; there is no periodic flash logging.

Forecast fields retain `qualified`, `meta_phase`, `next_sync_us`, `next_meta_us`,
`covered`, `outside`, `misses` and `resets`. Reasons: 0 learning, 1 tracking,
2 OFF/fault, 3 context change, 4 missing pair, 5 outside window, 6 cadence,
7 clock reversal, 8 META unavailable, 9 worker/IRQ discontinuity, 10 map unavailable.
Forecast `would_rx` predicts receive demand; use the separate sleep record to
inspect actual residency. The additional `wake_lead_us` is zero in this policy.

```sh
python3 firmware/twatch/tools/lora_shadow_tests.py --out artifacts/watch-windows-01
python3 firmware/twatch/tools/radio_owner_tests.py --out artifacts/watch-sleep-owner-01
python3 firmware/twatch/tools/radio_tests.py --idf "$IDF_PATH" --out artifacts/watch-radio-01
```

These offline tests cover windows, complete relearning, the actual owner's
SPI/BUSY/IRQ boundaries, cancellation and failure containment with simulated
peripherals. Live window coverage, late/early transmissions, train resets,
out-of-range return, clock continuity and battery-side current still require
hardware validation. No measured battery-life improvement is claimed.

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

## Field test for the power candidate

Use the same brightness, timeout, face and radio settings for comparisons.

1. Unplug USB, set BLE OFF and leave LoRa in its normal enabled mode. Let the
   screen time out, then try at least 20 touch wakes, including after 10 minutes
   asleep. Each first touch should reveal the current time; a second gesture
   operates the UI. Check both buttons and every face for a black or stale frame.
2. Use the Battery app's 1h and 6h charts to record starting/ending percentage,
   elapsed time and display-on total. A two-hour comparison is useful; an
   overnight run is stronger. Record restarts, warm casing, missed wakes and
   unexpected display activity. Percentage is not a measured current.
3. Turn BLE ON, confirm reception returns, then repeat OFF/ON and touch wake
   several times. Report a persistent WAIT/FAULT, frozen time, backward step or
   lost touch response. Leave LoRa and wrist detection at their normal settings.
4. Reconnect USB after a long battery interval. Diagnostics and ordinary update
   access should return within a few seconds. Check that Battery history and
   uptime survived; an unexplained reset is a failure, even if time reacquires.

If the screen fails to wake, try the upper-left power key and retain the
circumstances. Use the documented BOOT/recessed-RESET recovery only when needed.
A passing field run establishes finite behavior, not independently measured
UTC accuracy or calibrated battery capacity.
