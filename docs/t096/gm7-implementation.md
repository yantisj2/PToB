# GM7 implementation scope and compatibility

Copyright (c) 2026 Jonathan Yantis. MIT.

Source **0.15.189** uses broadcast GM discovery, ordinary shadow STATUS and the
existing RC G1 receive/matcher/fitter. A GM follows the selected source without
JOIN, GATT, REQUEST/ANCHOR, nonces, admission sessions or ANN permission renewal.
Normal RC client admission, GATT, RC9, relay, USB and LoRa remain supported.

Ordinary RC discovery and JOIN service are enabled only for actual ACTIVE output
with the existing finite usable source envelope. Non-serving GMs keep management
discovery and scanning but cannot attract clients into a source-less JOIN.
An exchange loses eligibility through the existing asynchronous disconnect
drain, without new negotiation or extended deadlines. GM following still has
no admission exchange and keeps its own GNSS/PPS running.

## Diagnostic canary

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

## GM behavior

An initially qualified GM elects itself after the ordinary30s quiet period
plus deterministic backoff (up to500ms). This follows the existing independent
60s GNSS qualification, so a continuous healthy cold startup normally needs
about90s plus backoff. Only the designated HOT has the fast3s takeover path.
The first successful reservation records full 64-bit functional UTC mesh birth;
replacements inherit that birth and founder and advance the leader term. When
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

| Policy | Before .186 | .186 behavior and reason |
| --- | --- | --- |
| Control construction | Primary3–25ms; STATUS/ROLE/copy3–7ms before slot | Up to100ms ahead, current or next frame. Native controller executes the original immutable window. |
| Discovery copy | Wait for primary completion, retain it21ms | Queue the same immutable primary body ahead of time; no completion dependency or retention timer. |
| GM following | ANN source permission at most1.5s; source-expiry preclose | Removed. Selected identity opens listening; actual authenticated G1 data/fit expiry bounds output. |
| Silence | Last ANN only | Last advancing selected ANN, self-ACTIVE ROLE or direct G1 header. Duplicate/reordered packets never renew it. |
| HOT failover |2.5s trigger, first G1 by3s | Retained, measured from last selected-GM communication; immutable YIELD/WITHDRAW takes precedence. |
| Ordinary STATUS | About2s,4.5s observation lifetime | Retained. Allows a missed ordinary report plus delayed worker service; bounded nomination observations are separate from data permission. |
| ROLE observation |4.5s and prior matching ANN/generations |4.5s independent bounded discovery observation. It cannot extend a data map or overwrite HOT qualification. |
| ANN observation | At most2.75s, coupled to source lease |2.75s bounded leader/successor observation spans many250ms reports and the2.5s HOT trigger. It cannot block authorized3s takeover or continuous G1 following. |
| Ordinary fallback/startup |5s quiet plus backoff |30s quiet plus existing backoff; user-requested conservative behavior in uncertain RF. Only designated HOT uses the fast path. |
| Shadow readiness | Correction at most250ms old plus continuous5s dwell | Actual GM follower delivered map/fit and live growing phase bound; remove the extra dwell and one-period cliff. Since .189 strict PLL LOCK remains a precision label, independent of functional HOT readiness. Historical blocker bit32 now means missing fit, not an unlocked strict PLL. |
| Physical phase | Live2ms; retained8ms/5s | Retained physical limits for safe reservation/promotion, separate from ordinary visibility. |
| Ordinary visibility without remote fit | Suppressed | Use local functional PPS phase, or best-effort native HF windows. These windows make no mesh-UTC/precision claim. GM37 discovery is continuous. |
| Management RX overflow | Permanently fence clock | Count/drop the lost observation. Existing bounded buffers remain. |
| GM precision record overflow | Permanent stop/error | Retire tuples/fit and reacquire through the existing receiver. A missed record is not a broken capture clock domain. Ordinary RC policy remains unchanged. |
| Independent GNSS qualification | Existing60s dwell/source/PPS bounds | Retained; control traffic cannot qualify GNSS or invent UTC accuracy. |

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

Wire revision/domain3 and the `GM7v3+S2+C1` diagnostic identity remain compatible
with .185. Existing source-valid fields are still decoded/emitted for wire
compatibility; they no longer create a GM receive permission session. Frozen
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
