# T-Watch Ultra changelog

## Unreleased

- Refresh the public target guide with all three face previews, everyday controls,
  build/update steps and explicit build-time location and tide-station setup.
  Keep experimental radio, wrist-wake and accuracy limits visible.

- Prevent repaired BLE permission expiries from repeatedly resetting source
  return qualification or forcing LoRa while authenticated pairs continue.
  Pause qualification through bounded sub-500 ms gaps, retain a cumulative gap
  budget, and expose switch/reset reasons with raw receiver counters on USB.
  Offline regression coverage does not establish field acceptance.

- Reserve the BLE host stack before SDK allocation, retire the temporary power
  worker, and use fresh admission IDs across same-boot radio restarts. Separate
  retained operating failures from cleanup safety; allow one bounded cleanup
  after eligible startup failures and radio-only recovery after 90 seconds.
  Require physical retirement proofs for maintenance, retain immutable first
  faults in dedicated flash slots, and deduplicate repeated recovery refusals.

- Bound malformed Bosch IMU FIFO parsing: reject unknown zero-size records,
  propagate wake-FIFO errors, and prevent large-buffer copy-index wrap. Parser
  errors disable gesture service while the UI and timing continue. Add UI phase,
  age and loop-progress diagnostics independent of the last rendered snapshot.

- Implement watch-owned BLE host/controller shutdown and restart, fencing host
  events and timestamp readers while preserving the continuous held clock and
  oscillator estimate. Rebuild the controller mapping and require fresh admission
  after restart; expose actual controller state and bounded lifecycle failures.
- Add authenticated LoRa shadow-window prediction with conservative SYNC/FOLLOW
  and META margins, coverage/miss counters and continuous-RX fallback. Actual
  LoRa sleep remains disabled pending live window validation.

- Replace the fixed LoRa transmitter pin with authenticated field-emitter discovery,
  retaining four non-evicting replay floors across radio OFF/ON and requiring
  fresh metadata/pairs after emitter changes. Physical reception on the updated
  image remains a separate check.

## 0.1.5 — development, 2026-11-16

- Apply the N30 LoRa owner safeguards to the watch: repeated OFF requests retain
  the original 200 ms drain, mode intent survives worker delay, and a quick
  OFF/ON retires the previous capture/model before starting a fresh generation.
  Reject old callback cookies and immediately fence revoked timing snapshots.
  Use full-width worker timestamps across the diagnostic heartbeat wrap.
- Keep watch operating-fault recovery separate from sticky cleanup failures.
  Run the shared owner race cases and watch recovery cases under ASan/UBSan;
  native builds remain offline preparation, with hardware validation pending.

## 0.1.4 — development, 2026-11-15

- Add Solar as the third persistent watch face, with a fixed Central 24-hour
  dial, location-derived sunrise/set and twilight chords, lunar phase and four
  chamfer-aligned circular subdials for RMS, sampled P99 hold, source strength
  and approximate predicted tide progress. The clock moves smoothly opposite
  the current civil-hour sun while seasonal horizons change behind the fixed
  scale. Preserve unavailable/expired states and three-way directional swipes.
  Native previews and year-round geometry checks are offline preparation, not
  installed hardware acceptance.

- Replace watch MCU fault/no-time resets with independent 90-second radio-only
  recovery requests, bounded owner retirement and an asynchronous NVS reason journal.
- Prevent retained BLE display estimates from blocking warm LoRa failover; use
  actual receive progress, repeated failure observations and stable BLE return.
- Preserve the last trained oscillator across BLE restarts and repeated parent
  acquisitions in the same boot counter. Initialize each new GR3/RC7 model only
  after its own authenticated physical pair, with fresh phase, fit and quality;
  keep prior time explicitly HOLD before that pair, including gaps over30s.
  Do not transfer source permission, lock or sampled P99 across the context.

- Add a compact offline Santa Rosa Beach Gulf calendar backend: approximate
  sunrise/set and civil twilight, Central DST conversion, and NOAA Destin/East
  Pass high/low predictions with explicit 2026–2027 coverage. Expose a smooth
  daylight brightness value for future consumers; manual brightness is unchanged.

- Refine Digital with local zone abbreviation, independent LoRa reception bars
  and integer P99 durations. Default to 30s sleep with 15/30/60/90s choices;
  keep the panel awake on valid USB power and transfer fresh content before reveal.

- Keep the BLE recovery timer armed after a previously working source disappears,
  even when its ten-second discovery evidence expires before the fifteen-second
  stall threshold. Use the existing bounded rebind and fresh authentication;
  rate-limit repeated silent recovery to once per minute. Field verification pending.

- Add an explicit watch-only relative fine receiver profile: RX17 hardware
  falling capture feeds the shared G1 servo in one continuous counter domain,
  with 50 ns delivered coordinates and a watch-derived phase convention.
  Preserve independent LoRa capture, unsupported/ambiguous input refusal,
  maintenance release and the default no-pad image. RF/UTC calibration remains
  unknown; field performance is separate from host regression results.
- Replace inactive strict-calibration holdover fields with sampled frozen-clock
  P99 trials. Preserve pending trials through short same-context interruptions,
  retain real error tails, and invalidate changed source/receiver contexts.
- Add servo phase/rate, fit and permission diagnostics, finite RX-only packet
  cohorts, a continuous-owner monitor and isolated BLE/LoRa recovery captures.

