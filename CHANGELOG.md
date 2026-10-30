# Changelog

## 2026-10-30 — approved holdover research packaging and W12 cleanup

- Preserve the approved LaTeX/PDF while archiving selected observations and
  reproducible source/protocol inputs compactly under `evidence/w12/`. Generate
  large analysis/prefix tables locally instead of tracking duplicate rows.
- Add approval/current-package identities and consolidate W12 holdover guidance.
  Preserve history, recovery artifacts and firmware behavior; no hardware or push.

## 2026-10-30 — MCXO-inspired holdover duration draft

- Prepare a seven-page author-review study of the longest sampled sub-1 µs P99
  W12 hold duration. Replay a 64-second device-grid frontier and a separate
  75-second fixed-cohort frontier (76 seconds fails); preserve later 24-second
  windows, failures, unknowns and exact version identities. Package public
  numerical evidence, replay and the current-code schematic; temperature stays
  shadow-only.

## 2026-10-30 — W12 exact receive history storage

- Save another 2816 bytes without dropping receive records or timestamp bits.
  Preserve the 256-entry capacity and association/fence rules; reconstruct only
  fields already guaranteed by validation. Hardware acceptance remains separate.

## 2026-10-30 — W12 minimum watchdog interval

- Enforce a five-second minimum on application watchdogs and wait five seconds
  before diagnosing main/host stalls. Raise the SDK interrupt watchdog to five
  seconds; task/boot remain five/nine. Preserve 120-second fault and 300-second
  no-time restart intervals, holdover sampling and original owner deadlines.

## 2026-10-30 — W12 compact exact estimator storage

- Reduce fixed estimator storage by 2792 bytes while retaining every accepted
  32-second history point and full-width error value. Sort percentile indices
  and store channel bytes without per-point padding.
- Preserve production HOLD, temperature shadow, all parallel duration probes and
  sampler populations. Offline equivalence does not establish hardware admission.

## 2026-10-30 — W12 GNSS host startup

- Reserve the full configured NimBLE host stack statically on GNSS W12 builds
  and check creation instead of silently accepting a missing dynamic task.
- Preserve host core/priority/stack budget, SDK options, GNSS, temperature shadow
  and production HOLD; bind allocation policy in native build inspection.

## 2026-10-30 — W12 sampled witness coverage repair

- Score each age through the first later actual BLE witness, retaining its full
  sampled prefix, overshoot and gaps. Keep unsupported expiry unknown; remove
  the arbitrary endpoint cutoff that suppressed P99 during ordinary BLE gaps.
- Preserve parallel short/long probes and production HOLD. Replay copied status
  snapshots at their recorded sample time rather than later log-stream state.
- Keep application `-Os` and use integer-only command scanning to fit GNSS,
  LoRa and RC9 features inside the original partition with SDK assertions intact.

## 2026-10-30 — W12 parallel P99 ramp

- Run 48 offset shadow clocks with frequent two-second lanes alongside the
  retained adaptive long probes. Preserve 100-run P99, prefix failures, coverage
  rules, production HOLD and temperature shadow isolation.
- Replay mixed-duration age populations independently. Retain GNSS features
  inside the original application partition using application-only `-Oz`;
  preserve SDK debug options and assertions.

## 2026-10-30 — W12 P99 hold-duration display

- W12 0.2.28 displays sampled hold seconds at1us rather than error nanoseconds.
  Use a continuous production-equivalent BLE-only shadow population, separate
  from temperature experiments. Preserve optimized production HOLD and diagnostics.
- Add bounded rolling duration statistics and independent replay, retaining
  crossings, gaps, test limits and warmup/expiry instead of inventing durations.

## 2026-10-30 — W12 causal HOLD rate estimation

- W12 0.2.27 adds bounded BLE-only HOLD rate consensus, separate from active
  steering. Preserve temperature shadow-only policy and continuous entry/return.
- Add accepted-channel/availability diagnostics, independent rate replay and
  2/4/8/12-second sampled error populations. Hardware accuracy remains unqualified.

## 2026-10-30 — W12 temperature shadow isolation

- W12 0.2.26 keeps temperature fitting in shadow and removes its steering from
  production HOLD. Preserve the optimized selector, BLE-rate anchor, continuous
  return, P99/P95/MAX, threshold crossings and coverage accounting.
- Keep the independent selected-output replay and finite BLE-witness tests.
  Missing temperature cannot disable rate-only HOLD. No persistent telemetry
  writes or partition changes are enabled.

## 2026-10-30 — W12 production thermal holdover

- W12 0.2.25 promotes the frozen BLE/temperature shadow projection to local
  HOLD, retains it through coarse acquisition, and resumes the trained BLE
  servo without a time step. Preserve source and relay expiry.
- Add finite production-output tests with BLE as a witness, independently
  replayed output and sampled P95/P99/MAX. Keep the P99 footer; distinguish
  activation verification from GM failover and independent accuracy.

## 2026-10-30 — W12 shadow frequency anchor

- W12 0.2.24 removes the historical absolute-rate blend at shadow entry.
  Anchor to the latest measured BLE frequency and compensate only subsequent
  temperature changes; keep held coefficients frozen and production unchanged.
  Count short BLE coverage gaps without restarting the frozen shadow clock.

## 2026-10-30 — W12 extended shadow holdover

- W12 0.2.23 replaces fixed one-second endpoints with frozen 120-second runs.
  Record each matched BLE error, first sampled 1 µs crossing, full-run maxima
  and coverage failures. Keep the production servo and shadow P99 footer.
- Preserve causal temperature integration beyond the short sensor history and
  isolate continuous learning from held coefficients. Report trial ages and
  paired comparisons; sampled duration is not a continuous accuracy bound.

## 2026-10-30 — W12 shadow holdover display

- W12 0.2.22 replaces the H forecast with sampled one-second shadow P99.
  The current servo stays active; BLE supplies all training and scoring.
  Capture records compare candidate and baseline P95/P99/MAX without trimming.

## 2026-10-29 — N30 real button shutdown

- N30 0.2.37 replaces non-clock four-second OLED blanking with deep sleep
  after radio/capture drain, OLED worker stop and button release. A fresh press
  reboots; the radio no longer keeps synchronizing behind an off screen.
- Keep the main Clock relay toggle and LoRa mode cycle. Add native input,
  display-owner handoff, original drain/timeout and power API host checks;
  battery lifetime, sleep current and physical wake remain unmeasured.

## 2026-10-29 — W12 button shutdown

- W12 0.2.21 adds a four-second hold on non-clock pages to request deep sleep
  after the original application drain and button release; a new press reboots.
- Preserve Clock relay toggling and LoRa cycling. Offline button, drain and
  off/wake checks accompany private per-device images; hardware acceptance remains separate.

## 2026-10-29 — Preserve robotics citation verification

- Retain the exact earlier S3 LaTeX source cited by the sealed robotics edition.
  A standalone launcher reproduces its original verifier without Git history.
- Keep the reviewed robotics source/PDF, original verifier and edition hashes
  unchanged; reject missing or changed cited snapshots and reviewed files.

## 2026-10-29 — Fit full W12 TZ profiles in the application slot

- Prepare W12 0.2.20 with application-only size optimization after the GNSS/TZ
  profile exceeded the original 1 MiB ceiling. Preserve every selected feature,
  SDK/controller optimization, assertions, calibration and flash boundaries.
- Check actual compiler flags in the builder and retained native inspector.
  Prepared per-device images remain private and are not hardware acceptance.

## 2026-10-29 — T096 GM automatic timezone and DST

- Add own-GNSS regional selection, pinned IANA 2026f offset calendars through
  2031, explicit manual modes, bounded position hold and unknown/UTC fallback.
- Update authenticated direct/relay coarse metadata dynamically without changing
  its four bytes, precision layouts, UTC coordinates, source or mesh epochs.
- Include deterministic generation, explicit geographic cuts/omissions,
  append-only IDs and dataset terms. Retain ambiguous outcomes and enforce
  16 KiB data/256 B added mutable-state budgets. Hardware acceptance is separate.

## 2026-10-29 — Bound firmware build storage

- Add shared native-build retention: latest/previous per exact device/profile,
  installed/recovery pins, disposable compiler cleanup, and audited retirement
  without rewriting historical receipts or deleting experiment logs.
- Add offline storage inspection, stale-plan and concurrency guards, a 2 GiB
  free-space preflight, and synthetic destructive-operation regression tests.
  Firmware behavior, hardware authority and enrolled-image privacy are unchanged.

## 2026-10-29 — Integrate current T096 development into main

- Merge T096 source 0.15.205 and its GM7 development history, including controller
  lifecycle fixes, HOT retention, bounded diagnostics and production-body tests.
- Include the breaking coarse TZ wire, manual CDT, three-advertising-period
  leases and local digital/analog clock display with UTC fallback. Preserve UTC
  transport and precision layouts; automatic TZ/DST and other target ports remain
  deferred.
- Retain the latest bounded test records and failed/incomplete certification
  gates. Update current build and compatibility guidance without promoting
  historical contracts or hardware qualification.

## 2026-10-29 — T096 manual CDT wire certification plan

- Select a breaking T096-first coarse-wire introduction, with manual Chicago
  ID 2 / -300 minute CDT and no runtime old-format fallback.
- Document offline contract/codec gates, exact-device preparation and scoped
  TC-01/TC-02/TC-04 direct, relay, GM replacement, reboot and renewal tests.
  Gate other target ports on the scoped T096 GM/RC results.
- Set the coarse TZ lease to three advertising periods, independent of precision
  timing. Existing authenticated parent advertisements renew the fixed mesh TZ;
  precision layouts, maps and clock algorithms stay unchanged. Candidate
  verification is underway; installation and certification remain separate.

## 2026-10-27 — time zone protocol and regional database plans

- Record the proposed four-byte coarse UTC extension with stable zone IDs,
  signed current offsets, manual GM configuration, authenticated RC forwarding,
  a coarse lease and an explicit compatibility revision.
- Propose a compact GM database with shared offset calendars, a coarse regional
  map, manual overrides and bounded rough GNSS selection. Document coverage,
  ambiguity, source licensing, storage targets and offline validation gates.
  These documentation changes implement no firmware behavior.

## 2026-10-27 — SS3 0.1.6 signed mesh epoch correction

- Port the current CoreSE 0.1.26 receive-only GM7 implementation3 codec/monitor,
  with selected-file provenance, SID14 dispatch and 64-bit replay fences.
- Correct Epoch to show the signed UTC mesh birth date/time and elapsed since
  that birth across leader changes. The previous 0.1.5 local first-seen timer
  was the wrong interpretation. Passive TD uses an explicitly labeled signed
  ANN UTC estimate; original report freshness and local uptime remain separate.
- Receive GM7 GNSS metadata alongside legacy GD1/GD2, and add actual-wire/native
  epoch regressions. Fence cached elapsed displays against the current admitted
  timing selection during handover and label local uptime explicitly as SS3 UP.
  Radio survey ownership, timing permissions and precision models remain unchanged.

## 2026-10-27 — SS3 0.1.5 current diagnostics and epoch display

- Receive authenticated GS mesh status and GD1/GD2 reference reports using the
  current CoreSE/T096 codecs, with explicit source provenance, original expiry,
  canonical advertiser/scope binding and replay fences across root replacements.
- Add Epoch and Reference pages to the eight-page StickS3 diagnostics cycle.
  Distinguish local epoch seen age from an unavailable epoch start, and reported
  GNSS/PTP quality from SS3 timing/output qualification. Expose receiver counters
  and original report deadlines in the existing CRC-protected identity row.
- Preserve the passive survey schedule, timing permissions and LCD ownership
  guards. Extend native signed-wire and full/strip renderer regressions.

## 2026-10-26 — N30/W12 successor recovery and latest research drafts

- Publish N30 0.2.34 and W12 0.2.17 source, build/inspection tools, notices and
  offline regressions. Both consume authenticated GM successor announcements;
  N30 additionally preserves a recently trained local actuator through direct
  GM handoff while requiring fresh source permission, timing and lock quality.
- Add the nine-page BLE timing-tail/multipath paper and indoor PNT paper for
  public draft review alongside the existing PToB-IPS study. Preserve prepared
  manuscript/PDF bytes and receipts, with separate publication records and
  reproducible analytical/evidence checks. No new hardware accuracy is qualified.

## CoreS3-SE 0.1.10 — precision diagnostics instrument (2026-10-23)

Add the M5Stack CoreS3-SE client with an instrument display, UTC register and
five diagnostic pages for mesh roles, source lineage, local timing quality,
GM reference and wire integrity. Reuse the pinned receiver with an 80 MHz fine
counter; keep internal causal RMS separate from calibrated UTC accuracy.
Show real GM HDOP, larger satellites-used and FIX/PPS/QUAL indicators, with
unknown PTP variance displayed as `VAR NONE`. Retain scoped report history
without renewing its original validity. Protected passive listening improves
diagnostic reception; clock qualification and failover rules remain unchanged.
Add the target/build guides, selected on-device screenshots and public source
notices. Offline tests, native build, app readback and a finite authenticated
GD2 reception check passed; output accuracy remains unqualified.

## T096 0.15.165–0.15.168 — inherited mesh epoch and GM diagnostics (2026-10-23)

Joining GMs inherit the mesh source epoch and retain it through successive GM
replacements and source withdrawal. A standalone GM forming a new cold mesh
generates a fresh epoch; no lab epoch is compiled in or persisted as a new mesh's
identity. Keep physical source generations, clock qualification and failover
gates unchanged.

