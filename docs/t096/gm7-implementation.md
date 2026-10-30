# GM7 implementation scope and compatibility

Copyright (c) 2026 Jonathan Yantis. MIT.

Source **0.15.209** uses broadcast GM discovery, ordinary shadow STATUS and the
existing RC G1 receive/matcher/fitter. A GM follows the selected source without
JOIN, GATT, REQUEST/ANCHOR, nonces, admission sessions or ANN permission renewal.
Normal RC client admission, GATT, RC9, relay, USB and LoRa remain supported.

The current coarse profile is **GM7v4+S2+C1+TZ1**, with JOIN revision 5 and RC
coarse control/capability revision 8. These revisions break coarse compatibility;
matching coarse revisions are required throughout the selected path. The
[W12/N30 ports](../esp32-time-zones.md) support the same metadata; other targets
require their own declared compatibility.
The [TZ contract](../../protocol/time-zone-v1/README.md) preserves G1/RC7 precision
layouts and UTC coordinates. Automatic GNSS zone/calendar selection, manual-zone
and fixed-offset modes feed the same four-byte pair and three-period presence
lease. Receivers retain coarse scanning after timing synchronization, allowing
ordered DST/unknown updates while locked. Local digital/analog clocks fall back
to UTC on metadata expiry. The [auto-TZ guide and scoped .209 results](auto-time-zone-plan.md#four-device-follow-up--29-october-2026)
retain passed direct checks and failed relay availability. Integration into main
does not certify GM failover, sustained availability or calibrated accuracy.

Ordinary RC discovery and JOIN service are enabled only for actual ACTIVE output
with the existing finite usable source envelope. Non-serving GMs keep management
discovery and scanning but cannot attract clients into a source-less JOIN.
An exchange loses eligibility through the existing asynchronous disconnect
drain, without new negotiation or extended deadlines. GM following still has
no admission exchange and keeps its own GNSS/PPS running.

## Historical diagnostic canary

The register witness described below was enabled in the .188–.196 diagnostic
checkpoints and disabled in .197. Ordinary retained crash capture and bounded
follower/HOT diagnostics remain enabled in .209. Preserve the older decoding
rules when reviewing records from their exact source version.

Version188 observes the unresolved ARM snapshot call-boundary fault. Its explicit
88B veneer frame records entry/post-inner/saved callee registers and traps an
observed mismatch before restore; it never repairs a pointer or continues timing
after a violation. This changes stack layout and may change occurrence. Clean
QEMU/native checks are not a CPU repair or endurance result.

The existing512B crash record is reused only after the prior crash's actual
payload and commit have been read back by the diagnostic owner. Pending prior
records remain complete and CRC-valid. Schema8 has a128B witness,64B thread window
(original bytes64..191 lost) and unchanged64B ISR window. Normal RC and unavailable
or inactive witnesses retain schema7/full192B thread capture. Old schema7 records
are never reinterpreted. Live witness bytes invalidate retained `diag 0100..0107`
until a new fatal record commits; the prior crash remains in the persisted journal.

Read `gm 00` and inspect `snapshot_witness`: `enabled`, `armed`,
`prior_crash_blocked`, `current`, `boot`, `sequence`, `stage`,
`capture_epoch_low32`, and `pending_crc32`. `armed` is permission after durable
handoff; `current` additionally requires a completed observation in this GM
incarnation. Raw stage low2 bits are the completed view (1entry,2post-inner,3saved);
bits4..5 identify a view still being written. Epoch low32 is descriptive only.
Bit6 records comparison completion; three captured views alone are not a checked
return. Decoder output retains unchecked differing views without inventing flags.
CRC-valid capsules with inconsistent witness semantics keep their base fault
header, registers and stack; the decoder marks only the witness inconsistent.
If blocked, preserve `pending_crc32` and inspect the existing status
`crash_persist_result`/diagnostic state. There is no export-as-ack command, automatic
deadline extension or operator bypass. Normal startup persistence acknowledges
only after actual payload+commit readback; failure remains explicitly blocked.

Since .190, startup with a valid pending crash and no append capacity may retire
the ordinary-event pin on a page whose eight records are all fully validated
foreign-source schema5 ordinary events. Current-source, crash, fault, unknown,
malformed and torn records cannot authorize this reclamation. The committed
crash page survives the existing erase, payload, commit and readback transaction;
only its successful durable acknowledgment can arm the witness. No allocation,
partition or host acknowledgment is added. A partly erased protected page may
conservatively remain full. True power loss may lose pending RAM; preserving the
previous committed crash does not recreate that pending record. Journal sequence
uses the maximum surviving committed generation after restart, not an immortal
floor from retired ordinary history; it is unrelated to mesh birth or term.

The shared controller also normalizes the periodic parent pointers in the
non-chain ULL AUX classification branch to the existing LLL representation.
Assertions, reference counts, release/flush ownership and timers are retained.
Production-body host regressions demonstrate the corrected classification and
cleanup paths; they do not prove the exact trigger of an observed controller
assertion, full Zephyr concurrency or repair of the separate CPU corruption.

Management AUX cancellation through the primary scanner now releases its
hardware timestamp capture before cleanup returns. This includes immediate
LLL AUX reception preempted by another radio event and scan-duration expiry.
The original missing release could block G1 timestamp capture while ordinary
management traffic continued. The production-body regression reproduces that
failure and verifies recovery without changing HOT or precision thresholds.

## GM behavior

An initially qualified GM elects itself after the ordinary30s quiet period
plus deterministic backoff (up to500ms). This follows the existing independent
60s GNSS qualification, so a continuous healthy cold startup normally needs
about90s plus backoff. Only the designated HOT has the fast3s takeover path.
The first valid serving reservation samples full 64-bit functional UTC mesh birth;
ACTIVE commits only after successful bounded ANN transmission. Possible RF
exposure preserves lineage even if completion fails. Subsequent replacements inherit that birth and founder and advance the leader term. When
ACTIVE meshes meet, earlier birth wins. Exact-birth ties use founder, then the
existing term/identity/boot ordering. Only the newer mesh stops output and
follows the older one. An older ACTIVE keeps serving and its existing nominee
when an unrelated newer ACTIVE appears.

Authenticated self-ACTIVE ROLE independently exposes full lineage and selects a
listening identity/boot even if every ANN is lost. It need not match a previous
ANN or any timing generation. It grants no time map, UTC accuracy or HOT rights.
Follower ROLE and legacy packets without full birth cannot decide mesh order.
ANN remains the ordinary leader/successor announcement; STATUS advertises each
member's independent GNSS eligibility and actual shadow preparation. The leader
chooses a HOT nominee and the nominee echoes HOT in its ordinary STATUS. There
is no additional nomination or acknowledgement exchange.

Functional GM shadow readiness requires the actual current same-scope map and
fit, at most2ms physical phase enclosure, unexpired correction/source evidence
and independent local GNSS/PPS qualification. It does not require the receiver's
strict precision PLL LOCK label. That label retains its125ns phase-entry,
strict200ns estimator/output histories and10s convergence dwell; actual RMS and
LOCK remain unchanged. HOT role and duration are independent. On the GM7
precision page only actual precision LOCK displays the SoftPLL badge; a HOT
designation or a good face-display RMS cannot imply that badge. RC presentation
and servo behavior remain unchanged.

GM listening stays active across control loss. Authenticated direct G1 packets
supply their own source/train/capture scope to the existing RC matcher and
causal fitter. Source/train changes retire incompatible timing fits. An
advancing direct header also supplies conservative transmitter liveness before
capture-generation or fitting qualification, so a still-transmitting selected
GM cannot cause another GM to start merely because ANN is missing. Publication
floors are compared within one source/train scope, never across a train reset.
A capture-only mismatch may inhibit duplicate output but cannot train the clock.

Each matched original/correction's actual authenticated expiry is converted
from its correction TX coordinate to the captured RX coordinate, conservatively
rounded down. Delivered time and exported maps obey that deadline and the
existing capture/matcher/fit freshness. A persistent listening identity is not a
persistent time grant. Local GNSS/PPS eligibility is independent: a GM with no
fix or PPS can receive and follow, but cannot become eligible to transmit.
Physical PPS capture and the local functional source clock continue while
following, merging, losing control traffic or changing radio ownership.
Diagnostic shadow history may be cleared on an incompatible source change;
that does not stop the physical PPS engine.
The qualified local functional map also survives silence and loss of all
neighbors. A non-HOT can use that map for ordinary initial election after the
30s quiet period plus deterministic backoff (up to500ms), without a
second preparation dwell or rebuilding its local clock. Cold election chooses
local PPS phase after the election is actually due, even if the retained remote
map has expired. A GM whose independent GNSS/PPS qualification is unavailable
keeps receiving time but cannot start output from an expired remote map.

## Timer and check policy

| Policy | Current behavior |
| --- | --- |
| Control construction / copies | Construct up to 100 ms ahead, current or next frame; queue the same immutable discovery body. The native controller executes the original window. |
| GM following | Selected identity opens listening; actual authenticated G1 data/fit expiry bounds output. ANN source-valid fields do not create a receive-permission session. |
| Silence | Last advancing selected ANN, self-ACTIVE ROLE or direct G1 header. Duplicate/reordered packets never renew it. |
| HOT failover | 2.5 s trigger, first completed G1 target by 3 s from last selected-GM communication; immutable YIELD/WITHDRAW takes precedence. Physical qualification remains open. |
| Ordinary STATUS | About 2 s cadence, 4.5 s observation lifetime. Nominee preference may survive a temporary readiness gap for 30 s; fresh HOT confirmation cannot. |
| ROLE / ANN observation | ROLE 4.5 s; ANN at most 2.75 s. Discovery observations do not extend maps or replace HOT readiness. |
| Ordinary fallback/startup | 30 s quiet plus up to 500 ms deterministic backoff after independent source qualification. Only designated HOT uses the fast path. |
| Shadow readiness | Actual same-scope delivered map/fit, live physical phase enclosure and own source qualification. Strict PLL LOCK is a separate precision label. |
| Physical phase | Live 2 ms; retained 8 ms / 5 s limits for safe reservation/promotion, separate from ordinary visibility. |
| Visibility without remote fit | Local functional PPS phase or best-effort native HF windows; neither grants remote precision. Management discovery remains enabled. |
| RX / precision record overflow | Count/drop management observations; retire incompatible GM timing tuples/fit and reacquire. A missing record is not a capture-domain failure. Ordinary RC policy retains its own guards. |
| Independent GNSS qualification | 60 s dwell plus actual source/PPS bounds; control traffic cannot qualify GNSS or invent UTC accuracy. |

The same existing PT handoff snapshot can be refreshed while listening, only on
advancing accepted G1 (or a genuinely new qualified timing scope). Its original
sample and prediction bounds do not renew on a poll. Transfer acknowledgement
is published only after actual stop/drain. Control expiry does not force that
stop. Fresh selected-GM communication cancels a pending silence-only takeover;
explicit YIELD/WITHDRAW retains its immutable stop-before-start semantics.

YIELD retains its750ms boundary and three copies250ms apart; WITHDRAW stops
output synchronously and attempts three immutable copies within its original1s
negative lifetime. These bounds coordinate output ownership rather than control
packet precision. Late or missing copies cannot slide the stop boundary. Real
hardware capture, source expiry, capture-domain continuity, group authentication,
identity/boot ambiguity, terminal/term floors and no-double-output checks remain.

Processing snapshots are separate from capture-domain continuity. Completion
handling returns its refreshed bridge to the service; a late older snapshot or
an unavailable post-MAC clock sample rejects that operation without erasing the
mesh. No rejected sample can publish time, advance a control floor or replace
the original captured RX receipt/expiry. A failed completion-time read consumes
no callback, allowing the same original completion to be reconciled later.
STOP and withdrawal always suppress output; an unavailable processing sample
cannot invent an RF negative timestamp. Real capture-epoch changes still stop
and drain the service through its existing clock-domain fence.

## Wire, diagnostics and replay limits

Normal builds use `G7V4`, revision 4, domain `PTOBTZ04`, and diagnostic identity
`GM7v4+S2+C1+TZ1`. ANN/STATUS/WITHDRAW are 182-byte bodies / 186-byte AD records;
ROLE/GNSS keep 168/156-byte bodies under the same selected revision/domain.
JOIN5 and RC coarse8 carry TZ; GR3 and RC7/profile5 precision bytes are unchanged.
The [TZ contract](../../protocol/time-zone-v1/README.md) defines exact offsets,
domains and runtime revision rejection. Source-valid fields remain on wire but
do not create a GM receive-permission session. Frozen
[S2](../../protocol/gm7_simplified_s2/README.md),
[C1](../../protocol/gm7_birth_c1/README.md),
[A1](../../protocol/gm7_scheduler_a1/README.md) and earlier contracts preserve
historical identities. Their superseded implementation choices do not override
this current operator-directed scope.

Peer screens distinguish authenticated heard, current control observation,
reported ROLE and actual current timing. ROLE durations stay values reported at
the sender sample; queries never advance those durations. Existing journal
records include packet-kind RX/TX outcomes and asymmetric merge. No new log
collector or diagnostic ring is required.
The existing `gm 00` page includes `owner_failed`, `time_rejects` (rejected
processing snapshots or invalid activation windows), and `local_time_source`
(`NONE`, `FUNCTIONAL_UTC`, or `GNSS_PPS_MAP`). The source category describes a
current local map, independently of the GNSS qualification dwell and received
BLE timing. It grants no output or precision qualification. Genuine owner
failure retains its cause, reports a network error and displays `OWNER FAULT`.
These fields reuse the existing owner counter and diagnostic/UI storage.

Receipt freshness authenticates membership and integrity, not the original age
of an unseen recording. Retained sequence/sample/publication and terminal floors
reject known repeats; they cannot prove the age of arbitrary unseen first
contact. That ordinary broadcast limitation is accepted and is not UTC accuracy.
Peer memory is bounded: expired unprotected incarnations can be reused, while
selected/nominee/radio-owned entries and live boot ambiguities are protected.
An evicted incarnation becomes first-observation-limited again.

## Offline checks and hardware limits

Use the public dependency and build commands in [the build guide](build.md).
The GM7 host runner exercises actual codec, owner, scheduler, controller and
service bodies with explicit fake platform/RF boundaries. The PT recovery runner
adds actual G1 encoding/decoding, correction matching, causal fitting, immutable
data expiry, no-GNSS reception, raw liveness, replay/scope negatives and real
handoff snapshot/drain behavior:

```sh
python3 firmware/t096/tools/gm7_tests.py --deps artifacts/deps --out artifacts/gm7-host-01
python3 firmware/t096/tools/gm7_pt_recovery_tests.py --deps artifacts/deps --out artifacts/gm7-pt-01
python3 firmware/t096/tools/gm7_cohort_tests.py --deps artifacts/deps \
  --out artifacts/gm7-cohort.json --physical --nodes 4 --duration 260 \
  --connect-after 100 --announce-after 112 \
  --owner-delays-us 15000,15000,15000,82000,15000,15000,54000
```

The composed cohort runs actual separate GM and PT owners, with modeled native
completion, RF capture, collision, loss and controller preemption. Its explicit
partition reconnects; a permanently partitioned fixture is not a collision
reproduction. Delayed owner cadence and completed packet counts are reported.
Models and native builds do not establish field convergence, physical HOT
failover, calibrated output accuracy or endurance. The observed .185 split-GM,
no-confirmed-HOT result and separate unexplained CPU faults remain failed/open
hardware acceptance. No CPU cause or repair follows from this control rework.

Enrolled images stay private. Installation still needs separate authorization,
exact-device ownership/recovery review, app-only bounds, actual new application
readback and protected-prefix comparison. Offline preparation operates no
hardware and does not authorize flashing.
