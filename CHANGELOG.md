# Changelog

Copyright (c) 2026 Jonathan Yantis. MIT.

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
