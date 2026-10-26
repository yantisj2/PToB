# T096 changelog

## 0.15.188 — Ordinary RC source selection and passive ARM snapshot witness

Only an ACTIVE GM with the existing live JOIN source envelope advertises ordinary
connectable RC service. Followers retain GM management discovery, scanning and
local PPS/shadow readiness. Source loss, merge, yield or stop cancels an ordinary
JOIN through its existing callback drain; it cannot return a source-less ACCEPT.
Returning to actual ACTIVE restores ordinary admission. RC profile GATT, RC9,
relay and the functional/strict timing estimators are unchanged.

This diagnostic canary adds an explicit AAPCS assembly frame around the existing
source snapshot body. It records entry, post-call and saved callee registers and
traps a real mismatch before restoring or returning. It does not repair the
unresolved CPU corruption. The additional active frame is88B; original32B stack
arguments and return value are preserved. Native disassembly/CFI and actual
Cortex-M4 QEMU tests cover the production source/UTC path and synthetic offline
boundary violations. They do not reproduce full Zephyr/IRQ concurrency.

No retained allocation is added. After a previous crash has been durably saved
and read back, an active GM may reuse128B of the existing512B record for the
witness. Crash schema8 retains64B thread and64B ISR windows; the lost thread
range is original bytes64..191. Normal RC, inactive and unavailable witnesses
retain schema7's full192B thread capture. Earlier formats remain distinct.
The existing `gm 00` query exposes enabled/armed/current stage, sequence, boot,
capture-epoch low32 and prior-crash blockage. Export never acknowledges a save.

Thirty-second ordinary fallback, three-second HOT bound, source/physical expiry,
GM/RC profile separation and all publication fences remain. This is offline
diagnostic preparation; CPU repair, sustained HOT and output accuracy are not
established by the image or tests.

## 0.15.187 — Recover from delayed GM processing snapshots

Carry the completion handler's refreshed same-domain bridge back to its caller.
A stale processing snapshot or unavailable post-MAC clock read rejects that
operation without permanently failing the GM owner or erasing its lineage and
peers. A rejected snapshot cannot publish output, renew a captured receipt or
extend a source deadline. STOP and withdrawal always suppress output, including
when processing time is unavailable; an invalid sample cannot create a fresh RF
negative timestamp. An unread completion stays queued for a later valid cycle.

The existing GM diagnostic page adds owner failure, time rejection count and
local functional/physical source category without increasing static state.
The quality page distinguishes an owner fault from ordinary synchronization wait.
Real capture-domain changes, source/map expiry, physical HOT qualification,
30-second ordinary election quiet and the three-second HOT bound remain.
Wire packets and normal RC receive/GATT/RC9 behavior are unchanged.

Host regressions execute the production completion/cycle/owner ordering with
an advancing clock, delayed completion, transient clock unavailability, old and
future receipts, original expiry, explicit stop, true epoch changes and genuine
counter exhaustion. Offline tests and native builds do not establish hardware
recovery or precision qualification; the previously observed CPU fault remains
unresolved.

## 0.15.186 — Durable broadcast GM following and asymmetric merge

- Ordinary non-HOT fallback and initial election wait at least30s of leader silence; designated HOT retains the fast3s path. Reuse retained control fields for replay floors/receipt derivation to preserve the normal RC static-memory budget.

GM profiles follow authenticated direct G1 through the existing RC matcher and
causal fitter without JOIN, ANN source leases or control refresh sessions.
Ordinary ANN/ROLE selects identity and full UTC lineage; self-ACTIVE ROLE can
independently stop a newer mesh and begin listening. Only the newer ACTIVE
yields. A progressing authenticated direct header inhibits duplicate output
before fitting; retained publication floors are scoped to source/train. Actual
matched original/correction expiry still bounds time and map exports. Local
GNSS/PPS capture and source maps continue across receive and role changes.

Prepare ordinary traffic up to100ms ahead, including the next frame; remove
the3–7ms lucky-poll gate, completed-primary copy dependency, ANN-only silence
clock, source-expiry RX shutdown, exact diagnostic-generation gate, redundant
250ms correction gate and additional5s shadow preparation dwell. Control RX
overflow counts a lost observation; GM precision record loss retires association
and reacquires through the same receiver. Real clock/driver faults stay distinct.
HOT nomination/ordinary STATUS echo and three immutable YIELD/WITHDRAW copies
remain. Normal RC admission/GATT, RC9, relay, USB and LoRa behavior is preserved.

See the [implementation guide](../../docs/t096/gm7-implementation.md) for timer
policy, replay limits and offline checks. Physical multi-GM acceptance and the
separate unexplained CPU faults remain open; no flash or hardware qualification
is implied.

## 0.15.184 — Simplified control admission and active-mesh discovery

S1 separates one causal peer/boot freshness fact from physical timing descriptor
admission. Fresh ordinary ANN/STATUS no longer renegotiate control on role,
lineage or source/train changes; a losing newer ACTIVE stops before physical
following. Keep exact descriptor lineage/token/generations, source expiry,
replay/terminal floors and real timing readiness. Delayed ANCHOR cannot erase
newer accepted control. Reduce pending nonces four→one and remove A1 reserved
frames, identity-bit copies, frozen retry UTC/residues/attempt state. Diversify
one immutable ordinary copy per frame across45/50/55ms, with the existing three
controller contexts and one bounded response arbitration. Ordinary nomination,
next STATUS HOT and three immutable YIELD/WITHDRAW copies retain their guards.
Pin enabled GM scanning to37 without phase-driven restarts. Add one bounded,
owner-validated USB YIELD request for an already announced, fresh ordinary HOT;
equal-quality nodes can yield without inventing quality differences. DC/RC and
stale source/scope/nominee requests remain fenced; command acceptance is not RF
completion.

Add authenticated heard-only peer display with unknown timers and distinct
fresh-control/descriptor facts, plus bounded RX/TX/event/merge/source records in
the existing journal. Preserve ordinary RC/client GATT/RC9, relays, USB, LoRa and
the .181/.182/.183 fixes. Production-body offline tests and native inspection
remain separate from actual RF convergence/HOT/events, unexplained CPU faults,
endurance and calibrated UTC/output acceptance. See the S1 amendment and GM7
implementation guide for exact scope and reproducible checks.

## 0.15.183 — Sample source processing time after its coherent input

Copy the GM7 clock metadata and physical source map together, then refresh the
paired processing clock under that same bounded lock before evaluating ages.
A PPS or RMC published after the earlier cycle timestamp could otherwise look
like future input and reset qualification despite being current. Return the
validated bridge to the caller; failed or changed clock domains remain fenced.
Keep original GNSS/PPS timestamps, freshness horizons, dwell and HOT/stop gates.
The focused regression fails on the original production ordering and passes
with the correction, using synthetic kernel/capture interleavings. This does
not establish the exact field withdrawal trigger or resolve CPU corruption.
The offline crash decoder also stops before CFI when the original or recovered
PC lies outside allocated executable ELF sections; raw registers remain intact.

## 0.15.182 — Reopen PT from a fresh GM control grant

Publish the GM7 grant, anchor, roster and bounded resume atomically. A control
withdrawal previously left PT stopped after a later valid local election or
authenticated follower contact. Separate this fast mailbox operation from HCI
consumption and preserve service stop, fatal/resource, original expiry and HOT
handoff precedence. Fence old or in-flight PT output and LoRa/shadow exports
against the current full binding and original horizons. First GM opt-in waits
for an unmanaged PT job to finish its exports. Two bits use existing input
padding; ordinary RC publication remains on its original path. Add named
`pt_stopped`, `pt_managed` and `pt_binding_matches` observations to `gm 00`,
without changing its existing keys or diagnostic storage. Focused tests execute
the real producer, PT, codec and native start/stop bodies with fake HAL/HCI.
These are offline behavior checks; actual RF delivery and the separate CPU
corruption remain unqualified.

