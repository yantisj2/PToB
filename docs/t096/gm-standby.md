# T096 G5 ACTIVE, HOT and COLD

Copyright (c) 2026 Jonathan Yantis. MIT.

Version0.15.134 introduces G5 coordination for the ordinary GM profile; current
version0.15.149 retains147's500ms G5 probes and1s original control records with the
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

The producer retains its original bounded JOIN timing anchor while COLD; the
service still suppresses true COLD precision publication. After actual selected
control promotion, PT may use the current causal anchor even if it never saw the
earlier passive publication. The selected remote root/boot must match, as must
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

A COLD clock ordinarily has no persistent GM ACL, PA precision acquisition or
continuous bootstrap scan. Inventory opportunities last at most1s and gate new
scan/create work only. An accepted pending owner keeps its original100ms initiation
cap; an early connected callback leaves that cap and retains the original5s
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

There is one explicit leaf exception: the configured sole LoRa transmitter may
need ordinary BLE leaf timing while COLD to keep **requested** LoRa forwarding
alive. It uses no extra GM-control link and gains no HOT/grant authority. Turning
LoRa forwarding OFF suppresses the exception. Being ACTIVE/HOT adds no extra
inventory/leaf work merely because the factory identity is the configured TX.
True COLD clocks and this COLD forwarding leaf must be measured separately.

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
repair timeout is confirmed. Version149 remains an offline control-policy candidate.
A separate146 watchdog recovery was observed with a valid retained worker-stall
capsule from the worker/terminal-service watchdog path; this does not by itself
prove a frozen worker. Its fault boot differs from the last observed boot, leaving an unobserved
intermediate incarnation. The initiating cause and reset latency remain unresolved.
Matching installed images and host/native checks do not establish output accuracy.

The remaining work has separate causes and evidence requirements. First retain
a stable two-GM pair with the third GM forwarding; only then exercise newcomer
and four-device admission, keeping the ordinary RC in its RC profile:

- Establish authenticated GM control reliably. Discovery and connection attempts
  can occur without completing admission; preserve connection callbacks, GATT
  setup errors, local cancellation and original transient deadlines separately.
  The first failed exchange still needs to be identified.
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
