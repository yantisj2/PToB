# CoreS3-SE firmware changes

Copyright (c) 2026 Jonathan Yantis. MIT.

## 0.1.35 — 30 October 2026

- Color only the live Reference Watch SV-used count orange below10;10 and above
  remain green. Preserve existing stale/unknown colors, FIX/PPS/QUAL and OBS.
  Satellite count never becomes an OBS reset condition or timing permission.
- Exact app/prefix/tail readback and fresh boot passed. All94 live watch samples
  in a bounded30s check retained one OBS start while SV changed between9 and10
  (seven samples at9,87 at10). Offline pixel comparison changed only the active
  SV digits; stale/unknown, OBS and every other pixel were identical.

## 0.1.34 — 30 October 2026

- Add Reference Watch and Epoch Counter as faces3/4 in the existing face cycle.
  Reference Watch centers time above SV/FIX/PPS/QUAL and HOT nomination,
  unchanged-selection duration and independent report freshness.
- Track selected-root FIX+PPS observation on every accepted reference report.
  Missing fields, negative flags, expiry gaps and source-context loss reset it;
  duplicate/replayed reports never extend its original deadline.
- Epoch Counter adds milliseconds and a modeled-phase vernier, sampled P99
  sub-1us hold duration, mesh elapsed age and signed UTC birth/founder.
  Native bitmap typography, palette, stable40px controls and existing pages0–8
  remain; new presentations use pages9/10 and bounded `ui watch` diagnostics.
- Preserve current source/precision gates,500ms labeled readout grace and
  automatic brightness. Build/software checks and hardware observations remain
  separate from calibrated timing, optical display and failover qualification.
- Exact-device app/prefix/tail readback and fresh boot/display/fine checks passed;
  eleven live-model renders and both new completed LCD frames passed pixel CRCs.
  The first host watch check exposed a missing reply allowlist entry, corrected
  with single-row and wrong/missing-reply fixtures without another firmware write.
- A60s field check verified all reported arithmetic but retained a late source
  loss: current clock/GNSS/observation/epoch fields160/183, with proper withdrawal.
  Automatic recovery subsequently gave108/108 full receiver statuses following
  and107/107 live watch samples over a bounded90s check, without receiver faults.
  The operator subsequently confirmed physical cycling through all four faces
  and the HOME/PToB controls. The earlier failed availability population remains;
  stable availability and independent timing accuracy are not certified.

## 0.1.33 — 30 October 2026

- Raise the automatic brightness floor from32 to150 after operator feedback
  that the display was too dim in its normal position. Use150/160/192/224/255
  across the same ambient bands; retain filtering, hysteresis and gradual changes.
- Keep explicit manual32–255 control and the existing150 sensor-failure fallback.
- Exact-device app/prefix/tail readback passed. In59.7s of USB AUTO,
  174/174 statuses followed the same GM with zero faults or sensor/write
  errors; the low-light setting reached150 with PMIC24 readback. Physical comfort
  and the operator's planned off-USB test remain to be evaluated.

## 0.1.32 — 30 October 2026

- Detect the optional LTR553 and sample ambient light on the display owner at4Hz,
  with fixed1x gain/100ms integration and proximity disabled. Median filtering,
  EMA,20% hysteresis and asymmetric delays suppress short shadows and flicker.
- Use32–255 brightness steps, PMIC readback verification and150 fallback
  after stale/missing sensor data. Bounded console commands provide AUTO/manual
  control and raw-count/health diagnostics. Sleep retains final backlight control.
- Preserve the031 display grace, GM preference, timing receiver and mesh discovery.
- One exact-device app-only installation passed full application/prefix/tail
  readback and fresh boot. The integrated sensor reported92/05 with fresh data;
  manual32/128/255 settings matched PMIC codes21/24/28. In179.8s of AUTO,
  501/501 sampled statuses followed the direct GM, with zero sensor/write errors
  or P99 withdrawals. A real6-to3 count change caused verified64-to32 dimming.
  The narrow observed light range does not qualify a full dark/daylight sweep
  or optical brightness/readability; all nine renders and completed LCD CRC passed.

## 0.1.31 — 30 October 2026

- Retain clock numerals and sampled P99/quantile readouts through gaps shorter
  than500ms, with amber LAST/CACHED labels. Repeated invalid snapshots cannot
  renew the grace. Clock retention remains bounded by the last physical delivery;
  original TZ expiry, timing permission, lock, source pulse and scope fences remain.
- Give an authenticated GM10s of rediscovery before relay fallback and180s to
  establish sustained precision. These target-local selection preferences never
  extend an expired timing model or permit admission from a stale locator.