## 0.15.181 — Allow initial election after an expired remote timing map

After the GM7 owner grants an initial election, use the independently qualified
local physical source to place its first ANNOUNCE. A retained remote lineage
without a live delivered timing map previously suppressed the reservation even
after all initial-election checks passed. Retain that lineage for birth, founder
and term inheritance. Fresh incumbents, rank, quiet/backoff, local GNSS dwell,
source expiry, HOT takeover and negative scheduling keep their existing checks.
A focused production-cycle regression covers first-ANN completion and these
guards with fake kernel/HAL/RF boundaries. This does not identify the separate
CPU corruption or establish hardware election or timing qualification.

## 0.15.180 — Retain a bounded CPU crash record and persist on reboot

Keep the existing256B fault capsule and add one512B retained record with original
integer registers, exact source/build/silicon/boot identity and up to192B of
known thread stack plus64B of ISR stack. Fatal capture uses only bounded static
storage and direct reads of known allocations, without flash or kernel locks.
Before radio startup, the existing bank0 owner appends one schema7 journal slot
and verifies payload plus commit before acknowledging RAM handoff. Latest crash
and ordinary-event pages are independently preserved across source upgrades;
no safe capacity or failed handoff leaves RAM pending and reports a separate
persistence error without blocking healthy GM startup. No bank1 or enrollment
writes are added. Read-only diag chunks0100–0107 export retained data; the offline
decoder requires the exact build and stops CFI unwinding at unavailable context.
Increase the GM7 ISR reserve from2176 to3072B for the fatal path; actual native
resource review remains required. This diagnostic change does not identify or
fix the unexplained CPU corruption, and does not qualify physical retention,
failover, UTC accuracy or hardware operation.

## 0.15.179 — Keep GM owner liveness on the CONTROL watchdog

Remove the GM7-only terminal check that counted queue and execution wall time
against 100 ms. The transport's existing 100 ms operation deadline starts at
worker execution; these were different budgets. Preserve serialization and cost
diagnostics, the original 30-second accepted-CONTROL watchdog, source/RF expiry
and the original cleanup deadline. RC behavior is unchanged. A focused production
supervisor-to-watchdog composition retains stale-owner, expiry and error gates.
The field wall overrun's delaying call and separate CPU corruption remain unknown.

## 0.15.178 — Refresh processing time after GM receive

Refresh the same-epoch processing bridge after owner receive, which independently
samples time during decoding. Passing its older timestamp to the next REQUEST
operation could permanently stop a cold GM before its first request. Preserve
the captured RX time, original deadlines and all qualification checks. The
bootstrap composition now also runs with an advancing owner clock. The separate
CPU pointer-corruption faults remain unresolved.

## 0.15.177 — Bootstrap GM followers before a physical phase map

Allow an authenticated ordinary ANNOUNCE locator to place a bounded nonce
REQUEST without qualified local GNSS or an already selected G1 map. Treat its
primary/discovery-copy phase as a guess; it grants no positive output, STATUS,
source, phase or HOT authority. Freeze retry-placement UTC in the immutable
REQUEST job's existing storage and use its actual RF START at completion,
preserving the A1 residues and original nonce/source deadlines. Keep ANCHOR,
subsequent control and captured G1 admission independent. Production-body tests
compose actual causal bootstrap with the normal full-identity GRv3/G1 receiver,
including older retained self lineage, absent UTC, refusal and response loss.
Includes version176's normal JOIN and resumed-progress corrections and retains
version175's fault diagnostics. The separate version174 CPU fault remains
unresolved; these offline checks establish no hardware qualification.

## 0.15.176 — Preserve time-client initialization and resumed progress

Retain the GM7 server's incoming normal JOIN connection through its existing
MTU/discovery and authenticated time exchange. Previously its outgoing-control
policy could retire that connection on the first owner poll. Keep the original
expiry, release, shutdown and callback drain limits; ordinary RC services are
unchanged. On a fully drained GM resume, record one actual CONTROL handoff sample
and retire the prior completed close state and current authority flags. A dormant
publication could otherwise trigger the unchanged30s progress watchdog before
the first resumed publication, particularly after a long RC interval. Queries
and pending reset retries do not refresh this sample. The original watchdog
decisions, transition bounds and source validity checks remain. Version175's
register diagnostics remain; the separate version174 CPU fault is unresolved.

## 0.15.175 — Preserve hardware fault registers

Diagnostic-only normal GM/RC candidate: enable the pinned kernel's extra
exception information and retain supplied integer registers in the same64word
fault capsule. Format4 uses actual memory/bus/usage exception provenance and
checked ISR-stack bounds before reading the saved callee registers. Formats2/3
remain readable; controller/assertion failures retain format3. SCB values are
post-kernel-clear and cannot establish BFAR validity. The separate watchdog role
record and all source, radio, HOT, RC/GATT/relay and timing behavior remain
unchanged. This collects evidence for an unexplained hardware BusFault observed
with version174; it does not claim to fix that fault or establish acceptance.

## 0.15.174 — Retire refused GM reservations

Roll back a refused first-ANNOUNCE reservation before releasing its job, and
retire absent completion at the original end. Process measured results first;
retain possibly exposed lineage and fence stale callbacks. Consume each original
primary opportunity once. Allow25ms of CPU construction lead while preserving
RF windows, actual3ms native admission, immutable samples/expiry and handoff
deadlines. Include29ms in the negative-only phase enclosure. Add same-size local
reservation-stage/error/failure diagnostics and distinct native timing refusal
codes. Actual service/controller regressions cover refusal-to-success recovery,
late result processing, early-queue cancellation and explicit fake CPU delays.
Ordinary RC/GATT/relay and other clients remain unchanged. These offline results
do not establish hardware acceptance or calibrated accuracy.

## 0.15.173 — Bind GM resume and physical UTC samples

Bind the current capture epoch only when an explicit RC-to-GM transition has
drained its old owners. Keep running-worker epoch faults terminal. Stop cancels
pending reset/start; failed start retains ordinary cleanup ownership and error.
Pair physical functional UTC with its actual capture tick and project to a fresh
same-epoch bridge for phase scheduling. Preserve coarse fallback timestamps,
original source horizons, backstep fences and the independent60s GNSS dwell.
Version172 could reject cold RC-to-GM resume at epoch0, and variable capture
latency could appear as a false UTC backstep. Actual dispatcher/service/source
tests cover both failures, warm transitions, cancellation, expiry and errors.
Ordinary RC/GATT/relay, USB, LoRa, DC and PC contracts remain unchanged. Offline
tests and builds do not establish hardware acceptance or calibrated accuracy.

## 0.15.172 — Serialize the GM clock pair

Sample the kernel/HF bridge in a bounded IRQ critical section so scheduler or IRQ
preemption cannot turn a healthy HF domain into a spurious wide-pair ESTALE. The
100us enclosure, physical domain/epoch faults and original deadlines remain
unchanged. Restore the prior IRQ key on every path. No HOT policy or RC service
changes. Actual service plus actual HF-reader regressions cover preemption,
intrinsic excess latency, stale/invalid domains, epoch replacement and drain.

