# T096 G5 ACTIVE, HOT and COLD

Copyright (c) 2026 Jonathan Yantis. MIT.

Version0.15.134 introduces G5 coordination for the ordinary GM profile. This is
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

## Qualification and bounded work

Independent local GNSS qualification continues while COLD, including under a
foreign LoRa constraint. The original60s physical dwell and source epochs still
reset on actual source faults. Physical eligibility, coordination inhibition,
current timing permission and output authority are distinct facts.

A COLD clock ordinarily has no persistent GM ACL, PA precision acquisition or
continuous bootstrap scan. Inventory opportunities last at most1s; the next
original opportunity is5s while discovering/no selected contact and30s with an
incumbent. Missed opportunities do not accumulate. G5 discovery visits least-recently-consumed
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
then fewer local failures, then full identity. A new attempt has one original30s
deadline. Established HOT loss withdraws readiness immediately and permits one
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
confirmed HOT bound additionally allows30s serialized inventory,30s acquisition,
5s stability and100ms modeled control delivery/owner scheduling. Real measured
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
| Native owner fake boundary | Actual `group_admission.c` and `ble_transport.c`, fake public BLE callbacks; sparse COLD scan/initiations/precision flags, forwarding exception OFF/ON, full inventory with ordinary RC admission, same-root inhibited bootstrap, lost/replayed phases, original5s expiry, promotion/drain/reuse and delayed STATUS after real authenticated withdrawal/takeover. |
| Hostile protocol states | Actual network owner; source/incarnation/generation mismatches, retired-generation replay, offset refinement, finite expiry with surviving management lease, COMMIT acknowledgement distinctions, history ranking and repeated candidate boots. |
| Native image inspection | Actual linked memory, frame/stack, controller and protected resource gates; normal LoRa static-free minimum4,644 bytes remains unchanged. |
| Hardware/off-host observation | Separate authorized exact-device tests; preserve source faults, intentional withdrawals, outages, missing readers, drops and observation denominators. Three/four-candidate simulation is not three/four-unit acceptance. |

See [diagnostics](diagnostics.md#g5-paged-observation) for the bounded off-host
observation contract and [build checks](build.md#checks) for reproducible commands.
