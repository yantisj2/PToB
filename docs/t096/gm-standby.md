# T096 G5 ACTIVE, HOT and COLD

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

Version161 restores the continuous PPS duration on every GM precision screen
and the HOT duration for the standby, using the existing hold timestamps.
The GM Bluetooth beat icon is restored beside RMS. ACTIVE duration, COLD
reception and stream RMS remain visible. This corrects
the160 display regression. Dedicated shadow page6 again works for GM and RC
profiles under its existing source/serving-GM guards; GM precision page2 stays
observational, while RC precision sampling remains enabled. No protocol timer changes.

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

Version159 refreshes coherent direct PT input before the G5 HOT/ACQUIRING
readiness tick, preserving the PT owner's observation timestamp and expiry.
ACTIVE, COLD, RC and relay paths retain their existing timing update. This fixes
an offline reproduced stale CONTROL-input refusal; it changes no election,
takeover, timer or RF policy and does not resolve the open controller failures.
The normal HOT page is observational; only dedicated shadow page6 requests
and arms PPS sampling. This is not evidence of a controller-crash cause.
The separate stream-loss takeover draft remains evaluation-only.

Version157 orders force before age within the existing ticker class. A queued
connection restart could otherwise cause two competing ACL reservations to defer
to each other indefinitely. The focused actual-body regression reproduces this
under oversubscribed DLE reservations; it proves sampled callback progress, not
field causation or universal fairness. Cross-class precision precedence, timers,
pairing, RF configuration and source156 diagnostics remain unchanged. Native and
field qualification are separate gates.

Copyright (c) 2026 Jonathan Yantis. MIT.

Version0.15.134 introduces G5 coordination for the ordinary GM profile. Since
version0.15.151 the implementation retains147's500ms G5 probes and1s original control records with the
shared45s control window and90s finite grants, one
original admission lifetime, one selected-peer retry policy and fair pair
dispatch, retaining causal timing handoff, latched PT closure,
GM watchdog coverage and the fixed45s successor acquisition budget. This is
an implementation and host-test contract, not hardware acceptance or a claim of
UTC/output accuracy. RC7 clients, USB, GNSS qualification, BLE capture and LoRa
wire3 retain their separate contracts. DC remains a separate flash-only image.

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
until the existing watchdog recovery. This is an offline candidate, not durable
HOT or hardware acceptance.

## Owners and compatibility

The serialized network owner has two control slots: self and one selected peer.
A separate inventory holds up to three remote full identities/incarnations.
An ACTIVE appoints one ACQUIRING candidate; a healthy ACTIVE/HOT pair keeps its
appointment when better-ranked third/fourth clocks arrive. Other capable clocks
are COLD. Candidate inventory is descriptive and never grants timing permission.

G5 uses outer control type19 and MAC domain `PTOBG5C5`; older outer type17 peers
cannot negotiate a G5 successor grant. The inner authority record remains
AUTH_CONFIG5. JOINv4 and RC7 timing remain compatible: an updated RC can receive
legacy serving timing during a staged update. Legacy coordination cannot supply
a finite G5 grant. Authenticated negative LoRa root evidence can still inhibit
output; it supplies no positive root/HOT authority. Do not infer G5 capability
from AUTH_CONFIG5 alone or run a mixed-generation GM pair as an accepted system.

After causal JOINv4, G5 STATUS type19/subtype0,1,2/revision5 uses160 bytes and
MAC domain `PTOBG5S5`. It binds sender/receiver full identities, both boots, both
current JOIN nonces and key epoch. Its request/response/final confirmation shares
the original transient resource deadline:5s from candidate selection or incoming
connection, shortened by current CONTACT/ACQUIRE/REPAIR expiry. GATT readiness
starts no later independent5s allowance. No STATUS phase renews it. Matching G5 STATUS and appointment allow the client to promote after its final
phase2 send is locally accepted; the server promotes only after receiving phase2.
A lost final phase can therefore leave one side temporarily promoted. The
already-owned outgoing/incoming transient survives poll completion; its
original5s deadline and original acquisition/repair/control gates bound
cleanup; local queued acceptance does not prove remote delivery. Actual captured
map/source/COMMIT acknowledgement gates remain necessary. Callback ownership must
drain before an object is reused. Ordinary RC admission needs no G5 STATUS.
The G5 owner determines passive timing mode before JOIN admission publishes its
client mailbox. A foreign root's coarse JOIN evidence cannot briefly demote a
serving ACTIVE into passive precision acquisition while awaiting G5 STATUS.

The producer retains its original bounded JOIN timing anchor while COLD. Since
160 the service publishes that passive client mailbox to PT under its original
bounds. Reception grants no HOT or serving authority. After selected control
promotion, PT may use a current causal anchor it did not previously observe. The selected remote root/boot must match, as must
known source epoch/train. A current own ACTIVE or unrelated anchor cannot enter
that bootstrap path. A previous matching anchor may survive a foreign/no-source
mailbox update only through its original bounds. Replay-age evidence permits
acquisition; an expired source permit cannot supply output. A usable direct grant
replaces bootstrap under the existing scope and captured-map gates.

PT withdrawal latches an original resource-drain deadline. CONTROL must consume
that intent and close its old native resources even if the same source scope
recovers between CONTROL cycles. Recovery cannot cancel or renew the intent.
Repeated closes retain the earliest original2s deadline, including a tighter
client quiet deadline. A still-owned controller/capture object faults at that
deadline; only its actual callback/drain releases ownership, and the fault stays
recorded after release. First consumption at/after expiry and HCI cleanup that
crosses expiry retain the timeout even if resources are released synchronously;
closure uses a fresh post-call observation time. Resource closure does not supply
new source authority.

## Qualification and bounded work

Independent local GNSS qualification continues while COLD, including under a
foreign LoRa constraint. The original60s physical dwell and source epochs still
reset on actual source faults. Physical eligibility, coordination inhibition,
current timing permission and output authority are distinct facts.

A COLD clock has no persistent GM-control ACL. Since160 it may maintain ordinary
passive PA reception and leaf JOIN/discovery renewal. This also makes inventory
work eligible; RF/controller noninterference with the selected pair remains
unqualified. No third control slot is added. Sparse G5 opportunities do not
suppress that leaf demand. Inventory opportunities last at most1s and gate new
scan/create work only. Since151 a G5 pending owner keeps an original1s
initiation cap in every role; ordinary RC keeps100ms. An early connected
callback leaves that cap and retains the original5s
resource lifetime. A callback at/after cancellation or expiry cannot revive it.
The serialized transport rechecks ownership after public calls, so a sampled
window/connected race cannot cancel an otherwise live owner or renew an expired
one. Callback, GATT parameter and TX references remain owned until actual drain.

