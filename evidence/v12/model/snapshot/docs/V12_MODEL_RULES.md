# V12 offline model rules, October 6, 2026

The operator authorized a checkpoint of existing changes, implementation and
stress testing of a V12 offline proposal, rule updates, and a physical test plan.
This assignment opens no serial ports and deploys no firmware. Earlier physical
authorizations do not turn a synthetic result into an installed V12 release.

Use the existing lab at `reviews/v10-model-20261005-01`, extended with
`harness.py v12`. The tracked implementation is `tools/v12_model.py` and the
runner/installer is `tools/v12_sim.py`. Frozen snapshots and `model-registry.json`
remain the V11 source regression baseline. Do not overwrite old receipts or
rename an old model result as V12. Keep all cases, including zero reception,
bound counterexamples and numeric-screen failures, in fresh ignored output IDs.

| Rule | V12 executable scope | Physical evidence still required |
| --- | --- | --- |
| At most four directly connected GMs, coordinator included | Initial real candidates TC-01, TC-03, C-3; a fourth synthetic GM and overflow fixtures; fixed TC-03 coordinator. Overflow and indirect candidates forced client, no control forwarding | Exact installed identities, deterministic authorized slots, direct authenticated coordinator links, provisioning and source capability |
| Source fitness is separate from coordination | Declared 60s source/boot wait, three advancing qualified source reports within6s, original evidence age; UNKNOWN means no precision claim | Native source mapping, coherence, qualification and GNSS reset behavior |
| One publisher under declared bounds | PREPARE/READY, COMMIT/ACTIVE; sticky healthy incumbent; STOP/STOPPED or original lease/drain fence | Real disabled/drained train acknowledgement, deadline mapping and timer/stop bounds |
| Delivery is separate from semantic acknowledgement | Whole110B records; per-direction bounded queue; retries retain original deadline; uncertain ACTIVE inhibits renewals | ATT/ACL receipts, application transition trace, encryption/provisioning and link recovery |
| A retired command cannot rearm publication | Authenticated role/boot/coordinator/challenge/config/sequence, retired decision, fresh READY for new activation | Durable boot/incarnation policy; reset, replay, reconnect and sequence exhaustion on firmware |
| Reboot cannot erase outstanding authority | Fixed coordinator quiet interval at least max permit+drain; existing permits expire locally | Boot guard cannot be bypassed by repeated reset; conservative local-to-coordinator map |
| Unknown bounds refuse authority | Unknown map or controller stop bound rejects PREPARE/COMMIT | Independent map/drain/rate measurement over load, temperature and reboot |
| Public precision remains passive | No client keys, election or connection needed for PADVB timing | Public PA sync/discovery capability and actual physical capture association |
| One hop extends timing only | Direct parent train; separate periodic relay train31250us off parent phase; root origin/boot/source epoch preserved; relay-of-relay refused; relay has no GM authority | Verified extension authorization, native relay TX/RX association, clock mapping/residence calibration and total path uncertainty |
| Mesh freshness cannot be restarted at relay | Original upstream reference age ≤500ms and original conservative permission deadline at relay TX and downstream correction; expired evidence becomes finite HELD output, never fresh authority | Real origin/epoch/revoke propagation, expiry under blocked work and independent far-client output |
| Clients receive at ≤50% with a contiguous quiet window | Quiet profile uses two run groups500ms apart; eight originals62.5ms apart with repeated correction history; last two maintain upstream freshness; coordinated350/400/450ms window follows either group, with all RX guards inside; ≥1 usable corrected update per warmed second is scored | Controller sync skip/window support, actual BLE shutdown/rearm, one-hop token/phase, whole blackout held output and coexistence |
| Corrections require exact originals | Root/boot/train/source epoch/event scoped capture; bounded3s original/replay lifetime; repeated payload ambiguity rejects | Native event identifier, TX/RX timestamp association and controller ABI invariants |
| Listening and radio use are separate ledgers | Generic profile ≤500ms RX/1s and ≤3000ms union/6s; native W12 always-online treatment removes these administrative caps, retains half duplex and measured ledgers; half-duty clients keep ≤50% | Actual acquisition/rejoin and steady-state controller/RF trace; CPU/heap/stack/energy |
| Predictions are causal | Corrected references become usable only after modeled work; no hindsight; bias retained in synthetic RMS/P99/max; unavailable grid/gaps retained | Delivered hardware output against calibrated independent truth |
| Missing evidence cannot pass | `independent_precision=null`, quality UNKNOWN, PTS OFF in every result | ≤500ns total held error+uncertainty over whole actual PTS blackout plus margins |

A/B are fixed-source passive transport fixtures; C/D add the proposed connected
coordinator. A/C's paired legacy transport is a declared abstraction. Use the
existing native V11 proof for firmware regression, never describe the four-mode
synthetic comparison as measured V11 performance. Run both matched cadence
comparisons and the earlier125ms PA versus1s legacy profile; current cycle proposes62.5ms PA. Acquisition,
source readiness, emitted-opportunity coverage and output availability are
separate denominators. Never omit an unavailable receiver from a pooled score.