Version171 fixed initial asynchronous readiness, but a later worker could close
on an unprotected software clock pair. These offline checks do not establish
measured critical-section duration, RF behavior or UTC accuracy.

## 0.15.171 — Wait for the initial GM capture domain

Initial GM startup waits for the existing asynchronous capture owner to publish
its HF domain before arming diagnostics or attempting normal service. The
original boot deadline and capture timeout remain fixed; driver faults, failed
start, maintenance and poweroff cancel once and cannot revive later. Ordinary RC
startup and runtime profile switching retain their existing paths. Report pending
or terminal startup separately from active GM7 diagnostics. Version170 could
attempt GM startup before capture publication, return ESTALE, and suppress the
later start with its one-shot flag. The regression now composes actual main,
time-start, capture and GM7 service bodies with fake public driver boundaries.

New offline source/build review is required; earlier170 receipts are historical.
No RF, output-continuity or calibrated UTC qualification follows from this fix.

## 0.15.170 — GM7 offline engineering

Replace GM-profile HOT/coordinator GATT policy with implementation2 connectionless
STATUS/ANNOUNCE, causal REQUEST/ANCHOR freshness and local HOT preparation. Apply
frozen A1 discovery and C1 provisional birth/exposure semantics. Keep normal
GM-to-RC JOIN/G1/GRv3, RC7/UCS1/RC9, clients, USB, LoRa RC8, DC and PC contracts.
Add scoped GM peer ROLE timers, local owner timers, bounded native complete-event
management and prospective G1 phase arbitration. Historical GM5/6 is selected
only by the explicit regression build option.

Validation and unverified operating assumptions are recorded in the
[GM7 implementation guide](../../docs/t096/gm7-implementation.md). Offline results
and native images do not establish RF delivery, physical takeover timing,
endurance or calibrated UTC accuracy. Hardware review remains separate.

Detailed release and validation notes, moved from the T096 README and build guide.
Entries describe their source version at the time; older policies, deadlines and
candidate labels are historical, not current operating instructions.

For current behavior, use the [T096 README](README.md),
[GM operating guide](../../docs/t096/gm-operations.md),
[build guide](../../docs/t096/build.md) and
[diagnostics](../../docs/t096/diagnostics.md). The
[project changelog](../../CHANGELOG.md) retains the wider release chronology,
including versions not repeated here. Test results do not qualify hardware;
failures, missing observations and unknown accuracy bounds remain part of each record.

Copyright (c) 2026 Jonathan Yantis. Original documentation: MIT. Third-party
patches retain their [source notices](NOTICE.md).

## 0.15.169 — Preserve PPS phase in the UTC basis

The source UTC owner now selects a whole-second offset from its coarse UTC
observation. Previously it froze the arbitrary UART/servo fraction into the
authenticated offset, shifting every downstream UTC timestamp even after the
PPS-relative map converged. Nearest-second selection refuses exact half-second
ties and overflow. Existing offset immutability, contradiction/source fences,
map availability and original deadlines remain. Unknown PPS/UTC association
stays unknown; this arithmetic fix does not certify the absolute second label
or GNSS/GPIO/RF accuracy. GM election and role policy are unchanged.

## 0.15.168 — Report actual GNSS HDOP

GD2 adds checksum-valid GGA HDOP in dimensionless hundredths to the reported
GNSS status and unchanged election quality. The 90-byte record has a new version
and MAC domain; the decoder retains strict 88-byte GD1 support with HDOP unknown.
Blank, negative, malformed, overflowing or unsupported-precision HDOP stays
unknown without changing ordinary GGA/time acceptance. The field fits existing
clock-view padding. Metadata cadence, TTL, precision and source qualification
remain unchanged. The local clock query exposes GGA age and raw HDOP for audits.

## 0.15.167 — More frequent source diagnostic windows

GD and GS alternate in the existing one-second nonprecision update, publishing
a fresh GD report no earlier than two seconds after its last successful update.
This gives passive clients five times the GD exposure of the original ten-second
cadence. The 88-byte record, 30-second diagnostic TTL, advertiser count,
controller limits, precision packets and qualification/failover gates are unchanged.
Reception remains subject to the client's actual radio opportunities; retained
historical diagnostics are not current source evidence.

## 0.15.166 — Occasional source diagnostics

A compact authenticated GD1 announcement reports the serving root's actual GNSS
fix/PPS observations, qualification result, satellites and snapshot ages, plus
its unchanged election quality tuple. One existing nonprecision advertising
window per ten seconds carries the 88-byte report; other windows retain GS.
Precision packets, controller data limits and qualification/failover gates stay
unchanged. Clients bind the original report to root/boot/mesh epoch and retain
independent expiry/replay fences. Reported quality is descriptive, including
unknown accuracy and variance; DOP, antenna health and UTC traceability are not
measured. Host, native and field validation are recorded separately.

## 0.15.165 — Mesh epoch inheritance

One mesh source epoch is inherited through authenticated G5 STATUS6 and retained
through successive GM replacements, failovers, temporary source/transport loss
and runtime role changes. Only an unassociated first standalone ACTIVE creates
a new epoch. Local physical generations continue to fence qualification,
withdrawal, UTC translation and finite grants independently. Root, boot and
publication/train changes still fence old timing maps. The group key epoch is
unchanged. STATUS is now 164 bytes with a versioned MAC; upgrade participating
GMs together. Public G1/RC7/LoRa timing layouts are unchanged.

The production-body regression exercises eight successive replacements until
all original identities are gone, plus independent source requalification.
UTC-path tests use distinct local and mesh epochs. Host/native results and
finite hardware observations are separate; no phase-continuous handoff or
absolute UTC accuracy is implied.

## 0.15.164 — LoRa startup watchdog ownership

Version164 prevents the independent LoRa watchdog from interpreting startup
flash-scan bytes as live owner state. One boot-zero atomic readiness word outside
the reused scratch union is published after LoRa initialization and before worker
launch. Until then the observer returns before reading that union. The original
30s monitored-stall bound, terminal-first-cause rule and sticky hardware fault
remain. This adds one atomic word; native resource deltas are a separate check.
No timer, retry, controller, pairing or RF policy changes.

A composed actual scanner/LoRa regression reproduces the old pre-initialization
timeout and later START/EIO record with zero SPI transfers, using a synthetic
valid public journal record and explicit observer interleavings. It verifies the
new gate, initialization publication and unchanged post-initialization watchdog
bounds. The ownership gap and unchanged LoRa bodies predate163. The two observed
163 startup recoveries match this chain, but their erased prior scan rows are
unavailable; the exact physical triggering row remains an inference. This fix
does not address or classify the separate historical controller assertions.

## 0.15.163 — Pinned controller fixes and failure diagnostics