One actual create submission consumes the G5 opportunity, including EBUSY/ENOMEM.
The next original opportunity is5s for an unpaired ACTIVE or an unbound selected
peer, otherwise30s with an incumbent. Completion does not restart that deadline.
A missed1s window schedules the same delay from its first expiry observation;
there is no catch-up. G5 has no separate completion cooldown or selected-anchor
shortening override. Discovery still consumes each tried report and visits
least-recently-consumed identities before passive hints/RSSI, preserving explicit
selected-peer/parent precedence and full-identity failure history. Ordinary RC
admission retains its own fairness/transport policy. This cadence is an offered
opportunity, not a guarantee of radio contact.

Before160, the sole LoRa transmitter was the COLD leaf exception. All COLD GMs
now use that authenticated receiver path independently of LoRa request state.
The configured transmitter may forward selected BLE timing without an extra
GM-control link or HOT grant. Being ACTIVE/HOT adds no leaf work merely because
its factory identity is the configured transmitter.

ACTIVE chooses quality dataset tiers, then local successful-acquisition history,
then fewer local failures, then full identity. One selected identity first enters
a private CONTACT scheduling subphase with an original45s deadline. It has no
PREPARE/COMMIT readiness or grant and supplies no persistent control promotion.
STATUS0 reports no appointment during CONTACT. The first matching causal outgoing
STATUS1 (server) or STATUS2 (client) construction starts one distinct original45s
ACQUIRE deadline. Construction precedes local send acceptance, so failed/lost sends
can only shorten that budget. Repeated exchanges cannot renew it. A different peer,
wrong boot/phase or expired exchange cannot arm it. This uses one existing selected
identity and the same timestamp storage, with no new role or wire handshake.
An unreachable best-ranked identity expires through normal failure history; idle
NO_CANDIDATE checks do not invent a failed-attempt deadline. Total contact plus
acquisition may therefore take90s before failure when no successor grant was issued.
The initial5s stable-READY dwell must fit inside acquisition. Established HOT loss
withdraws readiness immediately and permits one original45s repair. An outstanding
issued grant retains its control owner through original expiry; a phase timeout
cannot silently abandon that obligation while the issuer continues serving. Failed attempts use30/60/120/240/300s cooldown, retained across
reboots/source epochs of the same full identity; starts are at least5s apart.
Inventory replacement with another identity discards that old local history.
No eligible candidate means NO HOT while a healthy ACTIVE continues serving.

The pair dispatcher rotates response, HELLO, SERVICE and management classes,
advancing only after actual transport acceptance. Each continuously due class
gets a turn within four accepted opportunities; expiring responses do not bypass
that order. Authenticated established HELLO, SERVICE and management share500ms
probes, independent of the45s control budget. Bootstrap HELLO remains200ms, and new source
permits still request HELLO immediately. G5 records retain one original1s
lifetime (two probes); legacy records retain250ms. ACK/READY expiry is clipped
to the original mapped request deadline. Four issued SERVICE slots retain every
unexpired1s record at500ms cadence; source refresh cannot reset SERVICE cadence.
These conditional software bounds do not guarantee controller or air delivery.
Source permits, READY freshness, management leases and finite grants retain
their independent original evidence; unrelated traffic cannot renew them.

Each authenticated remote instant uses its own conservative offset guard:
2000ppm over the greater of elapsed map age and its distance from the causal
anchor, plus100us. A future grant retains its full long-distance guard; that
guard cannot also shorten a separate record expiry. Evidence uses the same
guard for both interval endpoints. Original response clipping and all source,
message, management and grant deadlines remain. Host tests include a fixed
finite delivery burst, original expiry boundaries and bounded-rate corners;
they do not establish native RF delay or physical durability.

## Finite successor authority and recovery

Actual source, authenticated transport/map, captured precision and management
readiness must all pass. After5s sustained readiness, ACTIVE issues an original
90s successor grant, scoped to full ACTIVE/HOT identity and boot, both physical
source contexts and pair generation. Local issuer generations and received
root-scoped generation floors are separate. Retransmission/map refinement can
only shorten a grant. A cleared/retired generation cannot be resurrected.

ACTIVE reports global HOT only after the current COMMIT is acknowledged; local
HOT does not prove that ACTIVE received that acknowledgement. Lost COMMIT/ACK
is conservatively treated as possibly issued. Before appointing a replacement,
ACTIVE waits through the last issued grant plus50ms. Thus an uncertain old HOT
and a new successor never receive overlapping original grants from that issuer.
The original phase timestamp remains unchanged while an outstanding grant owns
retirement. Pending confirmation survives READY/PT gaps; retries carry the same
generation and original expiry. Reconnection still obeys its original native
resource deadline, clipped to the original issued obligation when the phase has
expired. Confirmed exact-tuple renewal skips another5s dwell; an unconfirmed or
changed tuple cannot. Original source permits remain independent of management. Grant expiry clears
local HOT and takeover authority immediately, even if a management lease survives.

A valid finite successor may react to explicit authenticated root withdrawal or
the original45s liveness suspicion, then completes the original3s output fence.
Its own source/incarnation and finite grant must still be valid at promotion.
Negative source creation time/epoch survives ACL cleanup in bounded inventory;
a delayed prewithdrawal positive STATUS cannot revive that exact retired root.
STATUS does not clear a same-epoch negative floor. A newer non-ACTIVE STATUS
may restore descriptive physical eligibility after the original60s qualification
in that same epoch, allowing nomination/contact. Old/equal creation times and
same-epoch ACTIVE claims remain rejected, including after eligibility returns.
This descriptive recovery grants no source map, HOT, takeover or finite grant. Fresh actual positive causal
HELLO, a new source epoch or a new boot supplies its appropriate new context.
A live LoRa constraint restricts root selection even when local physical GPS is
qualified; same-incumbent acquisition may seek direct proof without granting
candidacy/output. Exact direct withdrawal retires only its original constrained
root/boot/epoch through the original constraint deadline, without rolling it.

A COLD clock has no fast-takeover authority. Silence retains the last authenticated
incumbent contact for at most120s, then a30..32s discovery round and original3s
output fence. A newer exact-root COLD/ineligible report or actual withdrawal
shortens that contact hold to at most90.05s from its first negative observation,
conservatively outwaiting any already issued90s successor grant. Repeated negative
reports cannot extend it. Simulator recovery assertions freeze the maximum surviving original contact
expiry at each actual failure, then add32s discovery and3s output fencing. A
confirmed HOT fixture bound also freezes surviving inventory expiries at failure.
It allows one30s serialized inventory round, up to three inventory identities'
fixed45s contact+45s acquisition+5s failed-attempt spacing, original45s repair,
90.05s uncertain-grant retirement,5s stability and100ms modeled control delivery.
The separate stale120s inventory TTL prevents a silent old best candidate from
being refreshed forever. These conservative fixture bounds assume its stable
qualified survivors, reliable modeled contact/PA and bounded serialized
opportunities; real RF or unavailable PA can leave NO HOT indefinitely. Real measured
outages and cumulative NOHOT time are recorded separately from these bounds.
A returning second source also needs its original60s qualification dwell.
These policy bounds assume the physical source remains
qualified; missing discovery/transport/precision can leave NO HOT longer. Complete
partitions may each serve independently. G5 does not claim global exclusivity
across arbitrary partitions; connected incumbents merge by full root identity.