- Console reports displayed P99 visibility, cached frames and readout withdrawals.
  Offline boundary/scope tests and finite device observations remain separate
  from RF, failover and UTC accuracy qualification.
- Exact-device app-only installation passed full actual app, protected-prefix,
  final-sector tail and fresh-boot checks. In one179.3s normal-RF capture,
  1,901/1,901 sampled statuses followed the direct GM with no faults; after
  warm-up, P99 had zero display withdrawals and one observed cached frame.
  All nine live-model renders and a completed LCD frame passed pixel CRCs.
  This is finite internal/framebuffer evidence, not optical or failover acceptance.

## 0.1.30 — preserve frozen shadow trials through quality changes

- Finite live observation on 30 October 2026: sampled P99 <1us HOLD ≥1.75 s
  (128 trials, one unknown). At a 103.4 s frozen-run age, the latest 128 BLE
  witnesses had P95/P99/MAX 909/936/948 ns, with 165 gaps >250 ms whose interiors
  remain unmeasured. These are sampled shadow diagnostics, not delivered HOLD
  performance or independently measured UTC/output accuracy.
- Apply sub1us qualification when starting a shadow trial, then continue scoring its frozen candidate through later RMS/lock changes. Do not censor tails by cancelling the trial when production quality worsens; retain scope invalidation, original run timeout and explicit witness-gap/unknown outcomes.
- The0.1.29 device auto-joined and received current CDT metadata, but its finite live check exposed repeated quality-triggered shadow aborts. Preserve that observation alongside the correction and its regression.

## 0.1.29 — automatic local mesh and current TZ admission

- Learn the local group ID/epoch from a keyed canonical G1 locator or authenticated GM4 ANN, using the provisioned trusted PSK without a group-ID allowlist. Start with no selected group; permit another local context after actual source loss and radio cleanup, with a fresh nonce-bound JOIN before timing returns.
- Replace old RC control/discovery length literals with the selected TZ codec constants (216-byte control,148-byte capability). Test actual admission, ordered DST/unknown updates, rejected foreign keys and mesh changes under ASan/UBSan.
- Name the alternate face TIME REGISTER because its digits follow the same leased local TZ as the instrument face. Preserve UTC fallback and all shadow/servo behavior.

## 0.1.28 — shadow workspace in PSRAM

- Allocate the bounded shadow/rate/horizon history in PSRAM before receiver admission, keeping it off the NimBLE host stack and out of internal static RAM.
- Preserve the0.1.27 app-only readback/prefix/tail success and its failed Bluetooth startup observation. Native memory/build checks do not replace a fresh exact-device boot and reception check.

## 0.1.27 — shadow holdover servo, sampled duration and local clock

- Bring forward the previously developed CoreSE0.1.18–0.1.26 receive-only GM7 diagnostics, preserving current builder retention hooks and the pinned receiver/calibration.
- Add a frozen120s BLE-trained rate-only candidate, bounded16/32s repeated-channel HOLD-rate choice and rolling128 absolute-error P95/P99/MAX. Witnesses score future errors without correcting a held candidate; tails and gaps remain visible.
- Share W12's sampled-prefix P99 duration estimator: at least100 witnessed trials per age, frequent2s trials and adaptive long probes, unknown outcomes and lower-bound semantics. This does not promote the candidate into delivered HOLD or promise continuous sub1us accuracy.
- Replace the main clock's relative offset with P99 <1us HOLD seconds; retain signed relative offset in Quality. Add Shadow as diagnostic6/6, console page8, preserving pages0–7.
- Auto-range shadow errors ns/us/ms/s at1000ns,1e6ns and1e9ns, with units in every label. Keep hold duration in seconds.
- Consume current GM4/JOIN5/RC8 coarse TZ with authenticated selected-parent ordering, original receipt expiry, DST updates, explicit unknown and UTC fallback. Keep UTC diagnostics, counter coordinates, GPIO6/7 and calibration unchanged.
- Add actual-renderer synthetic review fixtures, owner/servo/UI regressions and an offline native build. Hardware installation and accuracy acceptance remain separate.

## 0.1.26 — current ACTIVE nomination and separate self-report age

- Show HOT NOMINEE / CURRENT from the current authenticated ACTIVE report, independently of extra HOT self-report expiry or local precision qualification. Show SELF role/age separately; preserve nomination identity and unchanged timer through missing self-reports.
- Expose hot_nomination_current alongside the original self-report hot_confirmed diagnostic. True withdrawal, cleared nominee and ACTIVE report expiry remain distinct.
- The0.1.25 finite90s capture had208/209 current ACTIVE nominations and no timer reset, while self-reports were current97/209. ROLE reception improved to19new reports but self-report gaps remain, including21s. Preserve those limitations rather than relabeling self-reports as fresh.