Add authenticated occasional GNSS/PTP diagnostics within the existing
nonprecision advertising schedule. The 90-byte GD2 report includes actual GGA
HDOP and preserves explicit unknown quality codes; legacy GD1 decoding remains
supported. Independent report sequences and original expiry prevent cache
refresh by replay. Document the [versioned V14 diagnostics extension](docs/V14_GM_DIAGNOSTICS_V2.md)
after field reception checks. Host tests, successive-replacement regressions
and all six native profiles passed. All four attached GMs received verified
app-only updates; TC-03's LoRa TX advanced with the inherited mesh epoch.
These observations do not establish uninterrupted handover or calibrated UTC.

## N30 0.2.27 — concurrent splash and immediate GM acquisition

Start BLE before OLED initialization and show a display-only PToB splash for
up to 1.5 seconds, ending early on current settled BLE lock below 1 µs internal
causal RMS. Fresh eligible GM discovery bypasses N30 collection waits and uses
the existing scanner cancellation and authenticated admission path. Preserve
relay observation, W12 policy, source expiry and failed-parent retry fences.
Add splash boundary/renderer and actual native scan-to-JOIN regressions.

## N30 0.2.24 — restore independent LoRa capture mapping

Keep the LoRa DIO1 capture map on its original20MHz XTAL GPTimer when BLE
selects the fine counter. This avoids receiver/fence work stretching LoRa's
20us mapping brackets and rejecting otherwise received packets. BLE timing,
calibration, LoRa modem settings and transmitter authentication remain unchanged.
Add slow-fine-reader reception and actual exported-counter regressions.

## N30 0.2.23 — current-parent RF signal bars

Replace clock-page relay-delivery bars with fresh authenticated BLE-parent RSSI
for direct GM, RC1 and RC2 reception. Keep RF strength visible during live-RF
HOLD, clear it on stale or changed scope, and keep timing/lock authority separate.
Retain the 2 µs fine-slew threshold and original500ms saver failure exit. Add
capture-only RSSI/bar observations for device checks; host checks cover signal
scope, expiry, smoothing, thresholds and actual rendered pixels.

## N30 0.2.22 — fine slew with original saver failure exit

Retain the N30-only fine phase slew below2000ns and restore the original500ms
screensaver exit on continuous total timing loss. Withdraw the uninstalled
0.2.21 persistent-saver candidate. Test sub100ms dips on every worker turn and
retain the exact failure boundary. Optional ALL console capture records bounded
surface transitions and successful OLED write spans to investigate the reported
brief Precision-header flash; its hardware cause and repair remain pending.
Read a copied display model's observation time after its snapshot is copied.
No source expiry, lock qualification or RF/UTC acceptance gate changes.

## 2026-10-22 — N30 measured precision follow-up

Replace the S3 timing draft with an eight-page results-first edition and a
sanitized, reproducible supplement. Retain all 660 nominal opportunities in
the completed N3-2 run (657 eligible, 166.590 ns RMS), its tails and the separate
N3-1 fixed-correction transfer (48 eligible of 64, 150.954 ns RMS). The downstream
sub-100 ns target remains unmet. Preserve the earlier edition and failures.
Link the new edition from the research hub, shorten the N30 README and show
the recorded 49 ns internal causal RMS in its renderer preview, with the metric
and illustrative fields identified. Jonathan Yantis approved committing and
pushing the follow-up and firmware to GitHub; the paper remains a draft.

## N30 0.2.20 — refined precision watch face

Keep the Casio-style segmented display and move PRECISION to page three, with
LoRa last. Show the actual root alias and BLE/RC hop beside the PPS indicator,
then the selected counter and tick size: 80 MHz / 12.5 ns or 20 MHz / 50 ns.
Remove the extra lock-estimate line and retain the settled-lock stopwatch.
Replace oscillator trim with a signed, scoped trend of frozen causal prediction
errors over at least eight seconds. This read-only REL estimate works with
finite diagnostic collection disabled and cannot steer the clock or alter RMS.
Counter resolution, causal RMS and independently observed PPS error remain
separate quantities; this interface revision makes no new accuracy claim.

## N30 0.2.19 — empirical fine-receiver candidate

Add an explicitly configured continuous MCPWM1 clock backend with bounded
callback history, unique fixed-class falling-edge association and conservative
late/pending-event withdrawal. Use one clock domain for RX, CURRENT, available
and completed maps, estimated TX brackets, LoRa target mapping and both displays.
Keep the existing causal G1 estimator, permission/expiry and settled-lock gates.
Bind the empirical phase convention and separate calibration provenance into
the build identity; refuse automatic legacy-offset transfer. Default builds
retain the legacy backend. Delivered fine accuracy and absolute phase require
on-device verification; the signal is not identified as a qualified RF END.

Production fine images omit the mutually exclusive finite packet-window edge
and fence storage, leaving more internal heap for the BLE controller. Keep the
complete finite observer in legacy builds, preserve production capture history
and reject observer starts before they can disturb the continuous owner.
During shutdown, retain the production fine clock until every consumer and the
periodic RX hook have closed, then release it within the original update drain.

## 2026-10-22 — ESP32-S3 public draft review

- Record approval to publish the reviewed ESP32-S3 paper and selected sanitized
  evidence as supporting research for the robotics study. Link both papers,
  their sources and immutable editions from the public research collection.
  Preserve draft status and the original incomplete experiment outcome.
  Unfinished N30 firmware development remains outside this publication.

## 2026-10-22 — robotic gPTP/TSN public draft review

- Record approval to publish the reviewed robotics research draft in the
  repository's paper collection, with public PDF, source, edition and verifier
  links. Preserve the reviewed artifacts, evidence limits and draft status.

## N30 0.2.18 — continuous-counter validation journal

Retain128 packet intervals in one continuous MCPWM domain, with persistent
late-invalidation fences and paged first-callback evidence. Default OFF; the
delivered clock, calibration and precision qualification remain unchanged.

## N30 0.2.17 — bounded packet-window journal

Add a default-OFF full-AD/callback journal on one continuous MCPWM1 counter,
with fixed eight-window selection, exact stamp/anchor/Pair joins, non-consuming
CRC cursors, capture fences and retained late invalidation. Preserve the delivered
clock, both quality displays, legacy offset and LoRa owner. Original descriptor
scalars reuse N30-only padding without new reads or Report growth. Packet/RF
association and finer-clock accuracy remain unqualified pending device evidence.

## 2026-10-22 — robotic gPTP/TSN research draft

- Add a [robotics timing adaptation study](papers/ptob-gptp-tsn-robotics-preprint.pdf)
  with matching LaTeX, edition receipt and offline analytical verifier, following
  the Bluetooth timecode paper's architecture and validation style.
- Propose extending a wired gPTP authority to wireless acquisition timestamps,
  with explicit clock-transfer, ROS 2, uncertainty, expiry and TSN boundaries.
  Historical evidence stays separate; interoperability, conformance and physical
  robot-output qualification remain future work. No hardware operation,
  firmware change, publication or draft promotion.

## 2026-10-22 — ESP32-S3 precision timing research draft

- Add an eight-page draft with matching LaTeX/PDF and a selected, sanitized
  evidence supplement for causal S3 timing, aggregate relay calibration and
  a prospective 660-second relative-PPS attempt. Retain its incomplete ending,
  invalid reference record, nominal missing opportunities and earlier failures.
- Compare with a separate historical Nordic population and distinguish
  production controller timestamps from the default-OFF 80 MHz observer.
  Packet endpoint, path-delay and independent output qualification remain open.
- Include an offline statistical verifier, exact edition/source/PDF identities
  and provenance. No submission, push or promotion of the research draft.

## N30 0.2.16 — precision watch face

- Add a fourth portrait PRECISION page with segmented causal SYNC RMS, signed
  active-clock ppb correction, cached 80 MHz observer state and continuous
  settled-lock duration. Preserve existing monochrome typography and controls.
- Reset lock duration on loss, expiry, publication gaps or timing-scope changes;
  show stale metrics as unavailable. Keep downstream PPS quality separate from
  the clock's own RMS and keep the raw observer disabled by default.
- Extend bound page commands and synthetic previews, with sanitizer checks for
  validity, controls and rendering. No change to timing authority or RF endpoints.

## 2026-10-22 — Bluetooth timecode public draft review

- Record approval to publish the reviewed Bluetooth timecode research draft on
  GitHub alongside the other papers, and link its PDF, source and edition receipt
  from the root research section. Preserve the reviewed artifacts and draft status.

## 2026-10-22 — Bluetooth timecode research draft

- Added a [shared time base and Bluetooth timecode study](papers/ptob-bluetooth-timecode-preprint.pdf),
  editable LaTeX source, dated edition manifest and offline analytical verifier.
  It proposes compatibility layers and an independent validation programme;
  timecode interoperability, physical output and TAI firmware support remain
  future work. Historical evidence retains its separate populations and limits.
  No new hardware test, vendor integration, submission or draft promotion.

## N30 0.2.15 — targeted TX diagnostics with record CRC

- Reject ISR logging in the small public wrapper before entering the noinline
  task formatter; test all public entries without touching format data.

- Add the N30-only finite `capture tx <ms>` observer. It keeps only complete
  formatted TX estimate/path records, binds each header and payload to a CRC,
  and reports intentionally filtered records separately from transport losses.
- Preserve existing ALL/default console bytes, fixed queues, original deadlines,
  W12/T096 behavior, production clock selection and timestamp endpoints. Keep
  the RW observer disabled by default.
- Verify actual producer/worker expiry, lease changes, record corruption and the
  original fixed frame buffers, including the TX-only END filtered counter.

## N30 0.2.14 — fixed RW marker observer profile

- Add an explicit reviewed GPIO4/5 profile for native RW_RX_ON238 and
  RW_TX_ON237, with distinct immutable provenance and retained raw contracts.
  Preserve the original pair and synthetic chain diagnostic; no arbitrary
  signal routing or baseband configuration writes are introduced.
- Keep RF endpoint, packet association, timing improvement and UTC unqualified.

## N30 0.2.13 — finite synthetic capture-chain diagnostic

- Add a default-OFF fixed GPIO4/5 pulse mode to isolate the raw group1 capture
  path from undocumented native marker behavior. Preserve immutable synthetic
  provenance, bounded issued/captured counts, actual level readback and quiet
  time across finite stop/expiry cleanup.
- Keep BLE/LoRa production timing, correction scopes, W12 and T096 unchanged.
  Synthetic pulses and native activity observations do not certify RF timing.


## 2026-10-22 — N30 0.2.12 native activity trial guards

- Bound native GPIO4/5 callback storms and expired leases in the ISR, including
  callbacks during channel enable; preserve MCPWM group0 and CURRENT channel2.
- Attempt both pad releases after partial failures and keep maintenance closed
  until all hardware owners acknowledge cleanup. Export actual matrix/OE,
  channel/IRQ masks and callback refusal counters in bounded CRC diagnostics.
- Compile and exercise the actual optional native profile in offline ASan/UBSan
  tests. Signal activity, RF endpoint, coalescence, packet association and UTC
  accuracy remain separate hardware questions; the observer never trains the
  production clock or changes the configured legacy aggregate correction.

## 2026-10-22 — N30 0.2.11 raw native marker observer

- Add an identity-bound, default-OFF MCPWM group1 raw observer, with no-pad
  CURRENT-only default and separately reviewed fixed GPIO4/5 native marker
  profile. Keep LoRa group0 and production clock/legacy offset behavior.
- Bound synchronous CH2 latch polling, unwrap/lifecycle health, finite CRC
  history and cleanup/update drain. Retain unknown RF endpoints, coalescing,
  packet association and UTC/output bounds; do not promote marker authority.
- Add actual-owner synthetic failure/maintenance/formatter checks and sanitizer
  counter lifecycle tests. Hardware acceptance and endpoint calibration remain
  separate from these offline checks.


## N30 0.2.10 — provisional downstream composite offset

- Accept the evidence-bound W12 schema1 aggregate-path profile in an ignored
  N30 device input, requiring estimated relay and BLE100. Reuse its validator
  and generated firmware binding; inspect the complete profile and provenance.
- Apply the existing publisher-wide canonical RC7 timestamp correction for all
  downstream receivers, preserving total-from-programmed-endpoint semantics,
  exact scope, once-only nominal ADDRESS conversion, expiry and unknown bounds.
  The received clock and shared W12/T096 firmware remain unchanged.
- Add actual generated-config/codec/HMAC/receiver-map checks for two synthetic
  receivers, default and wrong-scope behavior, plus N30 input/mode guards.

## N30 0.2.9 — reusable timing diagnostics and conditional shadow analysis

- Add default-OFF runtime timing recording with a finite ten-minute maximum,
  selectable sampling, fixed16-record RAM history and non-consuming CRC reads.
  Retain raw mapping brackets, descriptor fields and authenticated causal pairs;
  preserve missing populations, source expiry, clock fitting and radio policy.
- Add an offline interval-fit shadow with explicit quantizer/drift assumptions,
  causal availability, inconsistent/stale refusal and unknown RF/UTC bounds.
  Preserve displayed RMS and every production timing gate.
- Audit pinned S3 receive arithmetic and hardware-capture leads. Diagnostic
  GPIO names and software timestamp-sync toggles do not establish an RF latch.
- Test actual recorder/adapter/command/parser behavior and exact observer
  ON/OFF clock/quality/permission equivalence. Physical precision remains a
  separately scoped measurement outcome.

## N30 0.2.8 — intermittent GM fallback and faster physical acquisition

- Accumulate BLE lock loss so brief good intervals cannot indefinitely suppress
  relay search or LoRa AUTO. Keep discovery open after a failed trial, preserve
  bounded GM retries, and drive radio fallback independently of the OLED.
- Retain N30 delayed physical corrections with capture-time causal predictions,
  dense64-point fitting and a one-second acceleration baseline. Preserve the
  existing1µs gates, causal tails, source expiry and default W12 behavior.
- Add negative controls, actual native relay cases, changing-frequency clock
  tests and real RC8/receive-owner integration with synthetic peripherals.
  Physical field acquisition and accuracy remain unqualified.