## Verification populations

`tools/gm_standby_tests.py` runs ASan/UBSan with public dependencies and records
all attempted cases, return codes, exact source hashes and no-hardware status.
The aggregate `tools/test.py` invokes it. Keep failed receipts/logs alongside
passing runs; simulations do not establish real RF scheduling or delivered UTC.

| Population | Boundary and checks |
| --- | --- |
| Network/precision simulation | Actual network, authenticated JOIN/control codecs and physical capture tuple precision fitter;3/4 candidates and join permutations, healthier newcomers, delayed/missing PA, source epoch flaps, HOT repair/replacement, ACTIVE loss, COMMIT loss, both-pair loss, return, partition merge and RX overload. JOIN delivery here is idealized/serialized per node; it is not native admission scheduling evidence. |
| Native owner fake boundary | Actual `group_admission.c` and `ble_transport.c`, fake public BLE callbacks; actual service-window policy, pending/connected callback order, synchronous create and blocking GATT deadline crossings, sparse COLD scan/initiations/precision flags, forwarding exception OFF/ON, full inventory with ordinary RC admission, same-root inhibited bootstrap, lost/replayed phases, original5s expiry, promotion/drain/reuse, failed-attempt gate just after a COLD exchange closes, and delayed STATUS after real authenticated withdrawal/takeover. |
| Producer/PT handoff | Actual encoded JOIN/STATUS/promotion and network-selected scope create a frozen ephemeral mailbox, consumed by the real PT/radio owner in a separate executable. Fifteen cases cover delayed/early PT, current/previous mismatches, ACTIVE, direct grant priority, original anchor expiry and expired-source acquisition without output. This establishes acquisition work, not actual RF convergence or HOT readiness. |
| Hostile protocol states | Actual network owner; source/incarnation/generation mismatches, retired-generation replay, offset refinement, finite expiry with surviving management lease, COMMIT acknowledgement distinctions, history ranking and repeated candidate boots; original response expiry and fair offered100ms dispatch with source changes, ACK loss/delay, response collisions, stalled submissions, READY glitches and exact repair expiry. The repeated-failure simulator suppresses PA on all spare nominees before recovery, independent of inventory order. |
| Native image inspection | Actual linked memory, frame/stack, controller and protected resource gates; normal LoRa static-free minimum4,644 bytes remains unchanged. |
| Hardware/off-host observation | Separate authorized exact-device tests; preserve source faults, intentional withdrawals, outages, missing readers, drops and observation denominators. Three/four-candidate simulation is not three/four-unit acceptance. |