- Trial a settled-pose wrist detector in the measured left-wrist/buttons-elbow frame; retain Bosch tilt as a diagnostic witness. Preserve five-second glance and sustained arm-lower dismissal. Add pose readiness/rejection diagnostics and one bounded retry for a short interrupt-status read failure, never FIFO replay. Worn-watch validation remains pending.
- Add independent BLE/LoRa controls in Settings with owner-state feedback. Radio choices last for the current boot; both default ON after reboot. Add a finite hardware cycle harness that checks retirement and restart of each receiver. Wrist recognition remains experimental and unreliable in operator testing.

- Keep authenticated coarse BLE/relay time usable while precision fits train;
  retain coarse LoRa time after numerical fit failures without waiting for META.
- Run both streams independently with a 500 ms presentation grace and stable
  quality-aware return to BLE; retain display-only time until reboot.
- Slew wall-clock corrections and all mechanical indications; hold brief
  glitches and move unknown RMS gradually toward maximum without a red bar.
- Add Bosch BHI260AP on-sensor glance wake with a separate 5-second glance;
  sleep-only one-shot arming after wrist-tilt testing missed raises;
  normal touch/button sessions retain the saved 90/120/180-second timeout.
- Restore and transfer changed Instrument tiles, with pixel-equivalence checks
  against full rendering; accept the fixed five-row source diagnostic frame.
- Add actual-codec loss/recovery regressions and a bounded exact-device mobile
  capture harness. Physical gesture reliability, mobile RF accuracy, panel
  smoothness and battery endurance require hardware observations.

## 0.1.3 — development, 2026-11-15

- Replace Instrument reception indicators with a calendar-style source aperture,
  centered PLL phase well and matching LoRa receiver-active jewel.
- Align mirrored apertures, captions and phase geometry; remove the red source
  selector triangle.
- Remove the red MIN annotation from the hold dial; lower-bound-only evidence
  parks unavailable instead of implying an exact sampled hold duration.

## 0.1.2 — development, 2026-11-14

- Refine Instrument with directional bevels, recessed concentric subdials,
  centered scale lettering and luminous pilot-style hands.
- Combine the source selector, PLL phase disc and LoRa receiver-active jewel;
  distinguish RC9 from BLE-derived sources.
- Change Instrument RMS to a linear 0 ns–10 µs dial with an over-range flag.
- Add release-resolved horizontal face swipes with persisted selection;
  preserve tap controls and consume the complete first wake gesture.

## 0.1.1 — development, 2026-11-14

- Refine Digital with corner-safe insets, chamfered instrument framing and MODE.
- Add olive Instrument with analog time, RMS and P99 hold dials, reception
  shutters, moon-like PLL and battery reserve. Persist either selected face.
- Default/minimum display sleep is 90 seconds; migrate older short settings.
- Keep presentation time through missing snapshots with a separate bounded local
  clock. Withdraw precision indicators during this display-only extrapolation;
  source authentication, timing leases and receiver behavior are unchanged.
- Host behavior and native render checks cover unknown states, limits, face
  selection, timeout and presentation-only continuity. Hardware precision and
  endurance remain unqualified.

## 0.1.0 — development, 2026-11-14

- Establish a separate Ultra V1 SX1262 target using the W12/N30 timing backend.
- Add a native 410 × 502 digital face, launcher, face selector and persisted
  brightness/display-timeout settings.
- Separate display QSPI from passive LoRa SPI, use native USB diagnostics and
  keep GNSS off. Preserve runtime BLE discovery and authentication fences.
- Add factory/session/ticket-bound USB ROM entry after the original bounded
  maintenance drain and complete USB acknowledgment.
- Installation, physical power behavior and timing qualification remain separate
  commissioning gates; no inherited precision or battery-runtime claim.

- First-device commissioning: app-only installation and full application readback,
  protected-prefix/tail preservation, native display rendering, authenticated BLE
  reception and active SX1262 RX observed. Software USB ROM entry and return
  passed. Physical touch/aesthetic review, fallback timing, precision and battery
  endurance remain unqualified. Initial display command validation failure and
  USB reset-path failure were retained and corrected.

### Lower-left button shutdown

- Debounced double-click requests guarded AXP2101 power-off; single-click wake/launcher remains available after a 450 ms recognition window.
- Startup-held BOOT, long holds and failed maintenance drains cannot trigger software shutdown. No MCU reset fallback. Host gesture/drain checks pass; physical power-off is pending.

### Solar internal-memory correction

- Compute twilight-band heights one column at a time, removing a 6,560-byte internal-DRAM table from display startup. All 55 native previews are byte-identical.
- The preceding image passed flash readback but failed display initialization on two starts; those failures remain in local evidence.

### Button sampling independent of rendering

- Move lower-left button sampling to a 5 ms ESP timer callback; rendering and IMU I2C no longer hide short clicks. The callback never performs shutdown or bus work.
- Add RAM-only gesture, sample-gap and shutdown-stage diagnostics to USB status. Preserve the original guarded drain and PMU shutdown operation.
- Regression reproduces missed 70 ms presses at 200 ms UI sampling and detects one double-click at the new cadence.

### Solar swipe responsiveness

- Capture touch independently of rendering, retaining completed swipes while the frame is busy. Both directions away from Solar, stale-screen rejection, bus errors and wake consumption have host regression coverage.
- Both display and touch owners must stop before PMU power-off. Physical swipe responsiveness and timing are checked separately after installation.

### First-contact display wake

- Wake on initial touch contact and notify the waiting UI immediately. Consume that entire gesture and retain fresh-frame-before-reveal behavior.
- Add detection-to-reveal, render and flush wake diagnostics without periodic flash writes.