## T096 0.15.164 — G5 GM checkpoint and LoRa startup guard (2026-10-22)

- Record **0.15.164 / G5 control protocol 5** as the accepted GM development
  checkpoint, retaining RC7 timing and LoRa wire 3. Add a short
  [GM operating guide](docs/t096/gm-operations.md), linked from both READMEs,
  for staged three/four-GM setup, receivers/relays and RC/DC macOS USB paths.
- Retain the separate completed 1,800-second observation and
  [later HOT withdrawal](docs/t096/gm-standby.md#source164-later-hot-withdrawal)
  despite steady local PPS. SERVICE expiry is an inference with unresolved
  initiating delivery/renewal failure; durable failover, same-image two-sided
  pair observation, fourth-GM acceptance and independent UTC accuracy remain open.
  This documentation checkpoint changes no firmware behavior, timers or wire version.
- Publish one atomic readiness word outside reused startup scan scratch before
  the LoRa worker starts. The independent observer reads no union state before
  that publication. Preserve the30s bound and sticky hardware faults; no timer,
  retry, controller, pairing or RF changes. Native RAM delta is measured separately.
- Add a composed actual scanner/owner/service regression with a synthetic valid
  journal record, deterministic observer cuts, original negative, initialization
  ordering and post-initialization watchdog controls.
- Preserve163's two startup failures and the limits of its completed600s field
  observation. Neither host reproduction nor that run qualifies controller/HOT
  durability or independently measured timing accuracy.

## T096 0.15.163 — pinned radio fixes and bounded fatal context (2026-10-22)

- Backport Zephyr355648a timer-route cleanup before radio DISABLE and ZMK10ba6d0
  same-owner zero-exchange cancellation through hash-pinned generated copies.
  Preserve forced guards, asynchronous ownership, assertions and queue capacity;
  keep the SDK checkout unchanged and exclude DC/PC/BabbleSim.
- Add actual-body old/new HAL, preempt, connection ISR cleanup and real FIFO
  controls with explicit hardware/IRQ/LLCP boundaries. Physical causation and
  durability remain field gates.
- Reuse four unused failure-only prepare words for current LLL/preempt state
  under the existing complete queue-copy mask. Preserve all queue entries,
  source/boot/CRC and assertions; no healthy diagnostic hook or additional
  controller policy change.
- Retain pending health detail across console admission refusal, using existing
  throttle/watchdog alternation and normal60s cadence after acceptance.
- Add actual-body source/configuration/decoder/delivery regressions and record
  source162's failed field runs separately from its two validated protocol fixes.
  Controller durability and initiating failure cause remain unresolved.

## T096 0.15.162 — current-session source refresh (2026-10-22)

- A rejected wide HELLO echo can refresh authenticated source health through
  the existing live clock map, with its original age/drift and record/source
  expiry. It cannot renew map evidence or successor grants. No timer, election,
  controller, radio or display policy changes.
- Retire the former ACTIVE's receiver generation floors on role exit, keeping
  its own issuance counter and outstanding grant deadlines. This prevents an
  old issuer counter from blocking a different GM's lower generation.
- Add the actual authenticated-wire regression and boot/nonce/map-age/expiry
  negative cases to the ordinary G5 runner. Native and field results remain
  separate; the earlier silent HOT peer failure is still unclassified.

Copyright (c) 2026 Jonathan Yantis. MIT.

## 22 October 2026 — N30 0.2.7 returning GM and five-minute no-time recovery

Fix the unbound failed-parent cache dead end after a GM loses and regains its
source in the same boot. A fresh locator for the old trial can now use its
bounded60s retry path when no eligible alternative exists; a new authenticated
exchange replaces authority. The remembered trial no longer blocks that sole
returning source forever. W12 defaults and T096 firmware behavior stay unchanged.

Add a separate N30 supervisor timer that reboots after a full five continuous
minutes without current BLE FOLLOW UTC or available LoRa time. Either source
cancels the interval; HOLD, traffic and stale data cannot postpone it. OFF and
update maintenance inhibit it. Existing120s fault supervision and RTC restart
records remain. Production-native source-return and actual supervisor/reset
boundary regressions use synthetic hardware; field recovery remains unqualified.

## 22 October 2026 — N30 0.2.6 field parent retry recovery

Bound N30 admission retries to a failed BLE parent after its existing60s lock
trial. Repeated direct-GM retries previously kept the shared20s/60s relay rate
gate closed indefinitely; a production-native synthetic regression reproduces
seven GM attempts and zero relay attempts, then passes with the fix. Reserve60s
for alternatives and allow at most one failed-parent retry per minute afterward.
Keep actual source expiry, live reception, manual selection, LoRa AUTO timing and
valid LoRa display preference. W12 defaults and T096 firmware behavior stay unchanged.

Sanitized offline checks cover retry boundaries, renewal/rebind, recovery and
manual choices, plus the actual N30 radio owner with authenticated RC8 pairs,
late AUTO startup, six minutes of capture wraps, CRC loss and reacquisition.
No field trace established why LoRa lacked time during the reported incident;
physical handover and LoRa reception still require a separate hardware check.

## 22 October 2026 — N30 0.2.5 continuous saver session

Keep the normal and minimal OLED savers in one session across BLE/LoRa switches,
precision-lock dips with usable time, and display-cache resets. After15minutes
in that session, keep the minimal layout until user wake/navigation or500ms of
continuous loss of both timing inputs. Local HOLD/stale data cannot renew it;
PPS permission and radio timing/source validity are unchanged. Production-pipeline
regressions cover exact loss/recovery/15minute boundaries and all saver contexts.

## 21 October 2026 — N30 main-screen guide and dual timing inputs

Lead the N30 target guide with the clock, dual BLE/LoRa and LoRa receiver
screens. Explain independent timing inputs, receive-only LoRa display fallback,
BLE lock preference and AUTO dwell, with controls and build links. Regenerate
only the three selected main-screen previews with recorded fixture/source hashes;
values remain illustrative. Add N30 to the project display gallery. Firmware and
hardware acceptance are unchanged.

## 21 October 2026 — N30 0.2.4 OLED saver power reduction

Set both screensaver modes to half normal OLED contrast. Keep the normal4Hz
saver, then show only its moving source header and250ms PPS flash after15minutes
of saver activity. Minimal mode renders only at pulse edges, blocks idle button
polling and pauses unused legacy indicator work. BLE/LoRa timing stays active;
the live XTAL timer retains its chip light-sleep guard. Host tests cover exact
boundaries, wake/loss handling, all positions and unchanged normal cadence.
Power draw and sustained hardware behavior remain separate observations.

## 21 October 2026 — T096 0.15.161 precision duration restoration

Restore the PPS duration for all GMs and HOT duration for the standby on the
precision screen. The160 layout inadvertently replaced those duration labels.
Restore the serving GM Bluetooth beat icon beside RMS. Reuse the existing hold
timestamps; COLD timing reception, ACTIVE duration, RMS,
GM precision shadow OFF and RC precision shadow ON remain. Restore dedicated
shadow-page sampling for GM and RC under its original source/serving-GM guards.
No protocol timer changes.

All four final images passed native/resource and actual startup checks. The
source160 protocol baseline passed its full suite;161 passed43 focused UI worker
cases and renderer/duration/icon checks under sanitizers. The initial161 field
check was interrupted by USB loss after four complete cohorts. Dedicated-page
hardware sampling remains pending; see the [checkpoint](docs/t096/gm-standby.md).

## 21 October 2026 — T096 0.15.160 COLD timing reception

All COLD GMs now use the existing passive precision receiver path previously
reserved for the configured LoRa transmitter. Receiving timing does not appoint
HOT or add a GM control slot. Precision pages show stream RMS independently of
shadow PPS; COLD receivers report standby rather than failed HOT admission.
GM shadow sampling is disabled, while RC precision/shadow pages retain it.
Timers, election, controller policy and source expiry remain unchanged. Physical
acceptance and the existing prepare-FIFO fault remain separate from host tests.


## 21 October 2026 — T096 0.15.159 standby input and HOT page

Refresh coherent direct HOT/ACQUIRING precision input before readiness consumes it,
retaining its original observation time and expiry. Make the ordinary HOT page
observational; only dedicated shadow page6 requests sampling. Election, takeover,
timers and RF policy remain unchanged. The stream-driven handoff draft stays
for evaluation only.

All four native images, profile composition, focused sanitizer checks and the
full host suite pass. Four-device field observation formed an acknowledged pair
and restored LoRa forwarding, but TC-04 then suffered the retained prepare-FIFO
fatal and watchdog recovery. After recovery, the HOT-page check kept sampling OFF
and the pair was acknowledged again. Controller failure and HOT durability remain
unresolved; see the [field checkpoint](docs/t096/gm-standby.md).

## 21 October 2026 — T096 0.15.157 ticker progress

Order force before age within the existing ticker class to prevent reciprocal
ACL deferral. Actual queued-controller regressions and native checks pass;
subsequent physical prepare-FIFO failures remain separate and unresolved.

## 21 October 2026 — T096 0.15.156 prepare failure capture

Normal native GM/RC images retain the failed prepare callback and a bounded,
read-only snapshot of the seven controller queue owners, using existing scratch
and fatal-capsule storage. Healthy prepare paths add no diagnostic counter calls
or periodic streams. Startup and invalid queue cuts report UNKNOWN; fatal capture
uses no application spinlock. Timers, pairing, RF policy, queue capacity and
assertions remain frozen. Focused sanitizer checks and independent review pass;
full native and field checks remain separate. This diagnostic does not resolve
the observed queue assertion or establish durable HOT operation.

## 21 October 2026 — T096 0.15.155 controller failure diagnostics

Preserve the original peripheral latency-cancel assertion status in the existing
fatal capsule, and extend the first-disconnect snapshot with bounded current
connection/ticker counters. A separate pinned patch changes generated controller
copies only; the upstream backport, scheduler policy, assertions, timers, pairing
and RF behavior remain unchanged. Focused sanitizer checks and independent review
pass. Full native resource validation and field observation remain separate gates;
this diagnostic candidate does not establish a fix or durable HOT operation.

## 21 October 2026 — T096 0.15.154 controller retirement snapshot

Extend the existing first-disconnect fault record with a bounded current
controller/LLCP snapshot. Native configuration assertions and one IRQ fence
protect the scalar copy; journal writes remain outside it. No timer, pairing,
radio, reference or SDK behavior changes. Four native builds, the host suite
and all four actual startup/runtime checks pass. Static storage is unchanged;
the additional evidence does not establish the initiating cause or durable HOT.

## 21 October 2026 — T096 0.15.153 disconnect failure isolation

Add a fault-only host state and work snapshot at the first unretired-disconnect
error. Timer, pairing and radio behavior remain frozen. All four native images
and the host suite pass; actual startup checks confirm installation. Two GM
failures remained DISCONNECTING at the original negotiated deadline and recovered
through the watchdog. Source-qualified HOT still drops and reforms. Document
the separate controller assertion, source-independent withdrawal observations
and controller/workload modeling gaps without treating conditional reproductions
as field root causes or accepting durable HOT.

## 21 October 2026 — T096 0.15.152 retained failure classification

The existing CONTROL watchdog capsule now records which accepted failure
predicate fired and its service errno. Reset behavior, timers, pairing, transport
and radio policy remain unchanged. Four ordinary images passed native builds and
startup checks. Subsequent retained capsules identify two terminal service-error
watchdog recoveries; an incoming connection remained unretired in the first live
capture. The controller/host cause and durable HOT pairing remain unresolved.
Further firmware and timer changes are frozen during fault isolation.

## 21 October 2026 — T096 0.15.151 shared control work budgets

G5 shares one 1 s initiation/GATT work budget per owner pass and the existing 5 s
scan-stop retirement budget. Authenticated STATUS and non-HELLO control use their
original session or signed expiry instead of a redundant 500 ms local queue gate.
Management 45 s, grants 90 s, source/PT validity and actual reference retirement stay
separate. Production-owner tests add delayed callbacks, aggregate GATT waiting,
partial restarts with retained peer state and COLD incumbent renewal. Ordinary
images omit unsolicited timing/controller diagnostics while keeping fault records.
The timer audit also identifies RC PA cleanup's 500 ms/2 s ownership margin as a
follow-up requiring actual-owner responsiveness tests; physical HOT durability
and controller-failure causality remain unaccepted.

## 21 October 2026 — T0960.15.147 fair G5 owner servicing

G5 uses one four-class round robin and shared500ms HELLO/SERVICE/management
probes, with1s original records; legacy records retain250ms. Original response
clipping, source/PT validity,15s grants and replay fences remain. No queues or
stored fields are added. Actual same-owner RX2/TX delayed-cycle tests reproduce
146 HOT loss and cover100–250ms and explicit burst schedules. Version146 physical
HOT withdrawal and a separate worker/terminal-service watchdog recovery remain failures,
not147 acceptance. Native controller/RF scheduling and durable physical HOT
still require observation.

## 21 October 2026 — T0960.15.146 G5 coordination margin

G5 management, READY and authenticated peer/root liveness share a5s lease,
with a finite15s grant derived as three coordination leases. Grant issuance
remains every2s; each
original grant, source permission, packet and precision map keeps its deadline.
Legacy coordination remains2s. This removes the zero-margin coupling between
2s grant renewal and2s acknowledgement expiry, without adding state or queues.
Actual production-wire regressions include selective renewal loss with varying
phase, longer outages and unchanged source/PT/replay refusals. The opt-in diagnostic image also records missed/consumed inventory opportunity
gates without changing CONTACT behavior. Stable physical HOT and CONTACT retry
reliability remain subject to hardware observation.

## 21 October 2026 — T0960.15.145 negotiated connection cleanup

Replace the implementation-chosen2s ACL retirement limit with actual negotiated
LE supervision plus5s callback allowance. Connecting cancellation and natural
disconnect retirement use5s; normal GM/RC outer stop/profile and maintenance
share a40s total budget. Existing source, message and output authority still expire
on their original deadlines. A real latched GNSS driver fault in idle normal GM
requests existing watchdog recovery; ordinary missing fixes do not. No persistent
state, queue or timer is added. Production-body host regressions cover late legal
callbacks and terminal recovery. Physical HOT reliability remains unqualified.

## 21 October 2026 — T0960.15.144 bounded connection retirement

Normal control closure now issues one public disconnect request, records its
result and keeps an original2s retirement deadline through actual disconnect, TX
and GATT callbacks. Missing retirement becomes terminal for the existing watchdog
without releasing unsafe storage. READY freshness shares the existing2s management
lease; source/PT validity,250ms message expiry and10s grants stay unchanged.
An off-by-default GM/RC controller diagnostic canary freezes the seven-entry
prepare FIFO into a separately identified retained fault schema without growing
queues or static scratch. Host tests cover ownership interleavings, selective
READY loss and diagnostic retention. Physical HOT durability and the controller
assertion root cause remain unqualified.

## 20 October 2026 — T0960.15.143 per-timestamp clock bounds

Authenticated control now maps each timestamp with its own conservative drift
distance. A future finite grant keeps its complete guard without unnecessarily
shortening a separate message expiry and its clipped response. Original TTLs,
source/grant limits, cadences, response clipping and repair dwell remain unchanged;
no stored state, timer, queue or buffer is added. Actual-production host checks
cover a fixed delivery burst, expiry/source boundaries, grant/replay fences and
clock-rate corners. This isolates a software failure; native RF durability and
the separate controller preparation assertion remain unqualified.
An additional native143 observation reproduced that controller assertion and
captured the subsequent automatic watchdog restart. The controller trigger and
a general reset-latency bound remain unresolved.

## 20 October 2026 — T0960.15.142 same-epoch candidate requalification

A newer non-ACTIVE STATUS may restore descriptive candidate eligibility after
physical qualification recovers in the same source epoch. Previously an earlier
ineligible report could block that recovery until a HELLO requiring appointment
had already arrived. The original withdrawal floor remains: old/equal reports
and same-epoch ACTIVE claims stay rejected; actual causal HELLO still gates its
retirement. Inventory can nominate, but supplies no map, HOT, takeover or grant.
No timer, state, slot, reserve or wire change. Production-code regression covers
source dwell, causal JOIN/STATUS/HELLO and retained replay/authority fences.
The separately observed native controller preparation assertion remains open;
watchdog restart was observed, but its cause and reset latency are unresolved.

## 20 October 2026 — T0960.15.141 unified G5 ownership and dispatch

Discovery windows gate new work; accepted G5 initiation retains its original100ms
cap and connected admission its original5s resource expiry. JOIN/STATUS clip to
that same expiry and current phase, with callback/TX/GATT ownership retained until
actual drain. One consumed create submission schedules the existing retry policy;
completion cooldown and selected-anchor shortening no longer compete with it.
Per-identity failure history, inventory fairness and ordinary RC policy remain.

Fair pair dispatch retains message cadences and original authority deadlines,
with an urgent expiring response preserving the other classes' turn. Existing
CONTACT30s, ACQUIRE45s and REPAIR10s share small lifecycle helpers; stable READY,
source qualification, finite grants, retirement, drains and watchdog retain their
bounds. No stored state, slot, buffer or reserve is added. Actual-production host
boundary tests cover callback races, original expiry, offered-capacity scheduling
and safe withdrawal; RF convergence and physical durability remain unqualified.

## 20 October 2026 — T096 development checkpoint and next test plan

Document remaining authenticated-control, physical HOT convergence/renewal and
watchdog reset-latency checks separately from passing host/native validation.
The next round establishes a three-GM baseline before a fourth RC joins and the
operator tries GM newcomer behavior. Clarify that `gm_radio` contains publisher
state rather than receive counters, and that the sole LoRa transmitter needs GM
runtime profile as well as an ON request and valid source export. Physical
multi-GM durability and independent output accuracy remain unqualified.

## 20 October 2026 — T0960.15.140 fixed successor acquisition budget

A new successor acquisition now has one fixed45s budget, including the existing
5s stable-READY dwell. Cold delivered-map convergence can consume most of the
previous30s budget before that dwell begins. Contact remains30s and repeated
exchanges cannot restart either original deadline. No phase, role, state or RAM
is added. Source, map, management, finite-grant, repair and output fences remain.
The tradeoff is15s longer before replacing an unsuccessful selected candidate;
this does not guarantee RF contact or repair unrelated HOT renewal loss.

## 20 October 2026 — T0960.15.139 GM owner watchdog coverage

The existing30s CONTROL-publication stall observer now covers GM and RC,
including pending profile transitions. Terminal service errors and unfinished
closure past the original deadline inhibit watchdog feeding without another drain
grace. The existing hardware watchdog supplies the eventual reset. Completed
closures, healthy COLD operation and missing GPS/RF retain their existing policy.
No state, timer, static RAM, source authority or protocol change is added.
Production regressions cover both profiles, late worker recovery, original drain
expiry and intentional maintenance. Physical reset latency remains unmeasured.

## 20 October 2026 — T0960.15.138 change-driven source HELLO

The existing change-driven HELLO scheduling now runs before the G5 return, so a
new source permit or epoch can request its existing causal exchange. Source, map,
grant and timeout bounds are unchanged. Existing management/pair deadlines, HOT
withdrawal and receive latency/backlog counters are exposed in `network.control`.
Production host checks preserve source/grant expiry and COLD fences; physical
multi-GM durability remains unqualified.

## 20 October 2026 — T0960.15.137 bounded contact and causal anchor handoff

An ACTIVE keeps one ranked nominee while making bounded contact. That private
phase has one original30s deadline, no successor authority and no management
readiness. Its first matching causal STATUS reply/final construction starts one
distinct original30s PA acquisition deadline; repeated/lost phases cannot renew
it. An unreachable better-ranked nominee still fails into the existing cooldown,
allowing another candidate. COLD keeps the same quiet windows and scan budget.
`gm00.attempt_phase` distinguishes CONTACT, ACQUIRE, REPAIR and IDLE. The total
attempt bound now includes contact plus acquisition; it is not the former30s total.

Admission retains the original causal JOIN anchor while COLD precision publication
remains suppressed. After real control promotion, PT can adopt that matching
selected remote root/boot anchor even when its first job missed the earlier passive
publication. Known source epoch/train must match; original replay/source expiry,
ACTIVE/mismatched-root fences, actual PA fit, source qualification and finite grants
remain necessary. Separate production executables test the actual producer mailbox
and delayed PT consumer, without manufacturing a map or HOT readiness. Failed
prior-code controls remain separate from current host/native proof. Hardware
contact, PA convergence and multi-GM durability still require independent testing.

## 20 October 2026 — T0960.15.136 consume PT withdrawal intent

G5 inventory JOIN now publishes passive timing mode from the current GM owner
policy immediately. A foreign root's ordinary JOIN ACCEPT previously could put
a healthy ACTIVE into the passive precision path until the next owner tick.

CONTROL now consumes PT's latched resource-close intent even when the same scope
recovers between cycles. It closes the original resources within the earliest
original deadline, rather than leaving an unconsumed2s latch to cause a later
terminal timeout. Repeated calls and quiet transitions cannot slide that deadline;
stuck ownership still faults, and real callback drain cannot clear the fault.
Late first consumption or HCI cleanup crossing expiry retain the timeout even
after synchronous resource release, using a fresh closure observation time.
Actual production admission and PA-owner regressions cover both cases. Legacy RC
admission, source/precision gates, authority, maintenance and resource limits remain.

## 20 October 2026 — T0960.15.135 G5 missed inventory retry

An ACTIVE without a selected peer now uses the same5s retry policy after a missed
inventory window as after a completed poll. The missed path previously treated
ACTIVE's own identity as a remote incumbent and waited30s, reducing opportunities
to discover qualified candidates or merge independently serving roots. Selected
transport repair also uses the existing common scheduling policy.

Production network/admission/transport regressions elapse repeated1s windows with
native create refusal or missing connection completion, retain the100ms attempt
cap,2s cooldown and delayed callback ownership, and require bounded next
opportunities while source service continues. No source, precision, authority,
output fence or resource gate changes. This corrects retry scheduling; the cause
of physical100ms connection failures and hardware merge durability remain open.

## 20 October 2026 — T0960.15.134 G5 bounded standby

Ordinary GM now separates three-identity candidate inventory from two control
slots (self and one appointed peer). A healthy ACTIVE/HOT pair retains its
appointment; other capable clocks stay COLD with continuous physical source
qualification and sparse causal inventory. The requested sole LoRa transmitter
has one ordinary BLE leaf exception while COLD. RC7 client timing, source epochs,
original output fences, negative LoRa root restriction and flash-only DC remain.

G5 explicitly changes the outer coordination envelope/domain while preserving
inner AUTH_CONFIG5 and JOINv4. Finite10s incarnation/source/pair-generation grants,
confirmed global HOT, bounded repair, cooldown history and conservative retirement
replace persistent all-member management. Negative context survives link cleanup
and defeats delayed positive inventory. Paged `gm00..04` owner snapshots expose
original deadlines, freshness/provenance, local/global HOT, output fence, failure
history and bounded transitions without extra flash writes. Production network,
precision, admission and transport fake-boundary tests join the aggregate host
suite. Native resource/controller gates stay unchanged. Hardware durability and
independently measured UTC/output accuracy remain unqualified.

## 20 October 2026 — T0960.15.133 preserve the selected HOT under LoRa

When current direct BLE admission matches the live LoRa root, retain that root's
ordinary selected standby. The final LoRa constraint previously re-ranked every
other capable clock after pair retention, so a better-ranked third newcomer could
clear an established HOT handshake. Production-policy regressions now cover both
three and four candidates and fail against the former behavior. Root inhibition,
direct admission, independent-source qualification and takeover fences remain.
Cold standby, physical multi-GM durability and output accuracy remain separate
work and qualification boundaries.

## 20 October 2026 — N30 offline RC target

Added Meshnology N30 versions0.1.0 (offline) and0.1.1 (commissioning candidate) using the existing W12 G1/RC7 core,
8MB flash configuration, bounded UART console and two portrait monochrome
pages adapted from T096. GPS is absent; only RC builds are accepted. W12 and
T096 firmware behavior is preserved. Production-renderer previews use synthetic
values. N3-1/N3-2 onboarding merges verified future factory identities into the
shared ignored map; no real identities or enrolled images are published. Default native
builds remain explicitly non-flashable. Clean-source commissioning candidates
require separate exact-device arrival/recovery/hardware review.

## 20 October 2026 — T0960.15.131 LoRa GM coordination

An authenticated accepted LoRa physical event pair now constrains GM election
to its actual root. Timing-only acquisition can inhibit a competing local GM
before its companion resolves the root boot. LoRa-only nodes withdraw local
source admission, HOT and prior takeover eligibility; local PPS, clock quality,
old reservations and unrelated BLE roots cannot override the constraint.

Current direct authenticated matching-root JOIN plus captured BLE time, or
current matching-root control admission with a causal map, permits ordinary source
readmission and HOT negotiation. Relayed root identities, cached membership,
RSSI and unknown boots cannot grant this eligibility. Direct loss withdraws it
while the original LoRa hold remains live. The canonical root is distinct from
TC01's immediate BLE parent; self-root descendants remain rejected.
After a completed origin handover, a strictly newer full self-root companion
may retire the old foreign constraint only for the separately proved current
serving incarnation/source epoch. It grants no time, admission or promotion;
cold, inactive, old-boot/epoch and replayed contexts cannot do this.
An explicitly withdrawing root may complete the normal coordinated HOT-to-ACTIVE
replacement while direct authenticated contact persists, after the old service
permit and existing takeover/output fences. Expiry or silence alone cannot.
Metadata alone cannot create/renew the constraint, and withdrawal/source changes require
new physical pairs. Expiry returns through ordinary admission/output fences.
The watchdog recognizes fresh LoRa-only GM coordination as an intentional
operating mode, separately from qualified time, so it does not spuriously
restart after20 minutes. Core/owner/transport faults retain their deadlines.

Wire3 and its HMAC domains remain unchanged. The bounded coordination hint is
separate from RC7 timing/USB authority and retains UNKNOWN passive freshness.
Complete RF partitions cannot guarantee global single-GM exclusion while also
permitting standalone startup. Host/native checks and actual-device/field
qualification remain separate.

## 20 October 2026 — T0960.15.130 one-shot toggle and fault retention

On the LoRa page the configured-role toggle fires once at1.5 seconds. The press
is consumed, so extending it cannot poweroff the device or repeat the action;
actual TX/RX ON/OFF text acknowledges the applied role. The footer states the
threshold. Intentional four-second poweroff remains on other pages, with
screensaver wake, page-change and profile-change cancellation retained.

The existing bank0 journal now carries its primary LoRa fault into every later
ordinary event record, with its original boot/time distinct from the new event.
Schema6 uses reserved words39/40 for that original boot; the16-word payload and
BLE pre/post context remain. The normal verified commit precedes obsolete-page
erase. Startup restores the newest valid copy; recoverable launch noise cannot
supersede an unacknowledged terminal cause. A newer terminal cause can replace
it through the same handoff. No extra write cadence, bank, reserve reduction or
protected-prefix exception is introduced. This corrects the prior rolling
journal's short retention after reset. New-source updates retain existing
source-bound journal initialization semantics.

## 20 October 2026 — T0960.15.129 launch and drain scheduling

An unretired SetTx command that returns after its20ms timing or30ms companion
launch margin now consumes its sequence and closes through bounded EINTR
recovery. Its complete chip timeout must fit the original360/450ms FEM window
before phase3 retirement. A latched physical DIO is still classified normally;
real retired-TX timeouts and DMA/SPI errors remain terminal. Late launches are
counted and journaled with stage8 and the original FEM-enable millisecond.

After RF inhibition, closure temporarily uses priority9 and wakes the sleeping
LoRa worker. It restores ordinary priority11 after all four closure acknowledgments;
BLE priorities and the original200ms drain deadline are unchanged. A missed drain
now retains ETIMEDOUT as its actual cause. These changes address scheduling
hazards; host regressions do not establish physical fault elimination.

## 20 October 2026 — T0960.15.128 stalled-start cancellation

The LoRa watchdog trip now advances the cancellation serial before inhibition.
A stalled start returning from capture/resource acquisition cannot pass its
original reset-release fence after the observer has faulted the owner. The
production regression reproduces127 returning a successful start after that
fault;128 returns cancellation with reset/FEM inhibited and no SPI submission.
This is a reviewed software race, separate from physical canary outcomes.

## 20 October 2026 — T0960.15.127 durable LoRa fault recovery

LoRa now retains the first fault's operation/stage, original observation times,
physical edge/generation, IRQ/BUSY/DIO state and DMA/capture ownership before
inhibiting RF. Main requests a CRC/commit journal record in the existing bank0;
BLE pre/post context and the original two-second writer deadline remain.
Recoverable launch records share the existing event budget. A terminal fault
gets one immediate record opportunity; saved, failed and unfinished writes stay
distinct in diagnostics.

Real transport faults keep their buffers and descriptors closed until the
existing hardware watchdog resets the MCU. Main requests that reset after the
record completes or its two-second bound; a source/silicon/boot-bound capsule
also retains the cause, closure bits and journal result. An independent observer
catches a frozen LoRa owner after30s without treating RF/source silence as a
fault. Ambiguous SetTx launches retain the bounded local retirement/cadence path.
The startup-only512B diagnostic scan scratch becomes the observer state only
after scanning finishes, retaining the4096B added-static ceiling and all original
reserves. Host/native checks do not establish physical reset retention, long-term
coexistence or output accuracy; those remain exact-device evidence gates.

## 20 October 2026 — T0960.15.126 bounded SetTx BUSY recovery

A BUSY-high deadline during an unfinished SetTx command now resets and retires
that ambiguous attempt through the existing EINTR recovery path. Admission is
limited to opcode0x83, phase4, a running owner, no fault and retired DMA; other
BUSY deadlines, transport errors, unretired DMA and completed-launch timeouts
remain faults. A concurrent cutoff/cancellation cannot double-count the abort.
The30ms BUSY wait and340/360ms timing or420/450ms companion windows are unchanged.
The service consumes the possibly transmitted sequence, closes fully and keeps
its4s timing/2s diagnostic retry bounds. This does not establish the cause of the
separate version125 observed timeout.

The console appends BUSY abort count and last timed-out opcode in two `hw`
fields. They occupy the hardware diagnostic structure's existing padding;
the52-byte structure, RAM ceiling, original reserves, page7 and RX fit are
preserved. Host/native checks remain separate from authorized hardware checks.

## 20 October 2026 — T0960.15.125 RC8 follow-up and link diagnostics

The timing service now populates its immutable context before validating a prior
physical TxDone correction. Version124 validated the unfinished packet, rejected
its zero context and discarded every prior correction; mapped TX events alone
did not establish receiver acquisition. A production-service regression requires
the second physical timing packet to carry the first event and the real receiver
policy to accept it. Wire3 bytes, HMAC domains and vectors are unchanged. The124
hardware population, its zero accepted samples and separate radio timeout remain
preserved failures; this source fix does not qualify RF coexistence or output.

An interrupted software launch retains the original hardware cutoff and requires
bounded reset/retirement before restart. Since RF may have reached a listener,
the proposed timing or companion sequence is consumed, prior correction is
cleared, pending diagnostic events remain and retries keep their cadence.
Retired-command timeouts and transport faults remain latched. A short final
driver fence closes the abort/command-retirement race without masking SPI or
BLE work. Companion age is sampled inside the snapshot lock after the latest
owner publication, preventing a preempting update from manufacturing UNKNOWN.

Page7 displays source/parent diagnostics and a locally measured RX link affine
residual RMS, separate from the companion's upstream BLE fit. The estimate uses
4..16 matched physical event pairs per block; rollover, context, capture epoch,
loss, gap and OFF reset it. COLLECTING and NO TIMING replace the generic
placeholder; a TX-only unit shows source validity. This is relative consistency,
not calibrated UTC/output accuracy. The56-byte fit stays in the RX/TX union and
the compact48-byte view preserves existing cache sizes and original reserves.

## 20 October 2026 — T0960.15.124 RC8 companion diagnostics

[RC8 wire3](protocol/rc8/README.md) retains80-byte timing with explicit full root
identity/source epoch and exact prior physical TxDone. Original validity offsets
round inward. New128-byte companions bind immutable boots/stream/parent context
separately from mutable descriptive quality; they run about120 seconds apart
plus coalesced significant events and yield to timing/BLE. Missing companions
permit an independent functional estimate with unknown boots/quality/freshness.
An applicable loss snapshot withdraws timing without refreshing hold; stale
snapshots cannot withdraw newer timing. Passive listeners never transmit.
Length-specific companion deadlines preserve the timing packet's original
cutoff. Host/native checks and later hardware populations retain separate limits;
this entry does not claim calibrated output precision or accepted deployment.

## 20 October 2026 — T0960.15.123 RC8 canary integration source

Normal builds add exact source-owner exports (a locked BLE follower needs no
local PPS; an actual GM needs its own qualified source), a separate UNKNOWN functional
clock estimate, page7/CDC controls and a bounded low-priority LoRa owner. A private
explicit exact-factory binding gates hardware; its default is disabled. Two
source intervals preserve original physical-event mapping and expiry. Operational
radio faults leave healthy BLE running and permit recovery after timely physical
closure. RC7/GM/USB authority and RC9 remain fenced. Integration revision3 also
preserves logical root boot independently of physical emitter boot; wire2 bytes
are unchanged. Native, exact-device and physical output qualification remain
separate gates; this entry does not claim a deployed or accepted image.

## 20 October 2026 — RC8 offline contract, wire2 revision

[RC8 wire version2](protocol/rc8/README.md) uses68-byte authenticated scope
metadata plus80-byte timing/correction frames, with the full metadata MAC as
context reference. Calculated4s timing/60s metadata occupation is6.833493%,
versus12.3008% for the superseded176-byte offline draft. Separate functional
availability, replay assurance, original-source authority and UNKNOWN output
uncertainty permit the intended passive secondary stream without a mandatory
short-lived external bootstrap. Eight-second correction matching and12-second
original-event hold remain unqualified policies. Metadata refresh never renews
timing, authority or an optional independent time anchor. Exact-factory TX,
listener prohibition, selected-GM export and BLE priority remain strict. Runtime
source/mapper/UI/radio integration and physical qualification are separate gates.

## 20 October 2026 — T096 framebuffer memory

T096 **0.15.122** stores the 80×160 display in one-byte palette indices and
expands five 32-row batches through a single persistent RGB565 DMA buffer.
All 17 existing colors and rendered pixels remain exact. Display storage falls
from 25,600 to 17,920 bytes; frame cadence, the shared 250 ms frame deadline,
synchronous DMA retirement and existing RAM/stack/controller gates remain.
LoRa remains disabled. Host pixel/transfer checks do not qualify hardware timing
or BLE coexistence; those require separate exact-device validation.

## 19 October 2026 — T096 active grandmaster status duration

T096 **0.15.121** labels the active GM `GRANDMASTER` on Precision and shows
green `ACTIVE mm:ss` for consecutive time in that role. The display timer
continues across navigation and the screensaver, and resets on role loss,
profile transition or stale input. PPS/HOT timers and intermediate HOT reasons
retain their separate meanings; timing authority is unchanged.

## 19 October 2026 — T096 active GM transmit-beat RMS

T096 **0.15.120** shows a Bluetooth badge and `BEAT RMS` on Precision only
while the GM is actively serving. The value comes from captured transmit-beat
timing; unavailable source/beat states remain explicit. The active GM has no
shadow-PPS mean or separate witness RMS. Following/standby GMs retain their
shadow-PPS display, RC retains its own BLE RMS, and all-page screensavers retain
the selected observer session behind the overlay. Timing authority is unchanged.

## 19 October 2026 — T096 RC displays its own BLE RMS

T096 **0.15.119** replaces Precision's old `CORRECTION` label and age value
with `BLE RMS`, using the RC's own local capture-fit RMS. Missing or expired
timing displays `--`; local GPS, shadow PPS and upstream quality cannot replace
this metric. The GM shadow-PPS layout and timing-owner policy are unchanged. Every page
may enter the existing idle screensaver; selected PPS observation continues
behind the overlay, and navigation wakes the selected page.

## 19 October 2026 — T096 Precision status and shadow PPS

T096 **0.15.118** gives the GM profile's Precision page a consistent grid,
stable green BLE/SoftPLL lock status and continuous PPS/HOT hold durations that
reset on loss. Eligible followers collect the existing independent shadow-PPS
session on this page, with its original dwell, ten-second settling and source
boundaries. Labeled mean and uncentered RMS replace the correction-age figure;
missing GPS/PPS/maps and a serving local GM show explicit unavailable states.
RMS retains all accepted errors with a bounded 96-bit sum. Display timers and
witness statistics grant no timing or HOT authority. Ordinary RC keeps its profile
and correction diagnostics; DC remains separate with Bluetooth disabled.

## 19 October 2026 — awake timing information and power scheduling research draft

Added Jonathan Yantis's standalone arXiv-targeted research draft and matching PDF,
with an edition/build receipt and selected sanitized PC117 evidence supplement.
All six 20-window runs, 42 refreshes/78 empty windows, failed priming, BLE coverage,
independent PPS fractional phase and brief fit turnover remain explicit. Earlier
PC112/115/116 context retains separate populations. Public standard-library Python
recomputes derived results and hypothetical guard/drift/energy scenarios offline.
Measured awake timing, modeled savings and future physical sleep/power work remain
distinct. No hardware, submission, push or publication release accompanied this draft.

## 19 October 2026 — T096 bounded sparse PC refresh

PC **0.15.117** suppresses degraded-parent discovery scanning while fresh admitted
ingress remains synchronized during an intentional fit gap. It uses a fresh
capture after queue drain and changes no time permission. Real ingress loss,
fallback and scan-ownership fences remain. The ordinary GM/RC/DC branch is
unchanged; prior PC observations retain the extra discovery activity.

PC **0.15.116** adds a separate phase-aligned 125 ms cadence. A fresh acquired
pair anchors a fixed schedule with an 8 ms RX lead; both RX and completion must
fit the original window. HOLD observations cannot retime it, late follow-ups
cannot widen it, and a changed source fails the treatment. Alignment metadata
and missed bursts remain explicit. GM/RC/DC timing is unchanged; awake training
windows remain separate from measured RF airtime, energy and sleep behavior.

PC **0.15.115** permits explicit `--pc-parent auto` for direct-GM discovery in
the isolated leaf. Relay candidates are excluded; authenticated admission,
source validity and output expiry remain. Numeric pins keep their original
scope. Automatic and fixed-parent captures require separate source populations.

PC **0.15.114** measures parent health from fresh authenticated ingress during
deliberate training gaps. Accepted pairs advance ingress progress while the
experimental map stays frozen, avoiding stalled-fit recovery during received
HOLD observations. Existing source/authentication/map expiry and GM/RC/DC code
paths remain. Earlier captures retain recovery interference and source refusals.

PC **0.15.113** adds finite warm-estimator LISTEN/HOLD cycles with fixed counter
deadlines, retained training across short bursts, per-record cycle/training
labels and explicit missed-burst counts. Completion keeps training suppressed
until a new experiment command. Reception remains awake for causal observation;
scheduled training fraction is not measured RF airtime or energy. Ordinary
GM/RC/DC timing and permission expiry remain unchanged.

## 19 October 2026 — SS3 survey and display recovery

SS3 **0.1.4** retires deferred sync cleanup when an asynchronous create failure
arrives during the cancel grace, fixing a reproduced phase-3 stall. Survey drain
keeps its original two-second bound and finishes each phase once. LCD DMA waits
are finite, retain buffers until actual completion, and feed independent display
supervision. A header activity mark, periodic repaint and CRC-protected display/
survey counters support long-run diagnosis without changing radio/task priority
or timing authority. Synthetic long-uptime and fault tests remain distinct from
finite hardware observation and continuous-reliability qualification.

## 19 October 2026 — T096 isolated awake-only PC experiment

T096 **0.15.111** adds a separate flash-only Power Control image with one
explicit direct-GM parent and no normal GM owner or downstream timing service.
Fixed-budget warm/cold-estimator LISTEN/HOLD treatments preserve frozen
experimental projections and causal pre-update BLE/PPS records. Radio HOLD
uses the existing bounded receiver drain and fresh receiver incarnation on
return. Normal GM/RC/DC timing, authentication and source/map expiry remain.
This stage implements no sleep or power measurement; RMS targets and calibrated
UTC/output performance remain unqualified. See the [PC protocol](docs/t096/power-control.md).

PC **0.15.112** explicitly creates the existing group-derived BLE identity
before Bluetooth initialization. The ordinary dispatcher normally owns that
step; the separate PC owner must perform it without starting the normal owner.
The first PC111 canary did not acquire timing within its fixed window and is
retained as a failed population; source inspection alone does not prove the
entire acquisition failure's cause. Ordinary profile timing remains unchanged.

## 19 October 2026 — T096 clear failed parent selection after outage

T096 **0.15.110** retires a previously working parent once usable time has been
absent for ten continuous seconds and PA is no longer synchronized. The long
cold-acquisition and degraded-source allowances no longer protect a powered-off
established parent. Exhausted acquisition or degraded recovery also clears the
selection without requiring an alternative locator. Discovery can then select
another parent or authenticate a returning identity through a new transaction.

Actual ACL and PA ownership must drain before the selection, old binding, fit,
source, proof and publication state are cleared. Failed drains retain their
original deadlines and fail closed. Failure history, rolling admission limits,
explicit parent configuration and relay-only enrollment survive. Admission-stage
progress cannot restart an established parent's loss interval. Existing source,
map and authentication expiry remain independent of recovery scheduling.
Retry journal reason 5 identifies retirement of a failed parent. Production-body
host regressions cover power loss, recovery before the grace expires, cold and
source-only interruptions, empty neighborhoods, returning identities and drain
failures. Field failover and long-run durability require separate observation.

## 18 October 2026 — T096 finite RC passive search

T096 **0.15.109** uses a finite 10ms/20ms passive RC search window instead of
continuous 20ms/20ms scanning. The degraded-parent watch remains 5ms/20ms.
This selects the pinned controller's cancel-on-preemption path rather than
retaining a continuous-scan resume prepare. Existing initiator windows are
already finite. Source/map permission, authentication, scan-search wall limits,
controller assertions and the RAM inspection minimum are unchanged.

A retained kernel fault after an RC-to-GM transition identified the pinned
controller's prepare-queue-full assertion. Its exact queued contexts were not
retained, so the continuous scan path is a mitigation target rather than a
proved complete fault cause. Production parameter/pinned-abort tests establish
the selected path; transition canary observations remain a separate hardware
check. Host-owned scan exposure does not measure physical listening airtime,
and the reduced search duty does not qualify reception or timing accuracy.

## 18 October 2026 — T096 serialize incoming GM admission

T096 **0.15.108** keeps the native GM's connectable G1 advertiser stopped while
its single incoming transient JOIN slot owns an ACL. Previously the owner
reopened advertising during that exchange, so receivers reacting to the same
GM change could connect together; one was rejected as a duplicate and entered
retry backoff. Advertising resumes after the actual disconnect callback releases
the slot. Existing control links, passive GM discovery, outgoing bootstrap and
periodic timing retain their behavior and deadlines. The pinned connectable
advertising option already stops automatically on connection; only the owner
can reopen it. No additional RAM, retries or timing authority are added.

RC marker-read diagnostics also preserve a real ATT error instead of replacing
it with a generic malformed-marker result. Production boundary tests cover busy
incoming ownership, disconnect requests without callbacks, restart after actual
release, outgoing discovery and the original ATT errors. Hardware qualification
and GNSS reception remain distinct from these checks.

## 18 October 2026 — T096 RC admission envelope and MTU completion

T096 **0.15.107** applies RC7's existing 1.2-second coarse replay interval to
initial direct-GM admission as well as renewal. A valid 145ms proof round with a
40ms source bound was previously refused by the old 100ms half-width limit.
RC7 now admits that conservative interval while retaining the original <250ms
round-trip limit, record/exchange authentication and full transit/source expiry
subtraction. The normal GM/control adapter retains its 100ms limit. Coarse
admission cannot establish precision lock or extend a source/map deadline.

The RC host MTU callback can recover an Unlikely Error only when the connection
is still live and its actual negotiated MTU already supports the 212-byte
control value. A disconnected link, unknown state, insufficient MTU or other
error remains refused. No MTU retry or transaction deadline extension is added.
Focused production regressions include the observed proof round, boundary ages,
expiry/codec fences and the live/disconnected MTU cases. Physical reception,
HOT readiness and sustained failover remain separate hardware observations.

## 18 October 2026 — T096 direct discovery after GM failover

T096 **0.15.106** consumes authenticated serving-GM announcements during RC
scanning. A fresh report matched to the same recent connectable G1 endpoint
requests one direct authentication attempt ahead of relay acquisition, selection
history and a former HOT peer's non-root cooldown. The old ACL, PA sync and local
publication drain before handoff; source, map and authentication are acquired
again. Ordinary start history remains intact, with one additional admission
reserve per receiver per minute for a new GM scope. Repeated reports for an
attempted scope cannot rearm it. Explicit parent and relay-only policies retain
their meaning. RC scanning resumes while an established parent supplies no usable
time. Signed reports are scheduling hints and cannot authorize timing.
Production selector/ingress regressions cover protected relays, promoted HOT
peers, endpoint/group/MAC/sequence/queue-age guards and bounded retry reserves.
Native builds and hardware observations remain separate checks.

## 18 October 2026 — T096 native GM control drain races

T096 **0.15.105** retains duplicate ACL ownership when a public disconnect
returns Command Disallowed or Not Connected during a natural disconnect race.
Only the matching callback releases the reference; a missing callback still
fails at the original two-second deadline. Native GM scan-stop contention gets
at most 500ms from the first refusal, with 20ms retry spacing and actual public
stop success required. Hard errors and RC-profile scan errors remain failures.
Terminal transport snapshots are retained without a pre-fault diagnostic poll.
Source/map permission, authentication, election and maintenance bounds are
unchanged. Host boundary regressions include callback races, missing callbacks,
persistent contention, hard errors and an expected-failure legacy control;
hardware durability remains unqualified.

## 18 October 2026 — SS3 event diagnostics and startup splash

SS3 **0.1.3** consumes GM role changes during its existing passive scans, with
at most one signed-report check per second. Major scope/membership changes and
HOT withdrawal update promptly; a new HOT requires matching observations
spanning ten seconds. The two-minute routine fallback, authentication/freshness
guards, scan schedule, task priority and precision traffic remain unchanged.
An early PToB splash identifies startup before USB/BLE initialization and stays
visible for at least one second without delaying those owners. Host checks and
native builds remain separate from hardware verification and timing qualification.

## 19 October 2026 — Mixed-target relay and timing overview

Refresh the T096 and W12 entry guides around the current G1/RC7 mesh, keeping
version-specific policy and diagnostic details in the changelogs and build
guides. T096 **0.15.104** includes longer relay acquisition/recovery, aged
selection confidence, earlier authentication renewal and source-aware retry
scheduling. W12 **0.1.22** includes channel-aware acquisition, bounded parent
trials, direct authentication sessions and production G1 PPS diagnostics.
Both targets retain established downstream trains for up to 30 seconds through
timing loss without emitting corrections or extending timing permission.

W12 estimated transmit timestamps use the nominal BLE1M ADDRESS reference for
mixed-target reception; per-device provisional profiles remain scoped aggregate
adjustments. Actual RF delay, residual path offsets, intermittent timing loss
and independent UTC/output accuracy remain unresolved qualification work. The
optional fast counter-map diagnostic defaults off and supplies no BLE repair.
Historical evidence, missing observations and earlier failed candidates retain
their original identities and limits.

## 19 October 2026 — Optional W12 PPS counter-map diagnostic

W12 **0.1.22** adds an explicit, default-off fast counter-map diagnostic for the
verified GNSS profile. It uses the existing driver latch around software capture
while retaining the full enclosure, physical PPS and model availability rules.
Build identity, actual compiler flags and stack checks bind the treatment.
This changes measurement overhead; it does not correct relay timing or qualify
BLE stability or UTC accuracy.

## 18 October 2026 — W12 production G1 PPS diagnostics

W12 **0.1.21** observes the production G1 actuator at physical PPS on the
explicit GNSS profile. Bounded records separate relative and output-corrected
phase, prior model availability, source permission and empirical GPS reference.
The strict R2 metric remains distinct; no clock discipline, timing permission
or GNSS command changes. Native stack checks cover the snapshot and callback
paths. Offset, RF delay, BLE durability and UTC accuracy require hardware tests.

## 18 October 2026 — T096 bounded owner state storage

T096 **0.15.104** stores route indices and the startup admission stage in
eight-bit fields with a compile-time route capacity check. Their validated
ranges fit this representation. Version 0.15.103 failed the native static RAM
margin because eight added bytes crossed a 64-byte alignment boundary. The
1,024-byte minimum, stacks, transport buffers and diagnostic capacities remain
unchanged. Native inspection and hardware qualification remain separate gates.

## 18 October 2026 — T096 explicit parent measurement control

T096 **0.15.103** accepts an explicit parent identity through its ordinary GM
or RC locator while remaining in RC mode. This enables direct-versus-relay
measurements without changing automatic selection, enrollment exclusions or
authenticated time and clock gates. Actual selector and admission regressions
cover unavailable, stale, excluded and unauthenticated sources. Hardware timing
and durability qualification remain separate.

## 18 October 2026 — T096 source-aware recovery

T096 **0.15.102** conserves new recovery transactions while an established,
authenticated relay is receiving fresh canonical packets with no corrections.
The original admission must remain live and younger than four minutes; normal
renewal, expiry, missing PA and changed authenticated paths bypass this wait.
Replayed packets cannot extend the 500 ms hint. In-flight transactions, rolling
start limits and all clock/output validity gates retain their existing bounds.
Actual owner/codec/matcher regressions and the full host suite pass, including
an expected-failure control with source-blind recovery restored. Hardware
recovery gains and native RAM margin require separate verification.

## 18 October 2026 — T096 relay retry stability

T096 **0.15.101** preserves pending same-parent retry timing when correction
loss persists. Repeated recovery polls no longer erase backoff, batch delays or
prepared start jitter. Authenticated path changes still rebind immediately;
source expiry, precision, output and controller ownership limits remain intact.
Pinned AUX connection diagnostics now cover the extended advertising path used
by W12 relays and retain the last initiation result after journal overwrite.
Three journal records fund that evidence without lowering the native RAM gate.
Production recovery/controller regressions and the full host suite pass; old
behavior controls fail as expected. Native and hardware qualification remain
separate, including the physical cause of unsuccessful connection attempts.

## 18 October 2026 — T096 relay continuity and recovery

T096 **0.15.100** retains an established RC downstream train through an observed
loss for up to 30 seconds, sending canonical no-correction packets while time
is invalid. It preserves original child leases, source/authentication expiry,
500 ms precision validity and controller ownership. Two seconds of observed
recovery clear the loss episode; flicker cannot renew it. Earlier four-minute
RC7 authentication renewal and deadline-aware retries reserve time before the
original six-minute expiry. Actual-worker/codec/matcher sanitizer regressions
retain failure and negative-control populations. The precision rejection counter now carries the actual wire-plus-timing total.
Hardware continuity, timing bias and UTC accuracy require separate observations.

T096 **0.15.99** separates intermittent timing progress from watchdog failure.
RC recovery now requires 30 minutes without usable timing. A usable observation
refreshes that deadline; 30 seconds of sustained recovery clears escalation.
Hardware, CPU, RC-owner and recovery-observer allowances are at least 30 seconds.
Source/precision validity and controller drain deadlines remain independent.
This gives normal selection time to leave a degraded RC1 for an eligible RC2
without favoring depth; RC2 reception remains leaf-only. A combined production
selector/watchdog regression covers that path. Explicit bootloader entry still
requires the original drained-owner proof and timely commit, followed by the
longer hardware reset interval.

## 18 October 2026 — T096 Syncing display delay

T096 **0.15.98** delays the transition from an already displayed RC analog clock
to Syncing until timing loss lasts 500 ms. The face advances from its last
display sample during that interval; repeated bad samples cannot move the loss
deadline. Startup has no grace. Profile changes, transport withdrawal and stale
observations cancel the cache. The display delay does not extend source/map
validity, precision lock, USB output, relay service or screensaver permission.

## 18 October 2026 — T096 tolerant relay field candidate

T096 **0.15.97** collects ten seconds of host-owned discovery exposure with a
60-second wall ceiling, admits idle relays at cold startup, and ranks bounded
RSSI alongside sample- and age-weighted usable-time/delivery history. Quality
confidence has a five-minute half-life and a 30-minute eviction horizon; path
changes discard usable-time proof and group changes clear history. Relay trials
allow 180 seconds for admission, 300 seconds for acquisition starting once at
first PA establishment, and 600 seconds total. Settled relays get 300 seconds for
degraded recovery; exhausted trials use 30/60/120-second probation. RC7 management
discovery remains available through timing loss while periodic timing, source/map
validity, authentication and RC2 leaf restrictions retain their gates. Host-owned
scan time is not measured RF airtime. Field durability remains unqualified.

## 18 October 2026 — T096 shadow PPS startup settling

T096 **0.15.96** collects a separate startup population before starting the
shadow PPS page's statistics. It requires ten seconds of consecutive usable
PPS/map observations from the same root, source epoch and parent. Invalid or
missing observations and source changes restart that interval. The qualifying
PPS edge is the measurement boundary; all later eligible errors contribute to
the mean, percentiles and maximum. Raw records remain available during startup,
and the console reports startup counts separately. This phase-blind delay
does not change timing, calibration, source selection or GNSS configuration,
guarantee a smaller maximum, or establish the cause of an observed phase step.

## 18 October 2026 — Direct-GM authentication load

W12 **0.1.18** and prepared SS3 **0.1.2** separate direct BLE100 authentication
from coarse replay-clock maintenance. Fresh matched GR3 corrections maintain
that anchor inside a fixed six-minute session; renewal is randomized over
225–240 seconds. The original source/map expiry, uncertainty and controller
ownership limits remain independent. BLE50 and RC7 admission policies are
unchanged. Host/native regressions retain hard-expiry and refusal controls;
physical stability remains a separately observed outcome.

## 18 October 2026 — W12 generated provisional-profile binding

W12 **0.1.17** fixes the generated macro binding that left 0.1.16's declared
provisional profile at zero in the compiled config. The exact manifest guard
refused that mismatch. A regression now exercises the actual builder-generated
header through the production config and emitted timestamp, and native compile
assertions reject a repeated mismatch. Timing qualification remains unchanged.

## 18 October 2026 — W12 provisional aggregate-path adjustment

W12 **0.1.16** adds an explicit per-device provisional offset to emitted
estimated relay timestamps, with zero as the default. The profile binds its
publisher, direct-root boot/source scope and measurement provenance, and affects
all downstream receivers using that publisher. Raw estimates remain available;
both raw and adjusted times must pass the original signed source envelope.
The adjustment does not change the fitted clock, protocol rules or precision
gates, identify TX versus RX bias, or provide an RF/UTC error bound. Residual
jitter is reported separately and is not subtracted from uncertainty.

## 18 October 2026 — T096 GM renewal retry headroom

T096 **0.15.95** starts direct-GM authentication renewal after four minutes,
leaving two minutes before the existing binding limit. The former five-minute
schedule could exhaust its remaining minute after failed exchanges under the
rolling two-starts-per-minute budget. Relay renewal, the warm retry backoff,
original transaction/drain deadlines, source/proof gates and held-map expiry
remain unchanged. Production renewal regressions retain the actual start
limiter and binding deadline; an old-schedule negative control exercises the
failure. This provides retry headroom and does not repair an unavailable GM
source or establish continuous hardware stability.

## 18 October 2026 — W12 connection refusal containment and TX evidence

W12 **0.1.15** contains controller Connection Parameters rejection only for its
exact validated outgoing request before any ACL is adopted. It records the
failed admission and retains the original retry, source expiry and drain limits.
Other errors and ownership violations remain faults. Capture-only successful TX
records retain the selected immutable publication, programmed timestamp,
controller clock bracket and emitted correction, sampled at most once per
sixteen events and 60 times per capture. These are estimated timestamps with an
unknown RF bound. Production regressions and old-behavior negative controls
cover both changes; controller cause, incoming-owner faults and RF timing
accuracy remain unqualified.

## 18 October 2026 — T096 shadow PPS session visibility

T096 **0.15.94** keeps the explicitly selected shadow PPS page awake instead of
letting the global five-minute idle timer end its witness session. Selecting the
page from an already active screensaver also wakes it. Leaving the page retains
the existing session stop and normal screensaver behavior. Production UI tests
cover a ten-minute witness run, entry after the idle deadline and all earlier
UI modes. The GM/HOT fix remains; GNSS, source/servo, reference eligibility,
map expiry and phase corrections are unchanged. This restores measurement
collection and does not calibrate W12 timing.

## 18 October 2026 — T096 GM dwell and HOT discovery refusal

T096 **0.15.93** retains a direct GM for 60 seconds of admission and up to
120 seconds of precision acquisition within a 180-second ceiling. A previously
settled GM gets 60 seconds to recover degraded timing instead of six. Expired
maps still withdraw immediately. When a normal HOT/follower G1 participant
returns an authenticated non-root refusal, another fresh direct locator takes
priority over a relay. That refusal cannot establish timing or change the GM.
Production selector regressions cover seven-second loss, fixed boundaries,
non-root discovery and finite fallback. Relay dwell, healthy-parent retention,
HOT/election, slot rules and source/proof/precision gates remain unchanged.

## 18 October 2026 — T096 relay admission dwell

T096 **0.15.92** gives a chosen relay up to 180 seconds to finish authenticated
admission, followed by its full 120-second precision trial within a five-minute
absolute ceiling. The former 60-second pre-admission cutoff could reject a
working relay while demand startup and bounded control retries were still in
progress. Repeated PENDING or renewal does not restart either deadline.
Production selector regressions retain the rolling control-start budget and
cover a late successful admission, finite silent-parent cutoff and unchanged
GM preference. Transaction, slot, source, proof and precision gates are unchanged.

## 18 October 2026 — W12 connection cleanup and fault evidence

W12 **0.1.14** installs adopted outgoing ACL termination ownership before a
public SDK call can yield, preserving its original drain deadline. Production
regressions cover a delayed CONNECT during termination, wrong handles, stale
callbacks and missing actual disconnection. Fixed incoming relay-owner records
add descriptor, handle, decision and drain evidence through the existing bounded
diagnostic queue. The observed incoming-owner fault remains a separate unresolved
hardware finding; source changes and passing synthetic checks do not qualify a
repair or physical stability. Slot and timing authority rules remain unchanged.

## 18 October 2026 — T096 terminal receiver admission

T096 **0.15.91** keeps an RC2 stream client receive-only and withdraws its RC7
mesh admission advertising. A peripheral connection racing the role change is
drained through the existing close path. Actual production layout tests cover
RC1-to-RC2 publication and a leaf client receiving that RC2 stream. USB timing,
UCS1 ownership, slot selection rules, the two-relay depth limit and validity
gates are unchanged. The 0.15.90 relay-read and strongest-scan fixes remain.

## 18 October 2026 — T096 relay interoperability and scan selection

T096 **0.15.90** discovers the relay characteristic declaration before reading
its 212-byte capability by handle. The pinned W12 NimBLE Read By Type path
flattens values through a 19-byte buffer and returns ATT unlikely-error for that
larger capability; repeating read-by-UUID could never complete admission. Each
new client ACL clears its handle. Metadata discovery grants no authority, and
authentication, source/path validity and the original two-second transaction
remain required.

Every automatic parent search now collects three seconds of locators, including
recovery. A four-second advisory locator horizon retains the full scan, and
eligible relays rank by smoothed signal strength before history/publishing ties.
Selection/refusal and empty scans end their collection; a healthy parent or its
full acquisition trial stays protected. GM preference and failed-source deferral
remain intact. Production regressions cover advertisement order, early strong
observations, per-search collection and the SDK read restriction. Hardware
reliability and independent timing accuracy remain separate validation work.

## 17 October 2026 — T096 previously settled GM retry

T096 **0.15.89** distinguishes a previously settled GM from one that never
supplied sustained usable time. After an interruption the former uses bounded
60/120/240-second retry deferral; the latter retains300/600/1200 seconds. Failure
history remains until sustained observed recovery. Healthy relays and active
relay trials stay protected when a GM becomes eligible again. Fresh admission,
source/map permission and all control deadlines remain required; earlier retry
does not repair an unavailable GM source. Actual selector/history regressions
cover exact retry boundaries, escalation, never-settled GM failure and a strong
returning GM during a relay's full precision trial. Hardware durability remains
under observation.

## 17 October 2026 — T096 settled relay recovery

T096 **0.15.88** gives a previously settled relay 120 seconds to recover degraded
timing. The former 30-second interval could expire after only one admission
opportunity under the retained two-starts-per-minute control budget, sending
recovery through untried relays. Repeated admissions and lock flickers cannot
restart the degraded episode; source/map expiry, invalid-path rejection, GM
preference during selection and original control deadlines remain unchanged.
Production selector tests exercise the control-budget interaction, stronger
competitors, exact expiry and sustained recovery. Field stability remains under
observation.

## 17 October 2026 — T096 transient relay capability reads

T096 **0.15.87** permits up to three 50 ms retries of an initial relay capability
read that reports ATT unlikely-error or insufficient resources on a live ACL.
Previously the first such refusal immediately ended the transaction. Retries
remain within its original two-second deadline and grant no time authority.
MTU, authentication, response/write errors, disconnected owners and exhausted
budgets retain their refusal behavior. Production lifecycle tests cover these
boundaries. Field relay acquisition remains under observation.

## 17 October 2026 — T096 GM renewal cadence

T096 **0.15.86** requests the existing 7.5 ms cold connection cadence for direct-GM
renewals as well. A monitored RC lost its binding after a renewal proof exceeded
the original 250 ms reply-age limit and subsequent retries failed. Relay renewals
retain 50 ms; source/permission validity, transaction deadlines and timing-first
radio arbitration remain unchanged. Production policy tests cover all cold/warm
GM/relay combinations. Hardware renewal durability remains under observation.

## 17 October 2026 — T096 relay acquisition stability

T096 **0.15.85** gives an admitted relay two minutes to establish sustained usable
time, with up to one minute for admission and an absolute three-minute acquisition
ceiling. An admitted GM gets one minute for lock. Recovery prefers a proven
usable GM, otherwise favors an eligible relay over an unproved GM, retains the
selected source through its trial, and gives a settled relay
30 seconds to recover. Failed timing sources are deferred for five minutes with
bounded escalation; brief lock reports and renewals cannot restart a trial or
erase failure history. Source and map expiry still withhold invalid time.

Relay-path refusal no longer leaves discovery in a GM-only state: it defers the
refused locator and can try another eligible relay. Failed periodic-sync
establishment retries within a live authenticated binding, rather than forcing
GATT renewal after each 1.5-second attempt. Expired bindings, changed paths and
stalled established timing streams still trigger authenticated recovery; the
original two-second sync attempt and final 500 ms cancellation budget remain.

Production selector/history regressions cover late relay lock, stronger competing
peers, returning GMs, repeated PENDING/renewal replies, degraded streams and
expiry. Actual relay-admission and PA recovery bodies cover path refusal, depth-two
receivers, retry ownership and original deadlines. These checks join the normal
offline suite. Field relay lock and durable
sync remain separate hardware validation work; this change installs no image.

## 17 October 2026 — W12 bounded GM lock trials

W12 **0.1.13** gives an automatically selected GM a full minute to establish
sustained relative lock, then seeks a fresh relay even while GM packets arrive.
Same-parent renewals and brief lock flickers cannot extend that trial forever.
Five-minute advisory deferral prevents immediate return to a failed parent.
Existing fast acquisition, fine hold, quality/authority gates and bounded radio
operations remain intact. Policy and production scheduling tests cover the
previous indefinite GM reservation; hardware failover remains separately observed.

## 17 October 2026 — DC serial queue timestamp race

T096 **0.15.84** samples the DC serial worker's queue-expiry clock under the
same lock as UART output timestamps. A fresh UART interrupt between the old
clock read and lock acquisition could otherwise discard a new chunk as having
a future timestamp. The actual bridge regression exercises this interleaving,
partial FIFO writes, the unchanged 500 ms backlog expiry and USB reset. The
change is confined to DC; precision capture and GM/RC owners are unchanged.

## 17 October 2026 — DC UFirebird II capability probes

T096 **0.15.83** extends only the DC host command policy with product/MSM/RTK
queries and UART1 raw-observation, ephemeris and station-message queries and
bounded zero/one rate controls. Explicit fixed-baud protocol masks preserve or
restore the observed R6 configuration during a raw-output experiment. PPS,
RTK-mode writes, resets, saves and arbitrary configuration remain refused.
This permits checking receiver capabilities without confusing bridge rejection
with receiver rejection; it does not establish raw output or an RTK solution.

## 17 October 2026 — DC GNSS serial development

T096 **0.15.82** adds a DC-only independent CDC ACM function for original GNSS output,
CRC-checked RTCM input and narrowly allowed UFirebird configuration commands.
The DC clock parser isolates binary observations while retaining its original
RMC/GGA and aiding path. The existing UART owner serializes host traffic with
aiding, enforces original ingress age and waits for DMA return during shutdown.
GM/RC retain their original GNSS path and single-CDC USB layout. Build receipts
identify the DC bridge; portable transport and UART-owner tests join the normal
offline suite. This is source development, not an installed or qualified image.
Receiver firmware support, RTK solutions, physical coexistence and independent
UTC calibration remain unverified.

## 17 October 2026 — publication data attachment

The [USB comparison supplement](evidence/usb/2026-10-17/README.md) attaches all
11,520 USB samples and 354 status observations from the existing fixed window.
Sanitized CSVs preserve warm-up, rejections and withheld output; a public offline
verifier recomputes the reported statistics. Raw packets, per-observation UTC,
device bindings, enrollment and local build artifacts remain excluded.

The client guide and matching paper clarify that chrony estimator medians use
the latest synchronized tracking readout sampled at each status point, including
cached values. Numerical results are unchanged. Source/PDF/evidence identities
are updated together, prior bytes remain in Git, and the paper remains nine pages.

## 17 October 2026 — USB timing paper refresh

The USB/macOS research draft replaces its October 15 main results with the
fixed concurrent 180-second RC v0.15.81/DC v0.15.80 comparison. It separates
valid USB replies, corrected-source RX predictions, chrony tracking readouts,
shared-source delivered agreement and sampled availability, preserving rejected,
warm-up and withheld counts. The bus was **shared, not clean**; isolated-controller
validation, completed two-hour trials, PPS/UTC commissioning and independent
endpoint calibration remain pending. Prior editions and historical failures
remain in Git and their immutable evidence collections. The matching source/PDF,
new edition manifest and offline identity/accounting checks are updated together;
the paper remains a draft and is shorter than the prior 11-page edition.

## 17 October 2026 — shared DC/RC USB publication

T096 **0.15.81** moves DC's unchanged-map publication rule into the existing
common USB publisher. Repeated owner polls preserve an in-flight serialized
observation; actual source/scope/permission changes and shortened session
deadlines still fence it. Original map and authority deadlines cannot be
renewed by polling. RC exports its authenticated BLE clock even without BLE
relay TX ownership. Source discipline, RF behavior and GM/RC switching remain
under their existing owners; DC remains a separate flash-only image.

Both macOS launch paths now use the DC trial's quarter-second chrony poll
cadence, retaining `filter 4`, `maxsamples 64`, source expiry and unknown UTC
bounds. The change applies to either USB source profile and starts no daemon
until an explicitly launched live session. Offline publication/race tests cover
unchanged polls, real revocation, source/session fences and receiver-only USB.
Hardware results are recorded against the exact installed image separately.

## 17 October 2026 — corrected-source RX baseline before chrony

The macOS USB-only readout now shows **RX USB RMS** from corrected-source
prediction errors against hardware SOF/Mach frame boundaries. A frozen previous
64–1024-sample affine fit predicts each admitted immutable-map sample; the
current sample cannot train its own prediction. Bias and transients remain in
RMS, UTC translation is excluded, and qualification, warm-up, rejected samples,
gaps and source scopes remain explicit. The live readout and offline precision
replay share the implementation. `--no-chrony --no-ntp` supports a baseline
without starting a daemon or adjusting the host clock. This relative metric
does not qualify absolute UTC or modify T096 GM/RC firmware.

## 17 October 2026 — DC unknown-association source admission

T096 **0.15.80** corrects DC's initial gate: the raw clock's `source_available`
requires commissioned PPS/UTC labeling and cannot become true on this adapter's
unknown association. DC now uses the existing source owner's fresh fix/PPS and
valid clock conditions, together with its PPS-disciplined map, matching epochs
and unchanged expiry checks. `dc_source_active` reports USB map availability
separately. UTC qualification and the raw clock's association bit remain unknown.
The actual parser/clock test covers this board-default path; the failed first
DC hardware population is retained locally rather than treated as settling time.

## 17 October 2026 — dedicated T096 Direct Connect image

T096 **0.15.79** adds **DC**, a separate flash-only local GNSS/PPS-to-USB image.
Bluetooth is disabled; mesh/controller owners, group credentials and the GM/RC
toggle are absent. Existing GNSS discipline, hardware USB SOF evidence, immutable
publication intervals, quality/epoch fences, EP0 recovery, watchdog, display and
supported maintenance remain. The normal GM/RC image retains its role behavior.
The dial retains the temperature-colored 32 Hz badge; the DC header uses the
font's supported uppercase letters so every character is rendered.

The DC face replaces BEAT RMS with USB RMS: recent per-frame USB SOF span error
against the GNSS-disciplined map, with sample/epoch/freshness gates. It does not
measure macOS/chrony RMS or absolute UTC error. The offline DC tests exercise
causal conversion, local-root USB provenance and withdrawal; the macOS tests
accept a DC map under the existing contract. The builder/inspector distinguish
DC and can package an exact-device local candidate without a PSK. Hardware
durability/precision, generic installation and website distribution remain
separate gates. See the [DC guide](docs/t096/direct-connect.md).

## 17 October 2026 — W12 background control tolerance

W12 **0.1.11** separates direct BLE100 control from precision deadlines: a
one-second control queue-age limit, four-second initiation, 500 ms supervision
and GATT discovery bounded by the original five-second authenticated exchange.
Six warm starts per rolling minute retain five-second spacing, with 5–10-second
failure backoff. BLE50/RC7,
actual owner drain and all precision capture, freshness, quality and source
checks stay intact. Tests distinguish delayed membership from stale precision
and reject control received within an exchange but processed after its expiry.

## 17 October 2026 — W12 replay recovery through unusable replies

W12 **0.1.10** starts BLE100 replay refresh at 100 ms uncertainty, urgent at
125 ms, to leave more time for a failed transport followed by authenticated
unusable time and the rolling start restriction. Combined-policy regressions
exercise a third attempt; the prior 150 ms policy misses the same finite replay
deadline. For a still-reserved current direct stream, failure jitter uses the
remaining replay time while retaining the 20-second minimum and rolling rate
limit. Hard source/model/replay expiry remains unchanged. Repeated failures can
still exhaust the context.

## 17 October 2026 — W12 precision reception before control work

W12 **0.1.9** reserves current authenticated precision reception through relative
quality dips, renews its current parent without broad discovery, and requests
5 ms/20 ms warm initiation windows. Queued precision reports are drained between
control operations under one shared eight-report budget per worker iteration.
Source/model/replay expiry, quality gates, cold acquisition and actual owner-drain deadlines remain
unchanged. Synthetic public-SDK boundary tests reject the previous continuous
warm initiation and delayed report processing. Physical radio scheduling and
long-term lock stability require separate observations.

## 17 October 2026 — W12 replay refresh retry budget

W12 **0.1.8** starts direct BLE100 replay refresh earlier, at 150 ms uncertainty
with urgency at 175 ms. The old 200 ms threshold left about 25 seconds before
replay expiry, less than the existing 20–40 second warm retry backoff. The new
threshold reserves about 50 seconds for a failed exchange and a retry. The
two-starts-per-minute cap, hard replay limit, signed source/model deadlines and
relative lock criteria remain unchanged. Combined-policy host tests reproduce
the old scheduling failure; multiple failed attempts can still lose lock.

## 17 October 2026 — native GM control latency budget

T096 **0.15.78** requests20ms native GM control ACL opportunities. The prior
50ms interval could consume100ms in two event waits, exceeding HELLO's unchanged
80ms causal map-width limit before owner and air delay. Passive bootstrap stays
7.5ms; legacy/field control stays50ms. Reserved precision events retain priority,
and source/authentication deadlines and HELLO cadence remain unchanged.

The regression feeds the transport's actual selected interval into the production
G1 codec/network with one event wait plus5ms modeled owner delay per direction,
checking settled HOT continuity, source dwell and replay refusal. The previous
50ms selection fails this budget test. This is a bounded latency model; physical
radio loss, hardware HOT stability and continuous delivery require live evidence.

## 17 October 2026 — finite native G1 initiation

T096 **0.15.77** replaces native G1's default continuous60ms/60ms initiator
with finite5ms/20ms windows. Its connection opportunity, ACL interval and
authentication deadlines remain unchanged. The regression checks the actual
parameters passed to connection creation and rejects the previous continuous
default, independently of ordinary discovery scan parameters.

It also exports the existing controller initiator counters after native
central connection callbacks. The records remain cumulative within one MCU
boot and distinguish prepared windows, valid reception, matching targets,
prepared requests, preemptions and resource/window refusals. A read of the
native1M scan flags accompanies them. No new static baseline or recovery action
is added; these diagnostics do not qualify a successful connection or HOT pair.

## 17 October 2026 — unpaired GM discovery reservations

T096 **0.15.76** uses the existing distributed5ms/20ms peer-discovery policy
for an unpaired native GM as well as a serving or established HOT core.
Previously the unpaired follower requested50ms control scan reservations beside
protected62.5ms precision events. Explicit PA acquisition and passive-client
windows retain their original budgets. The production transport regression
checks the unpaired scan parameters; the previous guard fails it. This is a
control scheduling correction, not a relaxation of HOT authentication or source
qualification.

## 17 October 2026 — bound unpaired GM discovery

T096 **0.15.75** applies the existing100ms connection-creation opportunity to
all transient native GM discoveries. An unpaired GM previously escaped the
ready-core guard and could initiate for the controller's full two seconds.
Cancellation still retains the connection until its callback drains; neither
provisional enrollment nor timing authority is extended. Production transport
regressions cover the unpaired path, an established core and negotiated MTU;
the previous guard fails the unpaired-path regression. Hardware HOT recovery
and continuous delivery remain separate verification requirements.

## 17 October 2026 — native GM pairing diagnostics

T096 **0.15.74** records bounded native control stages in the existing timing
journal: create/cancellation, MTU/discovery, JOIN send/receive/retirement and
control promotion. It adds no static journal storage or control authority.
This diagnostic candidate does not itself repair or qualify HOT pairing.

## 17 October 2026 — fractional phase dispatch correction

T096 **0.15.73** includes the fractional phase field in the controller's
empty-update check. The v0.15.70–72 dispatcher rejected a fractional-only
request before it reached the phase adjustment, returning a bounded error while
leaving the existing legal-slot withdrawal checks active. The regression now
executes the generated queued API and actual dispatcher for both directions,
and verifies rejection of genuinely empty/stopped requests and deferral of an
expired event. The earlier dispatch logic fails this regression. No ticker
interval, source deadline or control priority changes in this correction.

## 17 October 2026 — bounded USB SOF coherence retry

T096 **0.15.72** retries a hardware SOF snapshot once when a frame arrives
between the counter/register reads. Both reads use the normal capture boundary;
interrupts remain enabled between them. Missing hardware, USB epoch changes or
two incoherent snapshots still return explicit unavailability. Source, map and
session deadlines are unchanged. The offline regression covers recovery on the
second physical read, persistent incoherence and an epoch change.

## 17 October 2026 — T096 USB map handoff

T096 **0.15.71** keeps an already published, compatible USB map usable while
the next discipline is prepared. The old projection retains its original map,
source and session deadlines; the new projection starts at publication. UTE1
can return the older map for its applicable SOF instead of replacing it with a
newer generation that does not cover that observation. Root, boot, capture,
path, UTC-offset changes and concurrent withdrawal still fence old authority.
Production-body tests reproduce the previous preparation gap and check both
sides of commit, original expiry and incompatible-scope rejection. This repairs
an unnecessary observation gap, without claiming repair of the independent USB
transport failure or extending timing authority.

## 17 October 2026 — T096 precision scheduling candidate

T096 **0.15.70** gives reserved periodic TX/RX events precedence over admission,
discovery and connected control in the pinned controller. It retains native
arbitration between precision events and all radio assertions. An experimental
RC phase servo applies fractional 8 us adjustments inside the legal slot,
without recreating the train; stale or illegal placement still withdraws or
reacquires normally. New phase journal records distinguish placement failure,
adjustment, start, quality withdrawal and demand expiry.

Validate admissions before changing a live RC7 path or resetting its receiver.
Same-scope renewals retain pending original/follow-up associations, fit and
original map deadlines. USB queue-overflow restart now fences old EP0 events,
replays current VBUS/regulator readiness and tolerates duplicate READY events.
The separate indefinite DMA wait and the historical persistent USB blackout
are not established as repaired. Source/session expiry and USB freshness limits
are unchanged. New production-body sanitizer tests are included in the offline
suite; simulation/build success does not establish hardware durability or UTC
accuracy.

## 16 October 2026 — macOS guide organization

Restore the original animated client preview and give the macOS README dedicated
interface, usage, tracking-results, continuity and provenance sections. Integrate
the selected ChronyControl view with the bounded publication-check results,
keeping the later screenshot and earlier hardware populations distinct.

## 16 October 2026 — macOS documentation placement

Move the ChronyControl screenshot, author attribution and tracking/development
notes to the macOS client README. Keep a short client link on the main README.
The image, historical evidence and firmware behavior are unchanged.

## 16 October 2026 — tracking preview refresh

Replace the README preview with a user-selected cleaner approximately ten-minute
ChronyControl interval, retaining Bryan Christianson's attribution. Use a calmer
development-status caption focused on planned GM authentication and transient
reduction. Preserve the earlier screenshot and detailed failure populations;
this presentation update changes no firmware, logging or measurement claims.

## 16 October 2026 — timing-chain stability and continuity

This update integrates T096 **0.15.69**, W12 **0.1.7**, and the macOS client
continuity changes. It is an experimental source update. The
[engineering results](evidence/stability/2026-10-16/README.md) retain the failed
and incomplete populations alongside the bounded successes.

- T096: improve cold admission, connection cancellation and established-link
  diagnostics; reserve native controller RX headroom under mesh load. Request
  100 ms RC discovery, gate every RC advertisement on usable upstream precision,
  and reduce failed steady control attempts without slowing the 16 Hz precision
  train. Legacy 300 s renewal and 360 s admission lease remain.
- T096: retain a separately labelled local clock estimate through source loss;
  preserve original authentication, source expiry and downstream authority gates.
  Move USB projection and serialization outside the interrupt-masking map lock,
  with a generation check that refuses raced observations. Add bounded native
  receive-outcome, transition, watchdog and retained-fault diagnostics.
- macOS: display HOLDOVER / LOCAL ONLY with observation age and unknown error;
  preserve the owned chrony process across compatible quality loss while pausing
  measurements. Source/counter changes still fence training. Reuse USB buffers,
  coordinate readers with a per-silicon kernel lock, and rediscover after stale
  transport errors. Add opt-in two-hour capture and ChronyControl observation.
  The README includes a user-supplied ChronyControl tracking screenshot with
  unresolved spikes preserved and attribution to Bryan Christianson.
- W12: align RC control/admission behavior, retain an OLED-only HOLD estimate,
  and add bounded receive-loss diagnostics. Isolate large status snapshots and
  enforce a static main-stack budget after rejecting a stack-overflowing
  candidate. See the [W12 changelog](firmware/w12/CHANGELOG.md).
- Preserve earlier W12 GNSS indicator/factory-binding repairs and the SS3 relay
  display fix in the integrated history. W12 remains RC-only with no USB timing
  relay; longer negotiated leases and quality-bearing BLE holdover are deferred.

The controller RX refusal was localized and passed bounded recovery checks.
TC-02's brief v69 USB check had no request errors. TC-04 still failed with
persistent USB/CDC nonresponse; v68 also retained controller assertions and MCU
restarts. The new USB lifecycle investigation is a proposal, not an applied fix.
These results do not establish continuous delivery, qualified HOT failover,
metrology readiness or independent UTC accuracy.

## W12 0.2.4 — separate RC9 and UTC-boundary display

- Schedule OLED time paints from the selected UTC second instead of prior paint time.
- Match N30 functional RC8 pre-companion time/RMS; preserve unknown origin bounds.
- Add independent authenticated RC9 client/service with original source expiry,
  distinct BLE class and client leases pinning AUTO LoRa through BLE recovery.
- Preserve GNSS inputs, normal G1 precision isolation and original owner drains.

## W12 0.2.5 — normal flash preparation

- Keep RC9 capability discovery active through speculative upstream retry;
  yield only for actual outgoing admission/ACL ownership.
- Expose RC9 service-owner flags without changing the normal status frame.
- Preserve UTC-boundary display paints, current source expiry, original client
  leases, GNSS inputs and the existing normal RC7 scheduling policy.