See [diagnostics](diagnostics.md#g5-paged-observation) for the bounded off-host
observation contract and [build checks](build.md#checks) for reproducible commands.


## Source-change scheduling and loss diagnostics

New local source permits and epochs trigger the existing HELLO due update before
the G5 owner returns. The selected-peer scheduler still limits actual control
transmission to the current pair; COLD inventory gains no persistent control
work. Unchanged source evidence does not repeatedly force a HELLO. This scheduling
correction changes no original source, map, grant, management or resource deadline.

`network.control` publishes already stored management/pair deadlines, HOT withdrawal
count and the most recent withdrawal time/reason, last record age, maximum record
age, maximum callback-to-owner receive latency, queue peak and current backlog.
Deadlines and the retained withdrawal timestamp use the reported logical boot
clock. Withdrawal fields are historical; other fields describe the current
owner snapshot or cumulative maxima. They do not freeze the full state at loss,
prove the cause of an RF outage, or measure output accuracy. Compare counter
increments and original timestamps across current owner publications; neither
query time nor a historical maximum renews any authority.


## Version161 checkpoint — 21 October 2026

All four private enrolled devices received161 in operator order and passed actual
startup source/build/boot checks. Protected-region comparison occurred before
writing; repeated new application byte readback was waived by the operator.
The preceding160 observation completed17/17 sequential cohorts with matching
ACTIVE/HOT acknowledgements, two locked COLD receivers and LoRa transmission
advancing from17 to59. It does not establish long-run durability or UTC accuracy.

Version161 restores existing PPS/HOT duration labels and the GM Bluetooth beat
icon while retaining the160 layout. Dedicated shadow page6 again supports GM/RC
followers; a serving GM retains its original exclusion from measuring its own
stream. GM precision page2 is observational; RC precision-page sampling remains
enabled. Original source, settling, visibility and transition guards remain.
No protocol/control timer changes are included.

The source160 full regression suite is the unchanged protocol baseline. All four
161 native/resource checks and43 focused UI worker cases pass, including elapsed
durations, the badge and GM dedicated-page sampling with synthetic test records.
Static storage is unchanged. Synthetic samples are separate from field evidence.

The first161 observation ended after four complete four-device cohorts when one
USB connection disappeared. Local FDs, guards, streams and locks closed, but its
external USB identity could not be reverified. The remaining three clocks were
released cleanly. This interrupted population does not qualify161 HOT durability;
physical dedicated-shadow sampling remains pending. The operator requested that
the absent clock remain excluded and monitoring continue on the other three.

The prepare-FIFO fatal and long-run pair durability remain unresolved. Additional
COLD receive/renewal work needs sustained unstable-RF and newcomer testing. The
stream-driven handoff/quality-yield proposal remains evaluation-only.

## Development checkpoint and remaining acceptance

Version0.15.145 produced physical HOT followed by management expiry while both
sources remained qualified. Version146 formed HOT with the longer coordination
lease, then lost the selected peer's source permit before the later queue maximum.
A delayed-owner replay reproduces the8→38 refusal sequence: response dispatch
can starve HELLO; merely widening records while retrying at100ms can also overload
servicing. Version147 addresses that scheduling gap in bounded host traces, but
its600s physical observation failed durability:86 cohorts included40 complete
four-device cohorts; the longest complete ACTIVE/HOT interval was34.564s.
A subsequent native missing-disconnect-callback terminal error propagated to a
matching retained watchdog capsule. The underlying callback cause remains open.
Version148 verified GM LoRa RX participation but failed HOT durability:78/78
cohorts over600s, longest complete pair53.938s, no network/radio errors, and
continued TX/RC correction. The first horizon lapse is unknown; the later10s
repair timeout is confirmed.

The local version149 observation ran for600s with89 attempted cohorts; only15
included all four devices before one GM reset. The remaining three readers
continued, showing physical HOT and advancing transmitter/RC LoRa corrections,
but those partial cohorts do not establish complete-population HOT durability.
The failed GM's incoming connection completed connection and MTU setup, then
remained unidentified through its5s guest allowance. A disconnect request
succeeded, but actual retirement did not arrive within negotiated supervision
plus the original5s allowance. The resulting terminal error reached the watchdog;
the next boot's matching retained capsule and fresh runtime confirmed automatic
recovery into GM. The missing completion's initiating cause remains unknown.
Version149 therefore remains unaccepted for durable HOT despite restored timing.
These images received one application write and source/build/default startup
checks; repeated application byte readbacks were explicitly skipped by the
operator. Protected-region checks before writing remain a separate observation.
A separate146 watchdog recovery was observed with a valid retained worker-stall
capsule from the worker/terminal-service watchdog path; this does not by itself
prove a frozen worker. Its fault boot differs from the last observed boot, leaving an unobserved
intermediate incarnation. The initiating cause and reset latency remain unresolved.
Matching installed images and host/native checks do not establish output accuracy.

The fresh version 150 observation ran for 600 s, with 80 attempted cohorts and
72 complete four-device cohorts. Its longest complete ACTIVE/HOT interval was 176.821 s;
this still failed durability. A COLD GM repeatedly attempted its known incumbent,
but the 100 ms initiation cap cancelled before authentication. Its original 120 s
contact expired and it later nominated itself ACTIVE. A production transport
fixture with 200 ms connection callbacks reproduces that loss; the version 151
shared 1 s work budget permits renewal while preserving original expiry when the peer is absent.
The final eight version 150 cohorts lacked two USB readers. Separate fresh
inspection identified a pin reset on one device and USB reenumeration without reboot on the
other, so those gaps are not counted as two firmware watchdog crashes.

Earlier version 150 phases also recorded separate LoRa-start and CONTROL-service
watchdog recoveries. The first LoRa fault preceded any SPI operation and its fault-triggered
diagnostic save; the exact resource-initialization failure remains unknown. The
CONTROL capsule establishes recovery but does not identify the initiating terminal
operation. Reduced diagnostics, timing changes and changing GNSS reception are
not a controlled overhead comparison.

All four local devices subsequently received version 151 from commit `650e330`,
with one RC and three GM runtime profiles. The 600 s observation completed all
83 attempted four-device cohorts, with no sampled network/radio fault, OFF
request or observed physical-boot change. COLD contact renewals were observed.
The configured LoRa transmitter advanced from 2 to 39 completed launches; the
RC's accepted LoRa packets advanced from 2 to 26, with corrections and BLE lock
observed during source availability. All readers and host locks were released.

This run cannot qualify HOT or failover: two GMs had no GNSS fix or qualified
source in any sample, and the remaining GM qualified in only 43/83 samples.
No HOT was observed. Source loss withdrew timing, which resumed after
requalification; the final cohort again had no qualified source or valid RC map.
Radio requests remain ON in the intended profiles. This source-limited population
must remain separate from the earlier source-qualified version 150 pair runs.

Version 152 changes only retained watchdog failure classification. Timer, pairing,
transport, radio and source behavior remain identical to 151. All four ordinary
images passed native builds and actual startup checks. In the first 600 s field
capture, 89 cohorts were attempted and 31 included all four participants. One GM
reset; its failed reader and subsequent gaps remain in the denominator. No
complete qualified ACTIVE/HOT cohort was observed. LoRa transmission and RC
corrections advanced without a sampled radio fault or OFF request.

Two separately recovered, image-bound capsules subsequently classified that GM's
resets as terminal service error `-116`, with progressing CONTROL. Neither stale
CONTROL nor expiration of the overall closure budget caused feed withdrawal.
The first live failure retained an anonymous incoming admission connection after
disconnect request, with no pending TX or GATT bridge and no matching application
disconnect completion. The newest event tail was not retrieved before reset, so
the exact public disconnect result and host/controller state remain unknown.
A different identified management link had timed out earlier during acquisition;
these are separate observed links and failure times. This is failure isolation,
not a demonstrated correction. Firmware and timer changes remain frozen while
the unchanged images are observed for a durable pair.

A later pair check observed matching ACTIVE/HOT tuples in separate snapshots
less than one second apart. The standby reported HOT in 12/89 samples of the next
600 s capture, then withdrew local source permission after GNSS loss while its
delivered BLE map remained locked. That brief recovery does not establish pair
durability; missing ACTIVE-reader cohorts are not filled by the separate check.

Continued version152 observation produced six separately bound terminal
CONTROL-service recoveries with errno `-116`. Three complete event tails show an
accepted disconnect request, no outstanding transport TX, and no application
disconnect completion by negotiated supervision plus the original5s allowance.
One occurred with the local source qualified throughout; source withdrawal is
therefore not necessary for this failure. A seventh recovery has a different
cause: retained kernel-OOPS evidence and the exact image identify the controller
`ticker_update_latency_cancel_op_cb` SUCCESS assertion. The original callback
status is unavailable. These populations must not be combined into one cause.

Version153 adds a first-fault host-state/work snapshot without changing timers,
pairing or radio policy. All four images passed native and host checks and
received one application write plus actual startup source/build/default checks.
Its first600s capture attempted91 cohorts,15 complete. An operator reset and a
terminal GM failure account for separate reader gaps. Two subsequent complete
GM failure tails show accepted disconnect requests and expiration of the
original5.5s budget:500ms negotiated supervision plus5s. At expiry the host was
still internally DISCONNECTING, with no queued TX or deferred-cleanup work. The
watchdog recovered both failures. This places the observed stall before priority
disconnect completion, but does not distinguish controller retirement from
integrated HCI delivery. A separate LoRa-start error also recovered through the
watchdog; its exact resource-initialization subcall remains unknown.

Better GNSS placement restored source qualification and episodic HOT, but did
not restore durability. A frozen source153 snapshot contains229 asynchronous
GM samples across four capture segments. Every sample has physical eligibility
and network error0; each device's source epoch and GNSS fault count remain
unchanged. Nevertheless, the ACTIVE GM lacks confirmed HOT in25/99 samples of
one completed segment, with57 counted withdrawals. The standby changes roles
and also counts withdrawals. These are separate device populations with capture
gaps, not229 simultaneous pair checks. Positive polls cannot establish continuity
between polls. Withdrawal masks identify coordination freshness/binding and
readiness/synchronization predicates; they do not identify the initiating loss.

The offline gap is now explicit. The coarse COLD model omits the configured
LoRa-transmitter bridge workload. Stock controller termination tests omit the
production connection-event PRT/supervision progression and execute no queued
mayfly callbacks. Expanded production-body tests demonstrate that prescribed
control-node exhaustion can delay termination until a node is released, and
that a prescribed ticker stop/update failure reaches the actual assertion.
Both are conditional reproductions; field pool occupancy and the initiating
ticker ordering remain unproved. No timer widening, ignored error or controller
workaround follows from them.

Version154 adds only a bounded current-controller/LLCP snapshot at the existing
first-disconnect fault. All four devices passed actual startup and intended
runtime checks with ordinary radio requests ON. Four native builds and the host
suite pass; static storage is unchanged. Current handle matching does not prove
controller incarnation, and WAIT_TX does not measure allocator occupancy. The
fault-time records classify the next failure; they do not fix HOT churn or the
controller assertion. Timer, pairing and radio behavior remain frozen.

The first180s source154 observation completed24/24 four-device cohorts without
a sampled network/radio error or observed reboot. Two GMs qualified; the third
remained source-limited COLD while forwarding timing to LoRa. Transmissions and
RC corrections advanced. HOT withdrawals still occurred. Subsequent reduced
console polling did not eliminate them or the failures: one GM repeated the
same source-bound controller ticker assertion, and another reached an anonymous
connection's original6s disconnect deadline. The new snapshot retained a matching
controller handle with a pending TERMINATE procedure and both its procedure and
local request machine still IDLE. For the same unchanged context this places
the missing progress before its first completed LLCP RUN, rather than at TX-node
allocation or completed teardown. Controller-slot reuse or a replacement
termination is not excluded. Zero expiry counters alone do not prove missing
connection events. Both failures subsequently recovered through the watchdog.
Diagnostics overhead remains a plausible contributor, not an established cause.

A later source154 GM failure independently repeated the same current-handle,
TERMINATE/IDLE signature on another controller handle, after an accepted close
reached its original6s deadline. The prior identified connection had already
retired; its identity cannot be assigned to this anonymous failing connection.
A subsequent observation gap contained an additional boot and a retained CONTROL
service-error watchdog record for that intervening boot. The retained record does
not match the earlier live-fault boot, whose capsule is missing. These are separate
failure observations, not one continuously observed reset chain. Both GMs remained
source-qualified across repeated HOT losses and reformation. Lower console polling
did not eliminate the failures, but was not a controlled CPU-load comparison.

The separately sealed source155 diagnostic candidate retains the original
latency-cancel callback status before its unchanged assertion and adds bounded
current connection/ticker counters at the same first-disconnect fault. Independent
review, the full host suite, four native images, DC/PC exclusions and optional
GM diagnostic composition passed. Static storage remains unchanged; the first-fault
capture's compiler-reported stack frame increases from80 to112 bytes. No timer,
pairing, RF or scheduler policy changed. All four authorized devices passed actual
source/build/default startup checks and intended runtime/radio restoration.

Its first180s observation completed24/24 four-device cohorts without a sampled
network/radio error or reboot. The two GMs were independently source-qualified in
23/24 samples each;15 complete cohorts reported the intended ACTIVE/HOT pair,
spanning106.53s between first and last observations. The third GM remained
source-limited COLD; LoRa transmissions and RC corrections advanced. Early in the
subsequent reduced-polling phase each GM recorded a HOT withdrawal and reformed
the pair, with unchanged sampled source epochs and no service error. This is a
diagnostic installation and finite private hardware observation, not a controller
fix, a public evidence dataset, calibrated timing or durable HOT acceptance.

A subsequent source155 COLD bridge/LoRa-transmitter reboot has a matched
source-bound fatal capsule and watchdog recovery. Exact installed-ELF disassembly
identifies `LL_ASSERT(next)` after `ull_prepare_enqueue` returned NULL. Under
normal FIFO invariants this means all seven prepare entries were occupied; the
retained data does not identify their owners or rule out an index invariant
failure. This is a distinct assertion from the earlier peripheral latency-cancel
failure; the new PA marker is absent. No host reader was attached by this
investigation during the failure interval, which does not rule out firmware
instrumentation or workload effects. The watchdog eventually recovered the unit
and LoRa forwarding resumed. Physical reset latency remains unmeasured.

A separate source155 GM disconnect failure also has a complete first-fault cut
and a matching service-error watchdog record from the same failed physical boot.
The anonymous incoming connection retained a matching current controller handle,
an unprocessed TERMINATE/IDLE head and zero ULL references after its accepted
close reached the original supervision-plus5s deadline. The ticker was valid with
matching current context, request/acknowledge distance1 and accumulated lazy1461.
This favors a scheduled but deferred connection over ordinary stopped-ticker
state. Lazy is not an RF-loss count or a measured starvation duration; a single
cut cannot prove generation, list membership or the blocker. Controller supervision
advances through completed events, so its stored zero is not proof that a wall-clock
supervision deadline elapsed. The watchdog recovered this failure too. Its cause
remains separate from the bridge's full-prepare assertion.

With the exact LOW_LAT_ULL_DONE-off configuration, prepare dequeue occurs in
`lll_done` before later ULL done notification. Delayed HCI/ULL completion alone
therefore does not explain the full prepare FIFO. Existing earlier canary captures
contain no bound schema3 FIFO fatal snapshot in the searched text records. The
next discriminator is a failure-only copy of queued owners and resume/aborted
flags using the existing observer and capsule storage, without enabling routine
success counters or periodic controller/timing console streams. Queue capacity,
assertions, timers, RF and pairing remain frozen; no corrective change is proved.

Source156 implements that quiet prepare capture after independent review. The
copy validates FIFO indices, masks IRQs across at most seven entries and marks
partial or unavailable data UNKNOWN. It reuses existing scratch and schema3
capsule storage; successful prepare paths have no observer calls or counters.
Four native builds, profile exclusions, the full host suite and actual four-device
startup/runtime checks pass. Static free space remains4708 bytes and flash grows
232 bytes relative to155. The fatal handler's compiler frame grows24 to120 bytes;
the conditional prepare/fatal stack estimate does not qualify arbitrary nested
IRQ chains or measured latency. Schema3 trades the secondary interrupted-thread
frame and IRQ-latency fields for queue owners. Field durability remains unaccepted.

Source156's first180s observation completed24/24 four-device cohorts without a sampled
service/radio error or reboot. Each intended pair endpoint was source-qualified
in19/24 samples. Eleven complete cohorts had matching ACTIVE/global-HOT and
peer/local-HOT identities and logical incarnations, spanning75.69s between first
and last observations. The third GM stayed source-limited COLD; LoRa TX and RC
corrections advanced. Subsequent reduced polling again captured readiness
withdrawals and reformation with unchanged sampled source epochs. This confirms
that the diagnostic update has not resolved HOT durability.

Source157 adds one same-class scheduler condition so force wins before age;
critical and cross-class precision precedence remain unchanged. An independently
reviewed actual queued-controller host population stalls48/2160 cases with the
previous condition and0/2160 with the candidate. The reduced DLE example exceeds
available reservation capacity, so this tests progress under contention rather
than capacity or field causation. Four native builds, profile composition and the
full host suite pass, with unchanged static objects/stack frames and52 fewer flash
bytes than156. All four authorized units passed actual source/build/default
startup checks and intended runtime restoration.

The first157180s observation has23/23 matching ACTIVE/global-HOT and local-HOT
cohorts, spanning167.59s between first and last samples, with zero observed
withdrawals and advancing LoRa/RC counters. Subsequent reduced polling failed:
the HOT peer stopped replying, then rebooted through a matching watchdog fatal
record. Its source/boot/CRC-bound schema3 capsule captures the exact
`lll_prepare_resolve():891` failed allocation assertion and all seven queued
prepare owners: three central, two scan, one ordinary advertiser and one periodic
receiver. All are nonresume/nonaborted at that cut. The failed eighth prepare is
ordinary advertising. Current active owner, queued timestamps, controller
incarnations and preempt/done dispatch progress remain unknown. This does not
establish a widened-receive or starvation cause;157 has not resolved controller
crashes or qualified durable HOT.

A separate source156 pre-update observation confirms that the successor did
promote and serve after its issuer was updated. Recorded software values show
45.003s root-health suspicion plus the existing3s output fence; clients were not
simultaneously observed during that transition. The requested design now makes
valid stream continuity primary and includes planned yield to a better qualified
successor. A draft stream-expiry-only change remains withheld: an existing
actual-owner recovery case produces two serving roots for16.625s and fails to
restore HOT by its270s endpoint. Ownership transfer and receiver-local loss need
a concise rule before another field candidate; the long management renewal
window must not become the outage detection delay.

Source159 refreshes coherent direct HOT/ACQUIRING precision input before readiness
uses it, preserving the original observation time and expiry. It also makes the
ordinary HOT page observational; intentional shadow sampling remains on page6.
No election, takeover, timer, queue, controller or RF policy changes are included.
The separate stream-driven takeover/quality-yield draft remains evaluation-only.

All four source159 native images, DC/PC/verbose composition, the complete host
suite, actual input-order tests and42 UI sanitizer modes pass. Writable static
objects and4708 bytes of unallocated RAM are unchanged. A new328-byte helper
frame runs on the existing8192-byte CONTROL stack; inspected frames are not a
whole-system stack or execution-time qualification.

The first source159180s observation completed24/24 four-device cohorts. Following
source qualification,13 cohorts matched TC-02 ACTIVE/global-HOT with TC-04 local
HOT, spanning90.39s between first and last observations. No network/radio errors,
HOT withdrawals or boot changes occurred in that sampled interval. TC-03 remained
COLD and unqualified locally, but acquired BLE timing and advanced LoRa TX2→17;
TC-01's accepted LoRa count advanced4→13 and correction sequence0→10.

The following HOT-page session failed during USB opening, before any page command
was sent. A fresh exact-device capture confirms a source159 fatal at277674ms of
the failed boot and matching watchdog recovery. The complete seven-entry prepare
queue contains four central, one advertising, one periodic receiver and one scan
entry, all ordinary nonresume/nonaborted; the eighth failed preparation is central.
The exact ELF resolves the same prepare-allocation assertion as157. The current
active owner and preempt/DONE dispatch history remain unknown. USB opening is not
proved neutral, but that attempted page selection did not precede the failure.
A separate pre-update157 watchdog/fatal was also preserved. Source159 therefore
fails durable HOT acceptance despite its short successful pair observation.

After that recovery, TC-04 requalified, reacquired HOT and passed the separate15s
HOT-page check:5/5 samples reported sampler OFF, unchanged session/sample counts,
and retained local HOT. Its original page was restored. A later TC-02 observation
acknowledged the recovered appointment. Final brief checks retained01RC,
02/03/04GM and all radio requests ON;03 remained the sole configured transmitter
with TX80, while01 reported74 accepted LoRa packets and correction sequence71.
Every reader was closed. These sequential checks establish functional recovery,
not uninterrupted pairing, reset latency, neutral diagnostics or UTC accuracy.

The next investigation should isolate controller prepare/preempt/DONE progress
at the full-queue failure. Keep timer and pairing changes frozen while retaining
fault-only evidence. The offline input/UI gaps now have actual-body coverage;
real ISR/mayfly dispatch and RF contention still require distinct evidence.
The current four-device population has three GM profiles and one RC. A capable
newcomer/fourth-GM test remains a separate next-round population after a durable
pair is demonstrated; other powered enrolled clocks remain observed RF context.

The remaining work has separate causes and evidence requirements. First retain
a stable two-GM pair with the third GM remaining COLD; only then exercise newcomer
and four-device admission, keeping the ordinary RC in its RC profile:

- Establish authenticated GM control reliably. Discovery and connection attempts
  can occur without completing admission; preserve connection callbacks, GATT
  setup errors, local cancellation and original transient deadlines separately.
  The first failed exchange still needs to be identified.
- Exercise partial connections and one-device restarts while other production
  owners retain their peer caches, grants, delayed messages and pending callbacks.
  Reuse peer/session invalidation on proved incarnation changes; software cache
  cleanup cannot substitute for actual hardware retirement.
- Compare ordinary images and reduced console traffic with diagnostic images.
  Diagnostic formatting, controller hooks and host polling consume resources;
  static memory checks alone do not establish that observation is neutral.
- Observe actual PA/map convergence and sustained HOT renewal. The fixed45s
  acquisition budget passes a controlled late-map host case; it cannot repair
  failed contact or establish physical convergence by itself. An earlier
  management-expiry withdrawal is reproduced by varying renewal acknowledgement
  latency; the new coordination margin still needs sustained field observation.
- Retain physical-source qualification failures. A void GNSS fix resets the
  original60s dwell even when a later snapshot shows satellites or PPS. Source
  availability, authenticated map readiness and watchdog health are separate.
- Measure eventual recovery from an injected owner stall or terminal drain fault.
  Production watchdog checks pass; physical reset latency remains unmeasured.
  Healthy COLD operation and absent GPS/RF alone must not request a reset.
- Keep the observed native controller preparation assertion open. Retained
  failure evidence from140 and143 shows the same assertion followed by a
  watchdog reset. Its trigger and a general reset-latency bound remain unresolved.
  Candidate requalification, per-timestamp clock bounds and passing host tests
  do not establish a repair for that controller failure.

The next authorized test round should first establish one ACTIVE and one
confirmed HOT with a third capable clock COLD, retaining source faults, NO HOT
intervals, resets, capture gaps and every attempted observation. Introduce the
fourth device as RC first, then perform the operator's GM newcomer test after the
three-GM baseline holds. Check that a healthy incumbent pair remains sticky,
COLD adds no persistent GM-control links, and failed nominees expire into bounded
retry history. Test HOT loss, ACTIVE loss, lost COMMIT/ACK, source epoch changes
and recovery separately. Moving a unit away from the host for better GNSS is a
new recorded reception condition, not a continuation of the indoor population.

End each run by restoring radio requests and intended runtime profiles, verifying
actual transmission or reception separately from an ON request, and releasing
all host readers and locks. The configured sole LoRa transmitter requires the
GM runtime profile: staging it as RC inhibits TX even if its request remains ON.
A GM-profile COLD forwarding leaf can use a current authenticated locked BLE map
without becoming ACTIVE/HOT; an actual serving GM retains its own source gate.

### Retirement and READY freshness in0.15.144

Normal native control connections issue one disconnect request. Version144 used an
implementation-chosen2s retirement budget; version145 replaces it as described below.
The immutable retirement budget starts on that request or the first natural disconnect callback.
The same budget covers outstanding TX and bridged GATT work; a request result
alone never releases references or immutable TX storage. Missing retirement is
terminal and reaches the existing watchdog path, even while CONTROL publishes.
This prevents an old peer owner from blocking inventory indefinitely without
pretending that timed-out controller ownership is safe to reuse.

READY freshness shares the existing2s management lease instead of an independent
1s cutoff. This tolerates a missed reply opportunity; current physical source,
causal map and PA evidence remain mandatory. Original250ms message expiry,
clipped response expiry,5s stable dwell and10s finite grants remain unchanged.
Selective1.1s READY-loss host checks retain ordinary source and physical-tuple
streams and exercise the strict2s cutoff separately. They do not model real RF
scheduling or establish durable physical HOT.

The multi-node host test executes real network, wire and precision bodies, but
retires logical peers instantly and uses synthetic packet/PA delivery. It cannot
expose a native connection whose disconnect or TX callback never arrives. The
separate native transport/JOIN test retains that owner and verifies inventory
exclusion until the original deadline produces a terminal error. The full host
runner passes that actual error to the production watchdog observer; the short
service error-publication bridge is checked against source rather than executed
as a complete worker. These tests distinguish authority expiry from liveness:
every closing resource must actually retire or become terminal by its original
deadline. Real HCI, ISR scheduling and controller FIFO coexistence still require
the diagnostic/native boundary and separately authorized physical observations.

### Negotiated cleanup in0.15.145

Incoming ACL connections can negotiate up to32s supervision; the pinned controller
also uses that timeout for LL termination. Version145 reads actual public connection
parameters and adds a shared5s callback allowance. Connecting cancellation and a
completed natural disconnect use only5s. Duplicate owners use the same policy.
Every deadline stays anchored to its first close; retries never renew it and
timeout never permits buffer/reference reuse. Event23 records supervision in10ms
units in result low16 bits and public connection state in high16 bits (negative
means parameter-query failure), interval/latency packed
in a (low/high16 bits), and native incarnation in b (zero for duplicate owners).
An unavailable or invalid negotiated value is terminal, never guessed.

Normal GM/RC outer stop, profile and maintenance budgets share40s: maximum32s LL
termination,5s callback retirement and3s dispatch allowance. The outer budget is
a total fail-safe, not a promise of arbitrary scheduler progress. Existing PA
synchronization, scan/HCI request, source, PT, message, READY and grant deadlines
are separate and unchanged. The independent30s owner-stall observer detects stale
publication, not a healthy owner polling a long cleanup. RC synchronous ACL
retirement polls publish only the existing closing timestamp; their original
LT37s local/40s outer limits remain finite, and no source authority is renewed. UCS
client disconnects begin before the RC link wait, so both drain concurrently
inside the original40s outer deadline with no additional local UCS timer. The pinned UATT stack copies notification storage
at submission and drains/detaches it before the disconnected callback; cleanup
also waits for submission return and the worker to finish, retaining references
on failure. Firmware maintenance
PREPARE and READY use the same40s bounded phase budget. The existing watchdog
restart observer spans both sequential40s drain/reinitialization phases plus3s
dispatch (83s), without extending either phase or tolerating a terminal error.

A latched GNSS driver error now requests the existing watchdog recovery only in
ordinary runtime GM while idle, outside profile transition and intentional poweroff.
No-fix, VOID, missing PPS and source silence do not trigger this hook. RC/DC/PC
behavior remains separate. Persistent driver faults can cause repeated resets;
no new backoff, timer or retained state is introduced. The capsule preserves the
error but does not uniquely identify its caller.

Host tests exercise actual native ownership with delayed legal callbacks beyond2s,
original negotiated expiry, missing TX/GATT callbacks and the production GNSS latch
to watchdog path. The former fixed2s implementation fails the same delayed-callback
regression. Native resource checks and physical HOT durability remain required;
these changes do not establish the cause of the earlier controller FIFO assertion.

### Coordination margin in0.15.146

G5 uses one5s coordination lease for management,
READY freshness and authenticated peer/root liveness. Legacy profiles retain2s.
Finite grants last at most15s (three coordination leases) and renew on the
existing2s issuance cadence;
the lease allows3s nominal margin for variation in renewal acknowledgement time,
provided accepted acknowledgements remain less than5s apart. The15s grant also
funds the2s issuance age,5s identity silence and existing3s takeover fence
after a lost renewal. It also expires before the10s repair closure plus5s
silence and3s fence could turn a deliberate closure into a false takeover.
These nominal relationships retain the50ms retirement guard; they do not
establish exclusivity under arbitrary asymmetric radio partitions.
A tested20s draft violated that second relationship; the existing precision-loss
regression exposed it, and that draft is not the146 policy.
Each original grant remains immutable; uncertain retirement waits through its
original expiry plus50ms. The changed grant cap requires matching GM firmware
and intentionally permits slower retirement of an uncertain grant.
A replay cannot renew a grant, and a new source epoch still requires qualification.
Independent1.5s PPS/source permits,2.5s GNSS freshness,250ms original control
records and current precision-map validity are unchanged. Coordination grace
never makes an expired source or timing map usable.

Previous offline coverage exercised regular25ms delivery and isolated READY
loss, but did not vary both COMMIT and READY renewal availability across the
2s issuance boundary. That left the equal renewal/lease periods untested. The
production-wire fixture now suppresses both renewal classes for phase-varying
2.4–2.6s bursts while HELLO, SERVICE and actual precision tuples continue; the
fixture offers at most one accepted send per100ms per peer. The old2s policy
loses HOT and the5s policy preserves it, with measured acknowledgement gaps
above2s but below5s. Six silent-primary failure phases span renewal boundaries
and pre-failure renewal loss; the15s finite grant funds takeover where the
10s grant can expire first. A longer outage still
withdraws HOT. Original lease boundaries, stale timing, source loss and replay
remain separate negative controls. These synthetic schedules do not reproduce
native ISR/HCI congestion; CONTACT admission failures are a separate question.

### Owner servicing in0.15.147

Earlier renewal fixtures limited accepted offers but did not service RX and TX
through the same delayed owner or continuously replenish response traffic. The
ordinary response wrap also failed to advance the cursor, while the expiring
response exception bypassed fairness. The shared500ms probes and pure round
robin replace those competing rules without new state or queues.

The durable paired test uses actual network/codec bodies, two RX records per
owner cycle and a32-record FIFO. Physical callbacks precede processing by a
separate timestamp. It compares100/125/150/175/200/250ms owner cycles and an
explicit repeating100/150/250ms burst. Exact146 fails five of these seven
schedules; the147 candidate passes all seven. Initialized authentication,
1Hz physical source and ideal PT remain fixture inputs; native interrupt/RF
scheduling and query interference are outside this model.

Only authenticated G5 outer19/domain5 accepts the1s record budget. MAC-correct
legacy250ms and G5 1s boundaries, original response clipping, four-slot ACK
history, replay, stale source and invalid PT remain negative controls. PPS
permission1.5s, GNSS freshness2.5s, PT publication250ms, causal echo500ms,
map width80ms, callback age500ms and original15s grants are unchanged. Longer
messages neither renew source permission nor make stale timing usable. This
change requires matching GM firmware and awaits physical acceptance.


### Partial restart and retained-state offline coverage

Version150 extends the existing production-owner harness with one-device resets
while the other endpoint retains its boot/epoch cache, issued grant and queued
wire records. Delayed and duplicate link-loss callbacks, actual new-boot transient
JOIN/STATUS authentication, stale proof/ACK rejection and original grant expiry
are checked without a global cache flush. Separate incoming-guest tests cover
missing, timely, late and duplicate disconnect callbacks and missing TX completion;
unsafe objects remain owned and missing retirement stays a terminal failure.

Four wire/precision schedules reset ACTIVE or HOT, delay remote link-loss by500ms
or5s, and retain remote caches. A previously COLD GM restores HOT while the reset
unit lacks a physical source; after normal source requalification, the returning
identity follows the healthy incumbent as COLD. These schedules reuse the existing
network/JOIN/precision bodies and two control slots. Their synthetic delivery and
callback assumptions do not prove hardware durability or explain a missing HCI,
L2CAP or application callback. Run `gm_standby_tests.py` as in the build guide;
its receipt lists the restart cases separately from the original scenario count.


The native COLD inventory regression also runs beyond the120s incumbent contact
lease with fresh advertisements from the incumbent and five competing cached
identities. Without incumbent preference, unsuccessful other-peer attempts can
consume the sparse30s opportunities until contact expires. Version150 points the
existing admission parent at a still-live known incumbent when COLD has no other
required parent. The normal JOIN/STATUS exchange renews contact; no source/PT or
HOT authority follows from the preference. An unavailable incumbent cannot renew
itself: the same test requires original expiry and finite ordinary fallback.
ACTIVE still inventories candidates and partitions; appointed control repair and
an existing required parent retain precedence. This fixes a demonstrated native
scheduler gap, not the unexplained physical missing-disconnect callback.

Version151 consolidates software work budgets without renewing authority. Public
production-owner tests cover50/200/900/1100ms callbacks across G5 roles,50ms JOIN
steps,600ms STATUS/control queue delay, original scan-stop expiry and aggregate
GATT waiting. The old100ms initiation body loses a reachable incumbent with200ms
callbacks. Original signed expiry, replay, future-receive, legacy queue guards,
source permits and deferred references remain checked. Host fakes do not establish
radio fairness, HCI/deferred-L2CAP behavior or physical durability.

| Control-path timer | Meaning and151 handling |
| --- | --- |
| G5 initiation1s | Original discovery/work budget, all roles; ordinary RC100ms unchanged. A callback cannot restart it. |
| G5 GATT1s | One original CONTROL-pass budget in existing owner_deadline, clipped by provisional expiry. Multiple calls share it; ordinary RC per-call100ms unchanged. |
| G5 scan-stop5s | Existing callback-retirement budget from first transient refusal;20ms retry cadence. Legacy core500ms/client behavior unchanged. Timeout retains ownership and faults. |
| G5 STATUS5s session | Original authenticated JOIN expiry replaces redundant500ms local queue-age check. Boot/nonces/MAC/phase still required. |
| G5 non-HELLO record1s | Original signed/mapped expiry replaces redundant500ms queue-age check; captured receive time, evidence ordering and replay remain. |
| HELLO/legacy receive500ms | Causal/coarse-time guard retained, including HELLO offset-width/source checks. |
| Source/PT and coarse-map bounds | Precision/physical authority, not work budgets; unchanged. |
| JOIN/provisional5s and callback retirement5s | Original owner lifetimes and actual reference drain; no synthetic completion. |
| Management/acquire/repair45s; issued grant90s | Original G5 obligations unchanged; legacy R2_MANAGEMENT_LEASE_US remains2s with G5 override45s. |
| Inventory contact120s; sparse retry30s/5s | Original contact permission and scheduling cadence unchanged; only accepted STATUS refreshes contact. |
| Probe500ms; UI/report/retry cadences | Scheduling rates, not failure limits; unchanged. |
| Owner queue100ms diagnostic threshold | Counter only, not fatal; unchanged. |
| RC dynamic sync-close500ms/2s | Separate resource/closure bounds; unchanged and flagged for follow-up lifecycle testing. No PA geometry change. |
| Admission close50ms/2ms | Grace/cadence with actual ownership recheck, not permission renewal; unchanged. |
| HELLO80ms, coarse-time100ms, record250ms, PPS1.5s | Causal width/precision/source bounds or legacy signed lifetime; unchanged. |
| R2_REPLY_MS2s / host query2.5s | Diagnostic reply/reader timeout only, not firmware authority; keep missing responses separate from device failures. |

A pass can still spend up to1s waiting for GATT. This bounded work must be checked
against healthy ACTIVE/HOT radio service in the field; the host test proves fixed
budget and reference ownership, not timely radio delivery under real contention.

The residual ownership audit found a separate RC cancellation constraint in
`rc_dynamic_service.c:sync_close_until`: ordinary parent retirement gets 500 ms,
while cancellation after 1.5 s acquisition gets only the remainder of the original
2 s creation deadline. Local ASan/UBSan schedules using the actual callback,
event-drain and close bodies accept lookup release at 499 ms but time out at 500 ms
or 600 ms. Supplying the unchanged close body one fixed 5 s retirement allowance
permits 600 ms release; missing release still expires with the object retained.
These are fake public-API schedules, not observed HCI latency or a diagnosed
field crash. The regular RC durability fixture still stubs this close function;
promote the actual-owner schedules into that suite before changing these bounds.

Consolidating these PA cleanup callers onto the existing callback-retirement
budget is the next small candidate. Acquisition must end independently, original
source permission must still expire, and retries must not restart cleanup.
The blocking wait needs CONTROL-responsiveness and radio-fairness checks before
adoption. Injected transient delete refusals also fault immediately today; their
actual host reachability remains unproved. Native precision's original 2 s closure
is a second review target. Admission's 50 ms grace requests release while transport
keeps the real references; peripheral BUSY/DMA/FEM limits serve separate purposes.

## Source162 field checkpoint (22 October2026)

Two requested600s observations ended early. The first retained seven complete
four-clock cohorts plus partial eighth,398 validated CRC frames, followed by
TC02 silence and exact-source/boot/ELF proof of `LL_ASSERT(next)` after a full
seven-entry prepare queue. Watchdog recovery was observed. The second retained
39 complete cohorts plus partial40,1889 CRC frames, and another TC02 fatal/
watchdog recovery; its reason3 is bound to that second failed boot, but no detailed
assertion capsule arrived. Do not assign the first FIFO site to the second boot.
The longest matched ACTIVE/HOT sampled interval was229.438507s. Neither run
qualifies durable HOT, calibrated UTC accuracy or independently measured output.

The second run separately observed HOT03 lose actual GNSS/PPS qualification;
ACTIVE01 kept its qualified source and HOT02 later qualified and was appointed.
COLD reception was maplocked20/20 on02 and39/39 on04.03 was COLD for26 samples,
mapvalid24/26 and locked21/26, then locked in all21 samples19–39 after its role
transition. These are normal precision reception observations, not HOT authority.
A later three-clock observation ended after16 complete cohorts/658 CRC frames;
TC03, then COLD and the sole LoRa transmitter, subsequently supplied a matching
reason3 fatal/watchdog record without a detailed assertion capsule. The unresolved
fatal is therefore not established as specific to HOT operation. No controller
fix or physical-cause conclusion follows from these retained records.
