# Wrist-raise display wake

Copyright (c) 2026 Jonathan Yantis. MIT.

The Ultra uses a **Bosch BHI260AP**, not the BMA423 found on earlier T-Watches.
The current experimental detector uses settled, direction-aware acceleration history for **left wrist, buttons toward elbow**. Bosch Wrist Tilt (67) remains a shadow counter; Glance (59) remains disabled. A 12.5 Hz corrected-acceleration stream supplies a short, volatile pose history for raising and arm-lower dismissal. Gyro, fitness and continuous orientation streams are not
requested separately.

## Prior art and integration

- [Bosch BHI260AP datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bhi260ap-ds000.pdf): virtual gesture sensors, host FIFO interface and power modes.
- [Bosch BHI2xy SensorAPI](https://github.com/boschsensortec/BHI2xy_SensorAPI): official RAM firmware upload, virtual-sensor discovery, event configuration and FIFO parsing. The selected driver and standard BHI260AP RAM image are pinned in [provenance.json](../../firmware/twatch/vendor/bhi260/provenance.json), with Bosch's BSD-3-Clause terms preserved.
- [LILYGO Ultra implementation](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/src/LilyGoWatchUltra.cpp): BHI260AP address, 1.8 V sensor rail and production-board axis remapping. The reviewed board revision is pinned in the target imports manifest.

I2C uses address **0x28**, SDA3/SCL2; sensor HIRQ is GPIO8. The production
mounting transform is P2: X/Y inverted and Z unchanged, applied to accelerometer
and gyroscope. The first wrist-tilt-only revision (sensor 67) produced missed raises and
unexpected wakes in operator testing. The baseline Glance detector used sleep-only one-shot arming. The Wrist Tilt stream runs continuously as a diagnostic witness; its events no longer wake the display. The host accepts a settled-pose raise only while asleep, after a 700 ms quiet interval and with fresh acceleration history. Refractory gating remains 1.5 seconds.
The operator’s initial wearing position is left wrist, buttons toward the elbow;
the board mounting transform is unchanged until physical orientation evidence
justifies changing it.

The adapter checks product identity, firmware boot and the actual
virtual-sensor availability before enabling gesture wake. Unsupported firmware
or initialization failure leaves touch/button wake operational and reports the
failure; it never silently substitutes ordinary movement for wrist recognition.

The public standard firmware is uploaded into sensor RAM at boot. Sensor flash
is not rewritten. FIFO servicing shares the UI's single I2C owner, with bounded
transactions and a 50 ms service deadline. Separate bus and deadline counters
retain failures; the gesture capture rejects any IMU failure. No motion processing runs in BLE or LoRa
callbacks. The sensor remains active during **panel sleep**; this is not ESP32
deep sleep. An exhausted transport failure disables motion wake until an explicit restart. One short INVALID_STATE failure reading the non-consuming interrupt-status register may be retried within the existing 50 ms deadline; FIFO reads and writes are never replayed. Both the first error and successful recovery count remain visible.

## Interaction

A recognized wrist raise while the display sleeps opens the last face for
**5 seconds**. Further gesture events during that glance do not extend it.
Touch/button interaction cancels the glance deadline and uses the saved normal
**90/120/180-second** timeout. Raising an already awake watch does not navigate
away from settings or shorten an interactive session. Gesture events are
coalesced with a 1.5-second refractory interval.

Returning toward the pre-raise pose dismisses a motion-triggered glance early.
The return must move about 41 degrees away from the viewing pose, approach within
about 26 degrees of the preceding pose and persist for 240 ms after filtering.
Large acceleration spikes, stale samples and small adjustments do not dismiss it.
This angular comparison is independent of left/right wearing orientation.
Touch promotes the glance to an interactive session and cancels arm-lower
dismissal. The five-second timeout is temporary troubleshooting policy;
arm-lower dismissal is permanent.

The host detector is an original adaptation of the settled-history and angular-change approach used by InfiniTime, not copied PineTime code or thresholds. Three normalized samples must settle (direction variance at most 0.012, magnitude spread at most 25%). A resting pose with normalized Y > 0.10 must persist 240 ms (including the measured seated near-face-up rest), followed within four seconds by a −Y viewing pose (Y < −0.55, Z > 0.05, |X| < 0.65), at least about 53 degrees from the starting pose and held for 160 ms. Sample gaps over 400 ms clear history. Return dismissal uses the captured starting and viewing poses. These thresholds reflect the measured wearing frame and remain experimental; other wrists/orientations are not yet supported.

`TWATCH_UI` exposes `imu`, `imu_stage`, `imu_error`, `imu_events`, `imu_errors`,
`wrist_wakes`, `imu_algorithm`, `imu_samples`, `arm_lower`, `imu_bus`, `imu_budget` and `asleep`. These distinguish detected gestures from actual display
wake actions. No raw personal motion stream is recorded by default.

## Validation limits

The host algorithm follows established watch-motion techniques; that does not establish its detection rate on this installed watch. Host tests cover the exact 5-second
boundary, repeated-event suppression, face restoration and touch promotion.
Physical checks must include seated and standing time-check motions, walking,
arm swing, desk work, both wrists and face orientation. Record attempted raises,
detected raises, false wakes, latency and screen duty cycle, alongside radio loss
and frame timings. Measure current on battery; datasheet sensor currents are not
whole-watch endurance results. Left/right wrist preference and sensitivity remain
future settings if field testing shows they are needed.

Current operator testing still misses wrist raises. The latest finite service check had no IMU bus or service-budget faults; this does not establish usable gesture detection. Use touch/button wake for field tests until wrist recognition is retuned.

## Diagnostic candidate and return-to-bench procedure

`imu diag` reports host detector **1001** and its supporting diagnostics; this is a PToB diagnostic identifier, not a Bosch virtual sensor ID. Earlier comparisons used 59 and then 67 for wake. Operator trials found missed raises and opposite-direction tilt events despite fresh samples. Board P2 mapping remains unchanged; the host wearing frame is explicit. Detector selection is not worn-watch acceptance.

### What the review established

| Finding | Evidence and implication |
| --- | --- |
| Board mapping matches the vendor | Pinned LilyGoLib `fe74cf72326c5673e43d69be4883a6ba5f589aa9` selects P2. SensorLib `93d109d330a3b212b0a61236bbfcb1959df66f06` maps both accelerometer and gyro to `diag(-1,-1,+1)`. The candidate reads both matrices back and refuses gesture initialization on a mismatch. This verifies configuration, not the physical axes of this particular watch. |
| Glance does not specify a particular wrist motion | [Android's interaction-sensor contract](https://source.android.com/docs/core/interaction/sensors/sensor-types#glance_gesture) leaves its motion definition to the manufacturer. The name alone cannot establish that a natural watch-check motion will trigger Bosch's image. |
| Glance and wrist tilt have different reporting modes | [Bosch's sensor table](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bhi260ap-ds000.pdf) lists 59 as one-shot and 67 as on-change. The initial comparison counted 67 as shadow-only. Sensor 67 now remains an independent shadow witness; host detector 1001 controls wake. |
| Vendor firmware differs | LILYGO's pinned initialization selects KLIO firmware; this target uses Bosch's standard BHI260AP image. Both gesture IDs must be discovered at runtime. Algorithm equivalence between images has not been established; firmware is not swapped speculatively. |
| An established watch implementation checks settled motion | [InfiniTime's motion controller](https://github.com/InfiniTimeOrg/InfiniTime/blob/main/src/components/motion/MotionController.cpp) combines recent acceleration history, variance and roll change, and has a separate lower-to-sleep decision. Its axes, scale and sample rate belong to PineTime, so its thresholds cannot be transplanted directly. No InfiniTime code is copied. |

Pinned vendor sources: [Ultra initialization](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/fe74cf72326c5673e43d69be4883a6ba5f589aa9/src/LilyGoWatchUltra.cpp),
[SensorLib remapping](https://github.com/lewisxhe/SensorLib/blob/93d109d330a3b212b0a61236bbfcb1959df66f06/src/bosch/BoschSensorBase.cpp).
Board mounting and wearing orientation are separate: keep P2 fixed during the
six-pose test. Record **left wrist, buttons toward elbow** on every labeled run.
Do not change a correct board remap merely to compensate for a strap orientation.

### Added visibility

`imu diag` uses fresh Console V3 tickets and a CRC-protected five-row frame.
The main task copies the UI owner's snapshot; diagnostics perform no I2C work
and no radio changes. The `TWATCH_IMU` row contains:

- Firmware kernel, available-sensor mask (glance=1, acceleration=2, wrist tilt=4),
  verified P2 mapping, interrupt configuration and sampled IRQ level.
- Requested arming state, arm/disarm counts, last read-back selected-detector rate and
  initial acceleration rate. Rates are in millihertz; detector readback is historical,
  not proof that a one-shot sensor remains enabled after its event.
- Corrected acceleration **counts**, hub timestamp, host receipt time and sample
  age. Counts are not claimed as calibrated g. Host receipt time includes FIFO delay.
- Poll/service counts, maximum service and poll gaps, stale-IRQ fallback count,
  bus/deadline errors, FIFO overflow, sensor-error and reset metadata.
- Pose preparation and rejected unsettled-window counts (`pose_prepared`, `pose_rejected`), explicit wearing frame, plus bounded status-read retry/recovery counts (`bus_retries`, `bus_recovered`).
- Separate glance events, tilt events, actual display wakes, arm-lower
  dismissals and rejected events (awake, disarmed or refractory).

Only `imu diag` exposes axis snapshots; normal status retains aggregate counters.
No motion history is written to watch NVS. USB capture stays in ignored local
artifacts. The continuous tilt stream adds a sensor subscription, so its incremental power cost
still needs measurement.

Two code-level faults now have regression tests: service the FIFO before a new
arm so an old event cannot become a fresh wake; reset a stale gravity estimate
before magnitude rejection so an initial shock cannot poison the return gate.
A stale stream with IRQ low gets a bounded FIFO service attempt at most twice
per second. A runtime hub reset or sensor-error event invalidates readiness;
there is no unbounded retry/reset loop. These are tested fixes, not a demonstrated
explanation for the operator's missed raises.

### Capture after installing the reviewed candidate

Use the exact new build receipt and the normal guarded installation workflow.
Do not run against an older image that lacks `imu diag`.

```sh
python firmware/twatch/tools/mobile_capture.py \
  --factory VERIFIED_WATCH_FACTORY --receipt artifacts/NEW_BUILD/receipt.json \
  --out artifacts/watch-imu-01 --seconds 180 --imu
```

The capture requests diagnostic snapshots at roughly 2 Hz. It retains an IMU
fault through the finite capture instead of aborting before the diagnostic row;
its final status still fails if the IMU was unhealthy. Other ownership, CRC,
original response-deadline and display-error guards remain active.

1. **Six stationary poses:** face up, face down, top edge up, bottom edge up,
   right edge up, left edge up. Hold each for ten seconds and label only the
   settled middle six seconds. Expect opposite poses to produce opposite vectors
   and the three pose pairs to be approximately orthogonal. Establish screen-axis
   signs from these observations before evaluating wrist orientation. Unknown
   signs are a measurement task, not permission to invent a transform.
2. **Ten seated raises:** wait until asleep and armed; lower the arm, check the
   time naturally, then lower it again. Record attempted raise and lower times,
   including misses. Do not touch the screen during a scored trial. Repeat ten
   standing raises using the same wearing orientation.
3. **Negative activity:** label desk work, arm swing/walking and ordinary small
   adjustments. Record every unexpected wake, not only successful raises.
4. **Compare the paths:** fresh axes plus tilt events but no glance events points
   toward detector suitability/configuration; glance events without wakes points
   toward host gating; stale axes or errors points toward FIFO/I2C/service. A
   correct axis test plus absent events does not prove a hardware defect.
5. **Dismissal:** distinguish arm-lower events from the five-second timeout and
   touch promotion. Reject unavailable or stale pre-raise pose history explicitly;
   do not make lower-to-sleep depend on whether a radio is selected.

Create a local labels CSV with `start_s,end_s,kind,label`; kinds are `pose`,
`raise`, and `negative`, with times relative to capture start. Then:

```sh
python3 firmware/twatch/tools/imu_report.py \
  --frames artifacts/watch-imu-01/frames.jsonl \
  --labels artifacts/watch-imu-01/labels.csv --out artifacts/watch-imu-report-01
```

The offline report produces axis means/variance, event and rejection counts,
coverage checks and CSV snapshots. Mixed boots and reset counters are refused;
gaps or stale samples make a trial UNKNOWN, not MISSED. Two-hertz snapshots do
not reconstruct the full 12.5 Hz waveform or support precise wake-latency claims.
Only operator-labeled attempts provide a denominator for detection performance.

The current candidate replaces Bosch wake decisions with the direction-aware host trial after the controlled Bosch comparisons failed. Synthetic adapter tests exercise both directions, stale history, settled poses, shocks, awake gating and transport recovery. Sparse 2 Hz diagnostic captures establish direction but cannot validate the full 12.5 Hz recognition waveform. The next worn-watch trial must measure actual raises, misses and false wakes; host tests are not acceptance. Keep failures and field radio configuration in the comparison record.

### Transport failure follow-up

A finite startup pass did not establish continued operation: subsequent diagnostics found a stopped stream and one transport error before the pose capture began. That pose window is invalid. The next revision preserves the full ESP-IDF result, first failing register/length/direction/time/duration and maximum transaction duration in `TWATCH_IMU` (`bus_rc`, `bus_reg`, `bus_len`, `bus_write`, `bus_at_us`, `bus_work_us`, `bus_max_us`). It increases the per-transfer host wait from 10 to 30 ms within the existing bounded service policy; a scheduling timeout remains a hypothesis until the exact result is observed. Bosch specifies no host-I2C clock stretching, so this is not described as a clock-stretch fix.

The explicit ticket-bound `imu restart` command queues one UI-owner RAM reinitialization, reuses the I2C device handle, and leaves the radios alone. It is refused during maintenance or an already pending restart. It may briefly pause the display; it is never an automatic retry loop. Start a new capture after restarting because IMU counters and pose history reset. Failed or partially consumed FIFO transfers are never blindly replayed.