Version163 brings two pinned public controller fixes into generated copies of
ordinary native GM/RC, including explicit verbose builds. The Zephyr
[radio-disable fix](https://github.com/zephyrproject-rtos/zephyr/commit/355648a69f5137c68d86fe577ddb256e7da74d12)
resets timer PPI/DPPI routes before clearing SHORTS and requesting DISABLE.
The ZMK fork's [connection cancellation fix](https://github.com/zmkfirmware/zephyr/commit/10ba6d0cb38bc3d258775d27982f707599320085)
changes same-owner zero-exchange central/peripheral preemption from EBUSY to
ECANCELED. Forced-event protection and asynchronous completion remain. Full
public patches, Apache-2.0 provenance, input/output hashes and exact source
membership are recorded; the pinned SDK checkout is unchanged. DC/PC and
BabbleSim are excluded. Queue capacity, assertions, priorities, timers, pairing,
precision policy and RF configuration remain unchanged.

Actual-source host controls distinguish the old cleanup route and cancellation
behavior, retain ownership until an explicitly delivered fake DISABLED interrupt,
then run actual connection ISR cleanup and FIFO retirement. Missing completion
still reaches the original full-queue assertion. HAL/IRQ, LLCP/HCI, ticker and
mayfly boundaries remain models. These fixes require native and field checks;
neither test establishes the physical initiator of the source162 failures.

Version163 also improves diagnostics for the unresolved controller fatal.
On failed prepare allocation only, four previously unused counter words retain
current LLL owner/abort callback and existing preempt, mayfly and ticker state
under the same IRQ fence as the seven queued preparations. The two pre-existing
preempt callback objects move one-for-one; no healthy controller hook, new state,
queue capacity, timer, priority or protocol policy is added. Exact source/build
mode and CRC are required for decoding. A current cut does not establish why
retirement stopped. Native and physical acceptance remain separate.

A rejected optional health note no longer consumes its detail window or reported
counter changes. It retries through the existing one-second throttle and alternate
watchdog opportunities, normally every two seconds; admitted healthy detail keeps
its original60s cadence. This corrects reproduced diagnostic delivery loss, not
the controller fatal. Console admission does not acknowledge host delivery.

## 0.15.162 — Current-session health refresh and generation retirement

Version162 keeps authenticated current-session source health fresh through the
existing accepted clock map when a new HELLO echo is too wide to replace that
map. It rechecks the original record expiry with the old map and its drift;
map evidence, source expiry, finite grants and all timers remain unchanged.
Changed sessions, missing or aged maps cannot use this path. A production-wire
regression covers the refresh and negative cases in the ordinary G5 suite.
This fixes an offline reproduced source-refresh inconsistency matching a field
withdrawal class; it does not establish the cause of the separate silent peer
or qualify physical HOT durability.

Version162 also retires receiver generation floors when a former ACTIVE
leaves that role. Its own issuance counter and outstanding grant deadlines are
preserved; an unrelated new GM may start at a lower generation. Same-GM
HOT/COLD retries retain their replay floors. The regression composes actual
native JOIN admission, STATUS and signed management after the role change.

## 0.15.161 — PPS/HOT durations and shadow-page restoration

Version161 restores the continuous PPS duration on every GM precision screen
and the HOT duration for the standby, using the existing hold timestamps.
The GM Bluetooth beat icon is restored beside RMS. ACTIVE duration, COLD
reception and stream RMS remain visible. This corrects
the160 display regression. Dedicated shadow page6 again works for GM and RC
profiles under its existing source/serving-GM guards; GM precision page2 stays
observational, while RC precision sampling remains enabled. No protocol timer changes.

## 0.15.160 — COLD timing reception

Version160 lets every COLD GM follow the ordinary authenticated precision stream
using the existing passive leaf receiver path. COLD remains outside the selected
GM/HOT control pair and gains no successor grant or output permission by receiving
time. This supersedes the earlier no-PA COLD policy and removes the LoRa-TX-only
exception. Independent local GNSS qualification continues.

The precision screen always presents current stream RMS (or -- when unavailable),
with COLD STANDBY/TIMING SYNC status for an unappointed receiver. GM shadow-PPS
sampling is held off on precision page2 while troubleshooting. RC precision page2
and dedicated page6 in either profile retain intentional shadow sampling; stream RMS and shadow PPS
statistics are distinct. Timer, election and controller policies are unchanged;
the retained prepare-FIFO fatal remains unresolved.

## 0.15.159 — Fresh standby input before readiness checks

Version159 refreshes coherent direct PT input before the G5 HOT/ACQUIRING
readiness tick, preserving the PT owner's observation timestamp and expiry.
ACTIVE, COLD, RC and relay paths retain their existing timing update. This fixes
an offline reproduced stale CONTROL-input refusal; it changes no election,
takeover, timer or RF policy and does not resolve the open controller failures.
The normal HOT page is observational; only dedicated shadow page6 requests
and arms PPS sampling. This is not evidence of a controller-crash cause.
The separate stream-loss takeover draft remains evaluation-only.

## 0.15.157 — Ticker progress under competing reservations

Version157 orders force before age within the existing ticker class. A queued
connection restart could otherwise cause two competing ACL reservations to defer
to each other indefinitely. The focused actual-body regression reproduces this
under oversubscribed DLE reservations; it proves sampled callback progress, not
field causation or universal fairness. Cross-class precision precedence, timers,
pairing, RF configuration and source156 diagnostics remain unchanged. Native and
field qualification are separate gates.
Run the focused portable regression with
`python3 firmware/t096/tools/ticker_fairness_tests.py --deps artifacts/deps --out artifacts/ticker-fairness`.
It extracts queued production bodies from the pinned dependency; counter/compare
HAL, mayfly delivery and LLCP/LLL completion remain explicit fake boundaries.
The ordinary `tools/test.py` suite includes it.

## 0.15.156 — Quiet prepare-FIFO failure capture

Version156 reuses the existing prepare-FIFO observer only when enqueue fails.
Normal native GM/RC images retain the failed owner and seven queued owners/flags,
without successful-operation counters or periodic timing/controller streams.
Existing capsule schema3 replaces its latency/interrupted-frame region; source,
boot, assertion, PA status and CRC remain. Unavailable or incomplete cuts are
UNKNOWN. Queue capacity, assertions, timers, pairing and RF policy are unchanged.
This diagnostic candidate still requires native/field checks and does not fix
or qualify HOT durability. See [source156 decoding](../../docs/t096/diagnostics.md#prepare-fifo-failure-only-capture-source156).

Version156 normal native GM/RC builds include quiet prepare-FIFO failure capture.
Its generated ULL mode/hash is pinned and inspected separately from the explicit
verbose canary. Ordinary builds contain no success counters or periodic controller/
timing streams; DC/PC exclude the capture. Source155's patch identities and the
upstream scheduler policy remain frozen. The full host runner covers actual
MFIFO and retained-capsule fixtures; native static/stack/flash and exact-device
checks remain separate gates. See the [source156 schema](../../docs/t096/diagnostics.md#prepare-fifo-failure-only-capture-source156)
for UNKNOWN handling and the schema3 loss of secondary IRQ/thread-frame fields.

## 0.15.155 — Controller failure diagnostics

Version155 adds failure-only capture of the original peripheral latency-cancel
callback status before its unchanged controller assertion. Three more first-fault
journal records expose current connection/ticker counters and marks. Generated
controller copies use a separate pinned diagnostic patch; the upstream backport
and existing precision scheduling policy remain unchanged. No timer, retry,
pairing rule, periodic probe or retained storage is added. These diagnostics
classify failures; they do not establish a fix or durable HOT operation.
See [source155 decoding](../../docs/t096/diagnostics.md#controller-failure-snapshot-source155).

Version155 normal native GM/RC builds apply a separate hash-pinned controller
diagnostic patch to generated copies of ticker.c and ull_peripheral.c. The
upstream91430 patch, existing precision scheduling policy and SDK checkout stay
unchanged. Native inspection requires exact generated hashes and one compiled
copy of each of the three controller translation units; DC/PC exclude the new
fault diagnostics. Run with ordinary, nonoptimized Python so generator assertions
remain enabled. The focused production-body/decoder/generator checks are:

```sh
CC=clang python3 firmware/t096/tools/controller_fault_diag_tests.py --deps artifacts/deps --out artifacts/check-controller-fault
```

The full host runner includes these checks. Fault hooks use the existing fatal
PA lane and first-fault journal, documented in the [source155 schema](../../docs/t096/diagnostics.md#controller-failure-snapshot-source155).
Full native static/stack/flash checks, exact-device installation and bounded
observations remain separate gates. Passing fixture schedules does not establish
field ordering, a controller fix or HOT durability.

## 0.15.154 — Controller retirement snapshot

Version154 extends that first-disconnect snapshot with current controller/LLCP
scalars under the same short native IRQ fence. Three fault-only journal records
report handle matching, local procedure/state, PRT and expiry counters. No SDK
patch, reference change, periodic probe, queue or static storage is added.
A current handle is not a controller incarnation; an inactive slot cannot prove
HCI-buffer blockage. Timer, pairing and radio behavior remain frozen.

## 0.15.153 — Disconnect host snapshot

Version153 adds a fault-only internal host snapshot at the existing first
unretired-disconnect timeout. Host state and TX/deferred work flags occupy
existing fault-record padding and unused watchdog detail bits; no timer,
pairing, radio rule, storage capacity or console cadence changes. These separate
samples narrow the stalled boundary without proving its cause. Durable physical
HOT remains unqualified. See [snapshot decoding](../../docs/t096/diagnostics.md#disconnect-host-snapshot-source153).

## 0.15.152 — CONTROL failure classification

Version152 records the accepted CONTROL failure subtype and service error in the
existing watchdog capsule, without changing reset behavior, deadlines or storage.
It distinguishes stale CONTROL publication, terminal service error and expired
incomplete closure. This is diagnostic coverage, not a crash correction; physical
pair durability remains unqualified. See [capsule decoding](../../docs/t096/diagnostics.md#control-watchdog-detail-source152).

## 0.15.151 — Shared control work budget

Version151 shares the existing1s budget across G5 initiation and each CONTROL
pass's GATT work; ordinary RC retains100ms. G5 scan-stop retries share the existing
5s retirement bound. G5 inventory/control queues defer to original authenticated
expiry, while HELLO and legacy coarse-time guards remain. Delayed callback and
JOIN/STATUS tests cover renewal, aggregate work and original expiry. Physical radio fairness
and sustained pair durability remain unqualified.

Version151 shares the existing1s G5 duration across initiation and aggregate GATT
work per CONTROL pass. G5 scan-stop retries use the existing5s retirement budget.
Queued G5 STATUS and non-HELLO control records retain original session/signed
expiry instead of an additional500ms queue-age gate. Ordinary RC, HELLO, source
and precision checks remain unchanged. Public tests include delayed callbacks,
queue work, original expiry and reference retention; physical radio fairness
and sustained HOT durability still require field checks.

## 0.15.150 — Quiet ordinary builds and incumbent contact

Version150 keeps unsolicited timing and controller console streams in explicit
controller-diagnostic builds only. Ordinary images retain the bounded timing
journal, explicit queries and independent watchdog/fault capture. This reduces
formatting and USB work without changing protocol, source or ownership bounds.
COLD clocks use their existing sparse discovery opportunity to renew a live
known incumbent before considering unrelated inventory. A reproduced native
scheduler case previously let competing candidates consume the120s contact
window. New JOIN/STATUS proof is still required; an unreachable incumbent keeps
its original expiry and fallback. No timer, queue or authority is added.
The149 physical run failed with an unretired incoming connection; this diagnostic
reduction is not a demonstrated fix or proof that diagnostics caused the failure.

Version150 ordinary GM/RC builds also omit the unsolicited timing console stream.
The existing `--controller-diagnostics` option enables both timing and controller
streams. Explicit timing queries, the bounded journal and watchdog remain active
in ordinary builds. Compare exact ordinary and diagnostic images under the same
placement, roles and bounded observation cadence before attributing any failure
to diagnostic load; host and native checks alone do not measure that effect.

## 0.15.149 — Control budget and conditional grant ownership

Version149 unifies G5 management freshness, READY, contact, acquisition and repair
around one45s control budget. The original90s successor grant is conditional:
an ACTIVE issuer retains its outstanding control owner through that grant's
original expiry, even if a phase or peer-proof window expires. It cannot mint a
replacement while confirmation is pending. Matching confirmation completes the
original attempt; a confirmed unchanged tuple renews without repeating the initial
5s stability dwell. Changed boots, source epochs or publication require fresh
stability. Fast500ms probes, original1s records and all source/PT expiry remain
independent. No new timer storage, queue or ownership buffer is added.

The148 physical run met the GM receiver policy but failed HOT durability:78/78
cohorts over600s had no network/radio errors, with a longest complete ACTIVE/HOT
interval of53.938s. LoRa TX and RC correction progressed. The first readiness
horizon lapse remains unexplained; the subsequent10s repair timeout is confirmed.
Version149 is an offline candidate until separately built and observed on hardware.

Normal GM version0.15.149 uses [G5 ACTIVE/HOT/COLD](../../docs/t096/gm-standby.md).
Authenticated control maps each remote instant with its own original conservative
clock-drift guard; a long finite-grant deadline cannot shorten a separate record
TTL. G5 uses1s original control records and500ms probes with90s finite grants.
Source/PT validity, wire layout and stored state remain unchanged. Legacy control
records retain250ms.
Its outer coordination type19/domain5 is distinct from the preserved inner
AUTH_CONFIG5, legacy JOINv4 and RC7 timing. During a staged update, updated RC
images can receive legacy timing; enter GM only with the explicitly compatible
G5 population. Existing old GM control cannot mint a new finite successor grant.

## 0.15.148 — Passive LoRa policy for solo GMs

Version148 permits passive LoRa reception on a solo GM, including a solo
serving GNSS GM. Recent authenticated BLE participation suppresses GM reception
independently of brief PT/map loss: original10s remote membership, direct
STATUS contact (original120s), current proved/accepted JOIN, or original client
and demand leases. Local attempts and reported root/selected IDs are not contact.
Expiry is evaluated by the progressing serialized owner; no new lease or timer
is created. Silent passive listeners beyond their contact lease are unobservable.
RC reception and the sole configured GM transmitter are unaffected. Manual
`requested` remains separate from `rx_suppressed`; old LoRa UTC/coordination is
fenced immediately, while physical closure still requires every acknowledgement
within the original200ms drain. A stalled owner retains conservative suppression
until the existing watchdog recovery. The148 hardware observation verified this RX policy, but not durable HOT.

## 0.15.146–0.15.147 — Fair G5 servicing and failed HOT observations

Version147 uses one fair four-class G5 dispatcher, a shared500ms probe cadence
and1s original G5 control records. Legacy records retain250ms. The5s coordination
lease,15s finite grants and independent source/PT/replay fences remain unchanged.
Version146 formed physical HOT but lost source permission; delayed-owner wire
replays expose response traffic starving HELLO and exceeding retry capacity.
The147 host candidate passes those bounded schedules; its subsequent physical HOT failed. Durable physical HOT,
controller recovery and CONTACT reliability remain unaccepted. Matching GM
versions are required.

## 0.15.144 — Connection retirement and optional controller canary

Version144 issues normal connection closure once and retains the original2s
retirement deadline through disconnect, TX and public-call callbacks. Missing
retirement becomes a terminal service error for the existing watchdog; buffers
and references remain owned until real completion or reset. READY freshness now
shares the existing2s management lease, preserving source/PT validity and all
original message/grant bounds. An explicit diagnostic canary records controller
prepare FIFO contents in the existing fault capsule; it is off by default.
These changes do not establish the cause or resolution of the controller assert,
physical HOT durability, failover latency or UTC accuracy.
The [remaining acceptance plan](../../docs/t096/gm-standby.md#development-checkpoint-and-remaining-acceptance)
keeps admission, source qualification, HOT renewal, watchdog reset measurement
and fourth-participant testing as separate open checks.

Version0.15.144 adds `--controller-diagnostics` to the normal GM/RC build command.
It defaults off; DC/PC reject the option. The receipt, generated build identity,
watchdog binding and `controller_diagnostics:true` status distinguish this image.
It keeps the seven-entry controller FIFO and existing resource reserves. The
[diagnostic schema](../../docs/t096/diagnostics.md#controller-prepare-canary) documents the bounded
console summary and conditional retained failure snapshot. This is an observation
variant, not a controller failure workaround or hardware acceptance.

```sh
CC=clang python3 firmware/t096/tools/controller_diag_tests.py --deps artifacts/deps --out artifacts/check-controller-diag
CC=clang python3 firmware/t096/tools/gm_drain_tests.py --deps artifacts/deps --out artifacts/check-gm-retirement
CC=clang python3 firmware/t096/tools/health_tests.py --out artifacts/check-health-capsules
```

## 0.15.143 — Per-timestamp causal-map bounds

Version143 bounds each authenticated remote timestamp by its own causal-map
clock-drift distance. A future finite grant keeps its full conservative guard
without shortening a separate short message expiry. Original response clipping,
source/grant lifetimes, all cadences and repair dwell remain unchanged. A fixed
host delivery-burst regression checks continued HOT within valid map bounds;
physical RF durability and the controller preparation assertion remain open.

## 0.15.142 — Same-epoch candidate requalification

Version142 permits a newer non-ACTIVE inventory report to describe physical
requalification in the same source epoch. The withdrawal floor remains stored:
old/equal reports and same-epoch ACTIVE claims stay rejected until the existing
causal HELLO path supplies its independent proof. Inventory can nominate a peer;
it supplies no source map, HOT status or grant. No timer, state or resource changes.

## 0.15.141 — G5 ownership and dispatch

Version141 makes discovery windows start new work only. An accepted G5 transient
retains its original initiation cap (1s for G5 since151, otherwise100ms)
and5s resource expiry; JOIN/STATUS share
that original expiry, clipped by the current phase. One consumed initiation sets
the existing G5 retry opportunity, without a separate completion cooldown or
selected-anchor override. A fair pair dispatcher retains original message due
cadences and prioritizes an expiring response without restarting the ordinary
turn. CONTACT/ACQUIRE/HOT/REPAIR reuse the existing phase deadline. No stored state,
buffer, slot, source/grant lifetime or watchdog allowance is added. Host checks
cover software boundaries; physical durability remains a separate check.

## 0.15.140 — Fixed successor acquisition budget

Version140 gives a new successor acquisition one fixed45s budget, including the
existing5s stable-READY dwell. Contact remains30s; repeated exchanges cannot renew
either deadline. This allows more time for cold delivered-map convergence without
changing source, map, management, grant or repair gates. No role or state is added;
an unsuccessful selected candidate can occupy the pair15s longer. Physical
convergence and continued HOT renewal still require independent testing.

## 0.15.139 — CONTROL publication watchdog coverage

Version139 stops feeding after30s of stale CONTROL publication in either ordinary
profile, including a pending transition. A terminal service error or unfinished
closure past its original deadline stops feeding immediately on observation; the
existing30s hardware parent then resets the MCU. This adds no drain grace, state,
timer or RAM. Completed closures with historical deadlines and live COLD workers
remain valid. GPS/RF silence alone is not a worker failure. Production host tests
cover the decision; physical reset latency requires a separate hardware check.

## 0.15.138 — Change-driven source HELLO

Version138 restores the existing change-driven HELLO scheduling for new source permits
and epochs before the G5 return. It changes no source, map, grant or timeout bound.
The `network.control` diagnostic exposes existing management/pair deadlines, last
HOT withdrawal and receive latency/backlog counters without new stored state.

## 0.15.137 — Contact phase and causal anchor handoff

Version137 keeps one bounded contact phase before actual successor acquisition;
sparse COLD discovery cannot spend the PA acquisition deadline before appointment.
It also retains the original causal JOIN anchor across control promotion so a
delayed precision-owner job can acquire the selected root without fabricated timing.

## 0.15.136 — Consume latched precision close intent

Version136 prevents foreign inventory JOIN from briefly putting a serving G5
ACTIVE into passive timing mode. CONTROL consumes PT's latched close intent even
if the scope recovers between cycles, preserving the first resource-drain deadline.

## 0.15.135 — Missed inventory retry

Version135 applies the same5s retry policy to missed and completed inventory
opportunities while ACTIVE has no selected peer, retaining the original1s window,
100ms connection attempt cap and2s discovery cooldown.

## 0.15.134 — G5 ACTIVE/HOT/COLD coordination

Version134 implements [G5 ACTIVE/HOT/COLD coordination](../../docs/t096/gm-standby.md):
one serving ACTIVE, one appointed successor and a separate bounded COLD inventory.
Physical source qualification continues independently of coordination inhibition.

Finite successor grants, bounded repair/cooldown and paged `gm` diagnostics support
unstable/off-host testing. The sole requested LoRa transmitter may retain ordinary
BLE leaf timing while COLD; that exception adds no GM-control/HOT authority.
G5 has an explicit new outer control envelope; AUTH_CONFIG5 and RC7 timing are
unchanged. Host/simulator success does not qualify physical multi-GM durability.

The COLD receiver exception above is historical; source 0.15.160 supersedes it
with ordinary COLD reception.

## 0.15.132 — Bounded NSS/FEM launch commit

Version0.15.132 corrects an RF launch-availability regression: worker wakeup
after a completed SetTx was incorrectly used as the command's commit time.
The dedicated SPI1 owner now controls NSS through public GPIO, with no CS
owned by the SPI context. Successful asynchronous byte completion enables the
original360/450ms FEM cutoff and releases NSS in the same bounded GPIO/timer
fence. The later worker uses that commit upper bound; it never renews the timer.
No SPI transfer or wait runs in the completion callback. Precommit has a separate
finite360/450ms bound, the original metadata finish deadline is checked at commit,
and BUSY30ms/DMA10ms and unsafe-buffer retirement remain unchanged. Cancellation
or a rejected/failed transfer asserts reset before NSS can commit a partial
command. A known committed launch with no DONE at cutoff remains a terminal
fault; pending physical DIO capture still wins over delayed software callbacks.

The `launch` diagnostic contains `[budget_us,precommit_us32,FEM_start_us32,
NSS_commit_upper_us32,committed]`, all in the same boot-local monotonic domain.
Unsigned subtraction handles its32-bit wrap; these observations are not radio
TX-start timestamps or output-accuracy measurements. Stage9 first-fault records
identify precommit/deadline aborts. The existing512B journal scratch funds these
fields without increasing static RAM or reducing any reserve. Native inspection,
sanitizer/fake-boundary controls and actual TX progress remain separate gates.

## 0.15.131 — Foreign LoRa root constraint on GM eligibility

A live foreign LoRa root inhibits local ACTIVE/HOT unless current direct
matching-root BLE admission permits normal coordination. An immediate relay
parent cannot substitute for the actual GM. Self-root echoes remain rejected,
and original hold/withdrawal/source fences remain.

Version0.15.131 supplies the accepted physical-pair root to election as a
per-cycle negative constraint, without another static owner or reserve cut.
Timing-only root/epoch may inhibit; a matching companion boot is required for
eligibility restoration. Current authenticated direct JOIN plus direct captured
BLE map, or current direct GM control/service, permits ordinary source
readmission. The existing source/HOT/management/output fences still apply.
Relay identities, advertisements and expired scopes cannot grant eligibility.
The original LoRa hold is never renewed by metadata/query time. This policy
does not grant RC7 timing authority or prove exclusion across RF partitions.

## 0.15.130 — One-shot LoRa toggle and retained fault history

Version130 fires the LoRa toggle once at1.5s and consumes that press, while
intentional4s poweroff stays on other pages. Its schema6 journal carries the
primary fault through ordinary commits/page erases until a newer terminal
cause supersedes it, within the existing bank0 owner and RAM reserve.

Version0.15.130 carries the primary LoRa fault in schema6 bank0 records until a
newer terminal fault supersedes it. Original fault boot uses reserved words39/40;
ordinary event boot remains4/5. Reason flags0x400/0x800/0x1000 identify new fault,
carried copy and terminal primary. No extra append cadence or flash region is
used: the ordinary verified commit hands off evidence before obsolete-page
erase. Startup restores the newest valid copy. App changes keep existing
source-bound journal initialization; preserve evidence before updating.

## 0.15.129 — Launch and drain scheduling

Version0.15.129 keeps the original200ms drain bound, temporarily wakes/prioritizes
only inhibited closure above priority10 console/diagnostic work, then restores
normal priority11. BLE owner priorities remain unchanged. Before retiring a
SetTx as phase3, its full340/420ms chip timeout must still fit the original
360/450ms FEM window; a late unretired launch closes/counts/journals EINTR and
keeps existing retry cadence. A previously latched DIO keeps its physical capture.
Real phase3 timeout, SPI and unretired-DMA faults remain terminal. Stage8 records
use word4 for the original FEM-enable millisecond so launch age remains visible.

## 0.15.128 — Stalled-start cancellation

Version0.15.128 advances the driver cancellation serial before a watchdog trip.
A stalled start returning from capture/resource acquisition must fail its
original reset-release fence. The production-driver regression fails against
127 and passes with128; this does not fabricate a physical worker stall.

## 0.15.127 — Durable LoRa fault observer

Version127 captures the first LoRa fault through the existing event journal and
uses the hardware watchdog for real transport faults or a frozen owner. Unsafe
DMA is retained until MCU reset. The record distinguishes actual closure and
journal success/failure; a conditional retained reset capsule carries the cause
across a same-image watchdog boot. Source/RF silence alone never requests reset.
The journal's startup scan scratch funds this observer without added static RAM.

Version0.15.127 adds a durable LoRa fault observer. The existing diagnostic scan
scratch has a separate lifetime after initialization, before the LoRa thread is
created. No stack/controller/USB reserve is reduced. The `lora` response adds
`watchdog`, `fault_record` and `owner`; their layout is documented in the
[RC8 guide](../../protocol/rc8/README.md). Real transport errors remain latched
until physical watchdog reset; no unretired DMA buffer is reused. Journal writes
use only existing bank0 at0xe1000..0xe2fff, with normal radio-aware admission,
readback/CRC/commit and the original finite maintenance deadline (40s in145;
older images retain their own deadline). App-only updates and all672
protected-block comparisons retain their original scope; journal storage is
already outside that protected population. Conditional retained-RAM evidence is
not a power-loss durability guarantee. Physical recovery/retention and live
stack/coexistence remain separately measured exact-device gates.

## 0.15.126 — Bounded SetTx BUSY recovery

Version0.15.126 adds BUSY-high deadline recovery only for an unfinished SetTx
with healthy retired DMA. Other BUSY/transport/completed-launch faults retain
their bounds. Hardware diagnostics use existing padding for BUSY abort count
and last opcode; `sizeof(r2_lora_hw_diag)` remains52. Page7, link-fit logic and
all original reserves remain unchanged. The source change requires new exact-
device functional/coexistence checks before field acceptance.

## 0.15.125 — Prior correction validation and link diagnostics

This source corrects prior-correction validation ordering: the full immutable context
is populated before validating a carried prior event. Version124 discarded those
corrections despite mapped physical TX events. It remains an unqualified canary integration until its
separate native/resource and authorized exact-device checks pass.

Page7 shows source/parent diagnostics and an RX-only LoRa link affine residual
RMS from4..16 matched physical radio event pairs. COLLECTING/NO TIMING identify
missing local fit data. The metric measures relative consistency; it is separate
from advertised parent/BLE quality and grants no absolute accuracy bound.
Reset-aborted software launches consume their possibly transmitted sequence,
retire fully and restart at a bounded cadence; real completed-launch timeouts
remain faults. Timing and companion hardware cutoff durations are unchanged.

Version0.15.125 keeps wire3 bytes/domains while correcting prior-event preparation,
adding bounded ambiguous-launch recovery and page7 RX link affine residual RMS.
`lora_rc8_tests.py` includes an independent high-precision Decimal residual oracle
over synthetic corrected physical pairs; this is not a hardware accuracy result.
The compact LoRa view stays48B; the RX-only sufficient statistics add56B inside
the boot-role union. Complete native ELF and exact-device stack checks must still
meet the original4096B added-static ceiling without reserve reductions.

Version125 populates the immutable context before validating a prior correction;
version124 discarded that correction because its not-yet-populated context failed
validation. A production-service test now feeds two service-generated physical
timing packets through the receiver and requires an accepted prior event.

## 0.15.123–0.15.124 — RC8 wire 3 integration

This integration summary accumulated through source 0.15.146; later fixes are
recorded individually above.

The [RC8 wire3 integration](../../protocol/rc8/README.md) adds exact actual-GM or
selected authenticated locked-BLE follower exports, an independent lower-quality functional
estimate, page7 controls and a bounded priority11 radio worker. BLE remains
preferred while usable. A transmitting follower needs no local PPS; an actual
GM retains its own PPS/source gate. Timing packets carry the full root identity and source epoch; a missed companion
still permits functional acquisition. Companions describe boots, parent and measured
quality about every120 seconds and on significant changes. Cold passive freshness and total uncertainty are UNKNOWN;
optional independent admission improves assurance. RC8 grants no RC7 timing,
GM promotion, RC9 or USB authority.

The version 131 election constraint is recorded separately above. This
integration was an unqualified canary pending its own native/resource and
authorized exact-device checks.

Normal0.15.146 builds include the [RC8 wire3 integration](../../protocol/rc8/README.md).
The default transmitter factory is zero, so LoRa hardware does not start. An
explicit normal-only `--lora-transmitter-factory` takes a16-digit exact factory
identity verified against ignored T096 registry metadata. It is included in the
private build/watchdog identity; a display alias grants no permission. The
configured transmitter requests TX ON only in GM profile, including when it is
a locked authenticated GM-profile follower without local PPS. An actual serving
GM retains its qualified own PPS/source requirement. Every other enrolled T096 requests passive
RX ON. TC01 in RC profile cannot transmit or receive its own stream.

The `lora` query exposes full context/root/boot/source epoch (unknown boots stay zero
until a matching companion), current and corrected
sequence, physical captured event, original source envelope/map generation,
accept/reject/replay/cache/missing-map counters, UNKNOWN uncertainty, selected
source, actual worker stack unused bytes and bounded closure status. `lora 00/01`
controls only the configured role; page7 fires the same volatile request once at1.5 seconds. The claimed press
cannot poweroff if held longer; intentional4-second poweroff remains on other pages. Functional UNKNOWN
freshness is explicitly allowed as secondary time when normal BLE is unusable;
it grants no normal-source or future RC9 authority. Host/source/worker/radio
fake-boundary tests run in the full runner. They do not operate hardware or
qualify RF, coexistence or output accuracy. Native linked RAM must prove the4096B
added-static ceiling; existing owner/controller/USB/stack gates remain.

Wire3 retains80-byte timing packets, adds128-byte descriptive companions roughly
every120 seconds plus coalesced events, and carries root identity/source epoch
in every timing packet. Companions never renew a timing sample. Current-context
loss withdraws the sample; stale diagnostic floors cannot withdraw newer timing.
The timing cutoff remains340ms chip/360ms FEM; companions use420ms/450ms.
After packet preparation the radio owner rechecks the original timing deadline
immediately before FEM enable. Missing parent GNSS/PPS evidence remains UNKNOWN.

## 0.15.122 — Palette framebuffer

The palette/DMA arrangement recovers 7,680 bytes of static RAM.

Version 0.15.122 replaces the 25,600-byte RGB565 framebuffer with 12,800 bytes
of palette indices and one persistent 5,120-byte DMA buffer. The fixed palette
preserves all 17 original RGB565 colors; there is no second full conversion
frame. Five pixel submissions share the original 250 ms deadline and keep the
public synchronous driver's completion/abort ownership. A failed batch ends
the frame before another conversion or submission. The 4 Hz ordinary and 8 Hz
RF-page cadence, UI stack and all memory/controller inspection gates remain.
`framebuffer_tests.py` checks exact reconstructed pixels against the production
drawings' direct RGB565 sink, bounded conversion and every command/payload
failure boundary; it is also included in the full host runner. Matching physical
display, BLE/USB service and stack observations remain exact-device gates.

## 0.15.110 — Failed-parent retirement

Version 0.15.110 adds failed-parent retirement to the production selector and
durability tests. Run `rc_selection_tests.py` and `rc_durability_tests.py` using
the commands above. These cover loss deadlines, empty neighborhoods, fresh
admission of returning identities and actual-owner drain boundaries with fake
hardware. They do not qualify physical failover.

## 0.15.109 — Finite RC passive search

Version 0.15.109 makes RC passive search finite (10 ms per 20 ms interval),
retaining the degraded-parent 5 ms/20 ms watch. The focused test compiles the
actual application scan parameters and pinned primary scan preemption callback:

```sh
CC=clang python3 firmware/t096/tools/rc_passive_scan_tests.py --deps artifacts/deps --out artifacts/check-passive-scan
```

`--legacy-continuous` is an expected-failure parameter control, not a controller
queue-overflow reproduction. A canary must separately retain exact MCU boot,
source, reset reason and fault capsule through the RC-to-GM transition. No
controller assertion, queue capacity, RAM margin or cleanup deadline is changed.

## 0.15.108 — Serialize incoming GM admission

Version 0.15.108 holds the native GM G1 advertiser closed while its single
incoming transient JOIN slot owns a connection. The pinned public
`BT_LE_ADV_OPT_CONN` already includes one-time behavior; the owner now waits for
actual slot release before restarting advertising. The production discovery
suite covers retained ownership after a disconnect request and actual callback
release. `--legacy-busy-advertiser` is an expected-failure control. No static
state or deadline is added. RC marker callback tests preserve original ATT errors.

## 0.15.107 — RC admission envelope and MTU completion

Version 0.15.107 uses RC7's coarse replay interval consistently for initial
GM admission and renewal, while preserving the normal GM/control adapter's
strict 100 ms half-width. Both adapters share record validation and expiry math.
The RC MTU completion regression verifies that a previously negotiated usable
MTU can recover Unlikely Error only on a live link. No new static state or
retry is added. Both focused suites are included in the full host runner:

```sh
CC=clang python3 firmware/t096/tools/rc_gm_admission_tests.py --deps artifacts/deps --out artifacts/check-gm-admission
CC=clang python3 firmware/t096/tools/rc_mtu_tests.py --deps artifacts/deps --out artifacts/check-rc-mtu
```

## 0.15.106 — Signed serving-GM discovery

Version 0.15.106 adds signed serving-GM discovery to the RC owner. Compact
locator storage funds a fixed 40-byte failover state and queue arrival timestamps;
the static RAM inspection minimum remains unchanged. Run its production selector
and ingress checks with:

```sh
CC=clang python3 firmware/t096/tools/rc_gm_failover_tests.py --deps artifacts/deps --out artifacts/check-gm-failover
```

## 0.15.105 — Native GM scan-stop and connection drain

Version 0.15.105 bounds native GM scan-stop contention to a fixed 500ms
recovery interval, with public stop retries at most every 20ms. Success must be
observed before releasing scan ownership. A duplicate connection whose close
races a natural disconnect retains its reference until its actual callback;
Command Disallowed/Not Connected responses cannot alone release it. Missing
callbacks originally retained a two-second failure deadline; version145 uses the
negotiated supervision plus5s and a shared40s outer cleanup budget. Terminal transport
diagnostics are saved without needing a pre-fault console poll. These checks
do not extend source, authentication, election or maintenance deadlines.
Bounded native transport indices use eight-bit storage to retain the unchanged
1,024-byte static RAM inspection minimum with the additional retry state.

```sh
CC=clang python3 firmware/t096/tools/gm_drain_tests.py --deps artifacts/deps --out artifacts/check-gm-drain
```

## 0.15.100–0.15.104 — Bounded selection storage and recovery

Version 0.15.104 narrows three bounded route/admission indices without changing
their values or reducing diagnostic, stack or transport capacity. Version
0.15.103's eight-byte scheduling hint crossed a native alignment boundary and
failed the unchanged 1,024-byte static margin gate; it was not deployable.
Version 0.15.103 allows an explicit parent identity to select a direct GM as
well as a relay, with no additional static state or change to automatic selection.
Version 0.15.102 adds a source-unavailable scheduling hint to 0.15.101's
preserved retry policy and pinned AUX observations. The hint adds eight bytes
of state; native inspection still requires at least 1,024 unallocated bytes.
Version 0.15.100 introduced transport hold and earlier RC7 renewal.
Run the combined production policy and publication regressions with:

```sh
CC=clang python3 firmware/t096/tools/rc_relay_renewal_tests.py --deps artifacts/deps --out artifacts/check-relay-renewal
CC=clang python3 firmware/t096/tools/rc_source_wait_tests.py --deps artifacts/deps --out artifacts/check-source-wait
CC=clang python3 firmware/t096/tools/rc_transport_hold_tests.py --deps artifacts/deps --out artifacts/check-transport-hold
CC=clang python3 firmware/t096/tools/initiator_aux_tests.py --deps artifacts/deps --out artifacts/check-initiator-aux
```

The full host runner includes these checks. A retained periodic train carries no timing
corrections while the existing timing gates are invalid; it cannot extend a
child lease, source, map or authentication deadline. Build/native inspection and
actual protected-region/application readback remain separate hardware gates.

## 0.15.99 — Maintenance reset enumeration

With 0.15.99, explicit maintenance still drains owners and accepts commit inside
the original READY deadline. The subsequent hardware watchdog reset can take
30 seconds. Exact-device update tools must use a finite post-commit enumeration
allowance that accommodates this latency. Do not retry prepare/commit or extend
their original deadlines while waiting for the bootloader.