The simulator uses an integer virtual clock, finite event queue, declared RF
loss, declared source flags, a causal32-reference affine estimator and declared
native20MHz capture quantization. Radio collision intervals, discovery airtime,
connection occupancy and payload sizes are hypotheses. Background scan windows
are conservative budget reservations and can be preempted by selected jobs;
this is not linked controller scheduling. Up to three continuous direct ACL links are assumed
already authenticated/bound. Boot faults model immediate new authenticated
bindings; establishment, identity protocol and reconnection duration remain
physical work. Coordinator loss intentionally reduces availability; no quorum,
Byzantine-peer exclusion, second-hop extension, PAwR, PTS or formal Bluetooth/IEEE
conformance is established. The one-hop model assumes a verified timing-extension
token observed directly from the root; its public-verifiable wire/security scheme
and physical bounds remain unimplemented. Root packet cost is198B maximum; relay
cost222B preserves four corrections plus origin metadata, a separately versioned
packing hypothesis. This is root-derived boundary time extension, not proof of
transparent-clock residence correction or an independent-source relay.

The stress runner distinguishes valid-bound invariant failures from deliberate
false-bound counterexamples. A suite safety pass means no unexpected invariant
failure in those finite cases. It does not mean every listener meets reliability
or numeric targets. Fake bounds must produce retained failures; never change
the thresholds or hide cases to make a headline pass.

`harness.py v12 quiet-stress` tests the dedicated client duty profile. Missing
update seconds, offered-opportunity coverage, fresh grid availability and
synthetic held error through the quiet window are different metrics. A held
prediction is not fresh source evidence. One-hop500ms origin freshness still
expires during longer quiet windows; PTS remains OFF until whole-blackout total
held error plus uncertainty is physically qualified within500ns. GM connections and time relays remain online for the whole second. Only public
clients may go quiet. Full precision clients and GM follower witnesses consume
both500ms groups; the root is not given fabricated self-reception scores. Do not claim quiet RF from a host mask.

For reproduction and physical staging, read
[the V12 plan](V12_PERIODIC_COORDINATION_PLAN.md). Apply the existing identity,
source/controller/precision, durability, ownership and sealed-evidence rules
before any later physical work. Active coordination is a new requested profile
with explicit traffic costs; it is not exempted from measurement by the generic
no-extra-RF durability-fix rule. Freeze a separate migration budget before paired
physical runs; no partition resizing or controller guard bypass is authorized.


The updated native lab profile is `config/v12_native_lab_profile.json`. Its
62.5ms train captures originals in all eight slots per500ms group. Six are
primary client opportunities (0 through312.5ms);375/437.5ms also refresh the
always-online relay source while repairing previous corrections. The relay is31.25ms off phase. A declared
5s periodic sync timeout avoids declaring synchronization lost after a single
sleeping-client miss; it does not extend source freshness, permission or output.
The updated profile refuses output until at least four causal references span
250ms; legacy fixture defaults retain their old two-row assumption. Startup
refusal is recorded as UNAVAILABLE, never erased from the coverage denominator.
FRESH, HELD and UNAVAILABLE grid counts are separate. HELD output expires1.5s
after the last physical reference, preserves original origin age/permission, and
creates no new RF authority. Score all available held output in RMS/P99/max,
including quiet cells, rather than scoring only the fresh subset. The1.5s horizon
is a model hypothesis, not a physical holdover qualification.

`harness.py v12 qualification` requires every nominal public listener to receive
a corrected update in every warmed second, full clients and GM followers to
receive one in each complete500ms source window, and all delivered numeric
screens to pass. The synthetic root's self-output is unmeasured. Initial10s and
final1s boundary accounting is explicit; complete source windows have a separate
denominator. Independent10% loss treatments target≥99.9% reception across their
predeclared per-treatment/path/mode groups; never pool these with nominal or
fault runs. Burst/outage/reboot/unknown-bound cases score safety and retain all
misses and numeric failures, rather than asserting uninterrupted reception.
No finite simulator pass guarantees reception through arbitrary RF interference.

TC-02 retains its existing field image; no lab write or control participation.
Direct-coordination bench: TC-03 coordinator/local candidate, TC-01 preferred
source, C-3 direct independent-source candidate, C-1 public client. One-hop bench:
fence and withdraw C-3 GM control before switching it to an always-online
time-only relay, with C-1 as the shielded far client. TC-01/TC-03 GM links remain
direct. The fourth slot stays unassigned; GM4/5/6 are synthetic fixtures. Do not
add a fifth physical receiver to satisfy this assignment. Removing W12 legacy
radio throttles is permitted only as a separately versioned native BLE lab
treatment with actual RF/resource measurements; it cannot establish native
timestamp association or quiet RF support. Controller ABI and all precision,
source and authority gates remain unchanged.

`config/v12_native_lab_profile.json` models three direct GMs and C-1 half-duty
client; `config/v12_four_unit_mesh_profile.json` models TC-01/TC-03 direct GMs,
C-3 time-only relay (logical fixture ID11) and C-1 far half-duty client. The
qualification suite tests this exact four-unit mesh in addition to larger
synthetic cap/fanout populations. No simultaneous C-3 GM/relay role is inferred.