## 0.1.25 — receive ROLE before ANN discovery

- Open the first protected window at41ms, after primary ANN38ms, to receive ROLE43ms before an ANN AUX can occupy that opportunity. Retain45/50ms ANN discovery copies, STATUS85–113ms and physical8ms/56ms guards.
- Version0.1.24 had197/197 current source snapshots but only2 ROLE receptions in90s; upstream journals confirmed successful ROLE transmission and reception by other GMs. Keep that incomplete observation.

## 0.1.24 — ordinary reports without precision gating

- Schedule ordinary ANN/ROLE/STATUS observation from the current finite source/model and recent authenticated physical reception, independently of raw sub-1us/one-second precision hold qualification. Precision validity is unchanged.
- Permit planning within25ms of the associated physical PA to accommodate host dispatch latency. Retain the8ms/56ms physical window guard.
- Version0.1.23 showed scan starvation while source time remained current but precision qualification was absent; retain its finite result.

## 0.1.23 — preserve completed diagnostic callbacks

- Decode complete nonstale passive reports before the protected-window scheduling filter. HCI callback time is not an RF timestamp; an already received ROLE/STATUS report can arrive after scan closure. Parent/RC9 selection keeps the existing filter.
- Count accepted callbacks outside the scan window; keep advertised TTL, replay and authentication unchanged.
- Version0.1.22 admitted both windows and advanced ANN155times in90s, but HOT refresh still had gaps during spontaneous upstream reset/selection changes. Retain that incomplete result.

## 0.1.22 — physical guard coverage

- End ANN/ROLE scanning at50ms and STATUS at113ms so a physical PA before the UTC slot boundary does not reject the complete management window. Preserve the8ms/56ms physical guard and original source/timing validity.
- Expose admitted primary/STATUS windows, physical PA phase and guard refusals in diagnostics.
- The0.1.21 finite hardware observation still had49.5s HOT report gaps and no admitted primary window; it is not accepted as a reception fix.

## 0.1.21 — cadence-aware HOT diagnostics

- Listen around actual GM7 ANN/ROLE32–56ms and STATUS86–116ms slots in each
  250ms UTC frame. A current admitted clock schedules passive reception only;
  fresh associated PA and physical start/end guards remain mandatory. Use50ms
  protected scan intervals so a complete management window closes before a
  primary-channel rotation; ordinary discovery/admission parameters stay intact.
- Accept positive authenticated ROLE=HOT as descriptive evidence alongside
  STATUS, preserving original4.5s expiry. An older cross-channel remote sample
  cannot replace newer peer state. One lost2s report does not clear the screen.
- Base HOT UNCHANGED on its selected identity/boot. Refreshes, report expiry and
  term/train changes do not restart that timer. Show explicit last-report age
  after expiry; nomination, reported state and timing authority remain separate.
- Test complete primary/AUX coverage for all actual STATUS slots and ROLE
  frames, pending-window health cancellation, original expiry, one lost report,
  stable timer pixels and real codec/HMAC. Timing precision validity is unchanged.

## 0.1.20 — receive complete management events

- Overlap adjacent12ms diagnostic phases at8/16/24/32/40/44ms. A full
  primary/gap/AUX event crossing one phase's closing edge can fit in a later
  phase; point-only calendar coverage was insufficient. Keep the final56ms
  boundary, quarter-frame shift, current clock gates and minimum200ms spacing.
- Exercise full2ms management events in the calendar test. Expose the most
  recently authenticated GD scope separately from accepted Reference data so
  unknown/current source mismatches remain diagnosable without relaxing scope.
- Preserve0.1.18/0.1.19 field outcomes separately. No source-permission,
  fine-counter, enrollment, GPIO or upstream firmware change.

## 0.1.19 — rotating diagnostic receive opportunities

- Fix the persistent management blind spot after timing lock: immediate post-PA
  windows repeatedly missed the GM7 calendar. Rotate12ms windows through
  8/20/32/44ms after a fresh associated PA and shift the management-frame quarter
  after a full cycle. Close by56ms, before the next62.5ms timing reception.
- Preserve original timing permission and healthy-current-clock gates,
  200ms minimum spacing, single-use physical captures and bounded cancellation.
  A pending window cancels on source loss or unhealthy clock; it cannot renew.
- Test the actual GM7 primary/diagnostic/capability opportunities against the
  receiver scheduler, plus pending-window, expiry, duplicate and reversal edges.
  Preserve the0.1.18 field gap and recovery observations as a separate outcome.

