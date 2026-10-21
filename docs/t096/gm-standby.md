# T096 G5 ACTIVE, HOT and COLD

Copyright (c) 2026 Jonathan Yantis. MIT.

Version0.15.134 introduces G5 coordination for the ordinary GM profile; current
version0.15.140 includes corrected inventory/contact scheduling, causal timing
handoff, explicit PT close intent consumption, change-driven HELLO scheduling,
GM watchdog coverage and a fixed45s successor acquisition budget. This is
an implementation and host-test contract, not hardware acceptance or a claim of
UTC/output accuracy. RC7 clients, USB, GNSS qualification, BLE capture and LoRa
wire3 retain their separate contracts. DC remains a separate flash-only image.

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
the original5s exchange deadline. No STATUS phase renews it. Matching G5 STATUS and appointment allow the client to promote after its final
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
continuous bootstrap scan. Inventory opportunities last at most1s; the next
original opportunity is5s while discovering/no selected contact and30s with an
incumbent. The same policy schedules both completed and missed opportunities:
ACTIVE without a selected peer waits5s after its1s window expires, rather than
mistaking its own ACTIVE identity for a remote incumbent and waiting30s. A
selected peer without a bound control link likewise retains5s repair spacing.
Missed opportunities do not accumulate. Each transient connection attempt still
has its original100ms cap and consumes the unchanged2s discovery cooldown.
Repeated missed opportunities therefore recur at6s spacing when the owner runs
on time; this does not guarantee radio contact. G5 discovery visits least-recently-consumed
identities before passive hints/RSSI, preserving explicit selected-peer/parent
precedence; ordinary RC ranking is unchanged. Failed selected transport
contact retries at5s spacing. One transient exchange per local owner is retained;
ordinary RC admission has its own fairness/transport controls.

There is one explicit leaf exception: the configured sole LoRa transmitter may
need ordinary BLE leaf timing while COLD to keep **requested** LoRa forwarding
alive. It uses no extra GM-control link and gains no HOT/grant authority. Turning
LoRa forwarding OFF suppresses the exception. Being ACTIVE/HOT adds no extra
inventory/leaf work merely because the factory identity is the configured TX.
True COLD clocks and this COLD forwarding leaf must be measured separately.

ACTIVE chooses quality dataset tiers, then local successful-acquisition history,
then fewer local failures, then full identity. One selected identity first enters
a private CONTACT scheduling subphase with an original30s deadline. It has no
PREPARE/COMMIT readiness or grant and supplies no persistent control promotion.
STATUS0 reports no appointment during CONTACT. The first matching causal outgoing
STATUS1 (server) or STATUS2 (client) construction starts one distinct original45s
ACQUIRE deadline. Construction precedes local send acceptance, so failed/lost sends
can only shorten that budget. Repeated exchanges cannot renew it. A different peer,
wrong boot/phase or expired exchange cannot arm it. This uses one existing selected
identity and the same timestamp storage, with no new role or wire handshake.
An unreachable best-ranked identity expires through normal failure history; idle
NO_CANDIDATE checks do not invent a failed-attempt deadline. Total contact plus
acquisition may therefore take75s before failure. Version137 separated contact
and acquisition into30s each; version140 changes only new acquisition attempts
to45s. The existing5s stable-READY dwell must fit inside acquisition. A delivered
map arriving25s after appointment left insufficient scheduling/transport margin
in the previous30s budget. This policy adds no FIT/READY role, timer renewal or
authority, and can delay replacement of an unsuccessful candidate by15s.
Established HOT loss withdraws readiness immediately and permits one
original10s repair. Failed attempts use30/60/120/240/300s cooldown, retained across
reboots/source epochs of the same full identity; starts are at least5s apart.
Inventory replacement with another identity discards that old local history.
No eligible candidate means NO HOT while a healthy ACTIVE continues serving.

## Finite successor authority and recovery

Actual source, authenticated transport/map, captured precision and management
readiness must all pass. After5s sustained readiness, ACTIVE issues an original
10s successor grant, scoped to full ACTIVE/HOT identity and boot, both physical
source contexts and pair generation. Local issuer generations and received
root-scoped generation floors are separate. Retransmission/map refinement can
only shorten a grant. A cleared/retired generation cannot be resurrected.

ACTIVE reports global HOT only after the current COMMIT is acknowledged; local
HOT does not prove that ACTIVE received that acknowledgement. Lost COMMIT/ACK
is conservatively treated as possibly issued. Before appointing a replacement,
ACTIVE waits through the last issued grant plus50ms. Thus an uncertain old HOT
and a new successor never receive overlapping original grants from that issuer.
Original source permits remain independent of management. Grant expiry clears
local HOT and takeover authority immediately, even if a management lease survives.

A valid finite successor may react to explicit authenticated root withdrawal or
the original2s liveness suspicion, then completes the original3s output fence.
Its own source/incarnation and finite grant must still be valid at promotion.
Negative source creation time/epoch survives ACL cleanup in bounded inventory;
a delayed prewithdrawal positive STATUS cannot revive that exact retired root.
STATUS does not clear a same-epoch negative floor. Fresh actual positive causal
HELLO, a new source epoch or a new boot supplies its appropriate new context.
A live LoRa constraint restricts root selection even when local physical GPS is
qualified; same-incumbent acquisition may seek direct proof without granting
candidacy/output. Exact direct withdrawal retires only its original constrained
root/boot/epoch through the original constraint deadline, without rolling it.

A COLD clock has no fast-takeover authority. Silence retains the last authenticated
incumbent contact for at most120s, then a30..32s discovery round and original3s
output fence. A newer exact-root COLD/ineligible report or actual withdrawal
shortens that contact hold to at most10.05s from its first negative observation,
conservatively outwaiting any already issued10s successor grant. Repeated negative
reports cannot extend it. Simulator recovery assertions freeze the maximum surviving original contact
expiry at each actual failure, then add32s discovery and3s output fencing. A
confirmed HOT fixture bound also freezes surviving inventory expiries at failure.
It allows one30s serialized inventory round, up to three inventory identities'
fixed30s contact+45s acquisition+5s failed-attempt spacing, original10s repair,
10.05s uncertain-grant retirement,5s stability and100ms modeled control delivery.
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
| Native owner fake boundary | Actual `group_admission.c` and `ble_transport.c`, fake public BLE callbacks; sparse COLD scan/initiations/precision flags, forwarding exception OFF/ON, full inventory with ordinary RC admission, same-root inhibited bootstrap, lost/replayed phases, original5s expiry, promotion/drain/reuse, failed-attempt gate just after a COLD exchange closes, and delayed STATUS after real authenticated withdrawal/takeover. |
| Producer/PT handoff | Actual encoded JOIN/STATUS/promotion and network-selected scope create a frozen ephemeral mailbox, consumed by the real PT/radio owner in a separate executable. Fifteen cases cover delayed/early PT, current/previous mismatches, ACTIVE, direct grant priority, original anchor expiry and expired-source acquisition without output. This establishes acquisition work, not actual RF convergence or HOT readiness. |
| Hostile protocol states | Actual network owner; source/incarnation/generation mismatches, retired-generation replay, offset refinement, finite expiry with surviving management lease, COMMIT acknowledgement distinctions, history ranking and repeated candidate boots. |
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