## 0.1.18 — GM7 diagnostics and actual epoch reporting

- Observe authenticated implementation3 ANN, STATUS, ROLE, GNSS and WITHDRAW on
  SID14 with canonical advertiser checks, 64-bit sequence counters, independent
  replay namespaces and original receipt expiry. Preserve legacy GS/GD reports.
- Show the actual signed epoch birth in UTC and elapsed time from a current,
  scope-matched UTC model. Keep local receiver uptime separate; missing current
  UTC remains unknown. Leadership terms do not reset the epoch's start.
- Distinguish an ACTIVE nomination from matching reported HOT readiness, expose
  normal capability STATUS reports, YIELD, explicit withdrawal and raw role
  blockers. Retain descriptive history without extending timing permission.
- Preserve the pinned receiver, recovery, scan budget and GPIO6/7 route. Host
  checks exercise the real codec, dispatcher, replay/expiry and UI withdrawal;
  native builds and hardware observations retain their own identities.

## 0.1.17 — acknowledged periodic owner closure

- Retire an exact periodic owner on the pinned SDK's successful HCI terminate
  return and live-handle removal. Preserve original return/quiet deadlines;
  create cancellation still requires its actual completion.
- Treat queued EDONE as an old-owner notification, preserve later owner epochs,
  and retry temporary SDK pool exhaustion until that notification frees it.
- Distinguish SDK error, late return and unclosed-ledger expiry in existing
  fault-site and bounded synchronization traces. Hardware acceptance is separate.

## 0.1.16 — relay lineage and pending-controller recovery

- Clear deferred source trials and retry state when the same relay authenticates
  a changed GM lineage, preserving the current request and explicit relay role.
- Bind late periodic-create completion to the frozen actual owner address and
  refuse reports once cleanup is queued. Preserve the original drain deadline.
- Exercise actual pinned receiver RC7 lineage/role and RC9 pending-create return
  paths alongside direct leader loss, boot and incarnation controls.

## 0.1.15 — complete fallback scope fencing

- Clear normal-source selection history and retry state at authenticated RC9
  takeover and fresh direct-root admission after an empty scope. Preserve the
  original authenticated lease and real controller drain deadlines.
- Verify fresh-boot and same-address incarnation changes with production native
  fixtures; learned parents and models are never restored from flash.
- Keep the pinned receiver baseline and target GPIO6/7 counter route.

## 0.1.14 — leader transition recovery

- Backport the tested W12 leader-loss/changed-incarnation cache fence to the
  pinned receiver. Retire old sessions, estimators, queued timing, locators,
  trial history and backoff; preserve explicit pins and exact counter routing.
- Clear current source-bound capability, residual and diagnostic scan state.
  Historical reports keep their original deadlines and remain advisory.
- Test loss boundaries, successful fresh new-leader admission, pins, repeated
  invalidation throttling and actual pinned-source overlay generation. Hardware
  failover and precision require separate exact-image/device observations.

## 0.1.13 — 2026-10-24

- Admit post-PA diagnostic windows for capture ages below 10 ms. The earlier
  4 ms gate could close before fine association and authenticated decode finished.
- Display native retained time as an amber HOLD estimate for at most 500 ms
  from the last actual physical delivery. Repeated polls cannot renew it.
  Current root/path, precision, offset and source pulse remain withheld.
- Keep source permission and all timing/admission deadlines unchanged. HOLD is
  presentation with unknown error, never current mesh time authority.

## 0.1.12 — 2026-10-24

- Protect periodic timing reception from optional diagnostic discovery: replace
  80 ms discovery windows with 12 ms windows at least 200 ms apart, started only
  after a new authenticated, physically associated PA less than 4 ms old.
- Preserve the original healthy-clock coverage requirement, passive scanning,
  callback receipt fences, source deadlines and all time-authority rules.
- Bind the new scheduling policy into inspected receipts. Diagnostic delivery
  and continuous timing availability still require separate hardware evidence.

## 0.1.11 — 2026-10-24

- Backport the shared AUTO RC9 recovery fix from `6ac2721` onto the existing
  `699f100` receiver baseline. A six-minute fallback lease no longer suppresses
  bounded passive normal BLE discovery and fresh admission.
- Preserve fallback time through root hints and failed admissions, and fence it
  only after successful normal admission. Preserve explicit parent pins and all
  original timing, authentication and expiry rules.
- Record the exact recovery patch and policy in inspected build receipts; retain
  CoreSE's existing fine route, clock coordinates and calibration policy.

Hardware acceptance remains an exact-device, separately recorded operation.
