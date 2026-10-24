# GM7 implementation scope and compatibility

Copyright (c) 2026 Jonathan Yantis. MIT.

GM7 changes **GM-profile election and HOT coordination**. RC remains a normal,
supported profile. Preserve ordinary GM-to-RC GATT time admission, RC1/RC2
relaying, UCS1, RC9 and the existing client service contracts. Remove the GATT
exchanges and ownership used for GM-to-GM HOT/coordinator work only.

This is the current implementation scope following the operator's clarification.
It supersedes broader client-migration and blanket GATT-removal requirements in
the frozen [firmware clearance](gm7-firmware-clearance.md). The frozen review
artifacts remain historical engineering inputs; they do not override this scope
or establish firmware-image or hardware acceptance.

## GM behavior

Retain initial GM election. GM-profile standbys follow the current GM's timing
stream, maintain their own shadow readiness and independent GNSS eligibility,
and send periodic status. The serving GM advertises its preferred HOT nominee;
that nominee echoes its armed state in its next ordinary status. HOT appointment
has no coordinator, grant or GATT handshake.

Mesh birth follows [birth amendment C1](../../protocol/gm7_birth_c1/README.md):
the first independently qualified elected GM samples actual functional UTC at
its first valid reservation, pending a complete ANNOUNCE. It is logical mesh
initiation, separate from later service and ACTIVE duration. The value is
stored as full 64-bit UTC microseconds. Surviving replacements retain that birth
and founder while advancing the leader term. The existing 32-bit timing token
remains separate. Older mesh birth wins when active meshes meet; the losing GM
stops its positive output before following the winner.

The [implementation2 wire contract](../../protocol/gm7_v2/README.md),
[ACTIVE discovery amendment A1](../../protocol/gm7_scheduler_a1/README.md) and
[bootstrap endpoint amendment B1](../../protocol/gm7_bootstrap_b1/README.md)
preserve their frozen identities. B1's optional host endpoint mode does not
authorize current client firmware changes or relax T096 control/HOT RF timing.
YIELD and WITHDRAW retain three immutable primary copies; copies cannot move
their original timing or permission deadlines. Radio delivery remains subject
to loss, with blackout handling as the fallback.

## Existing clients and services

Keep normal client JOIN authentication and its wire format. The GM-side serving
snapshot must come from current GM7 source truth and preserve original expiry,
nonce, boot, timing-token, capture and train fences. Client JOIN cannot appoint
HOT or promote a GM control connection. Preserve G1 publication and GRv3 original
root proofs and held horizons so existing RC clients can receive and relay time
without a firmware migration.

Keep normal RC runtime dispatch, source selection, GATT services, UCS1 and RC9.
Verify bounded GM/RC switching and maintenance, including stale callback fencing
and service restoration. Client HOT target preloading is a later, optional
extension; W12, N30, SS3 and CoreS3-SE firmware/UI migrations are outside this
implementation.

## Diagnostics and display

Update GM-local versioned diagnostics and screens together. HOT duration belongs
to the armed owner and survives the supported short received-map gap; PPS and
ACTIVE durations retain their own provenance. Screen refreshes and diagnostic
queries cannot reset timers or renew timing permission. Show UTC mesh birth,
founder and leader term separately from the timing token and precision metrics.

Preserve useful source and quality information for existing diagnostic consumers
through a GM-side compatibility adapter. Existing GS1 requires a coordinator
when its HOT bit is set, so compatible GM7 serving reports leave that legacy HOT
bit clear and coordinator zero. GS1 cannot describe a GM7 HOT nomination. Keep
GD2's original quality meanings and unknown values; do not reinterpret its
fields as GM7 quality ordinals. Full GM7 HOT details use the new diagnostics and
updated GM-local and GM-peer UI. Existing RC/client HOT rendering remains
deferred until a separately scoped client extension is implemented. GM peer
ROLE durations remain sender-reported values at their sample; a receiver never
turns them into an observed continuity timer. Revoked nomination, expired STATUS,
changed root/train/boot/term or retired association clears the current HOT label.
The existing selected-root GD2 GNSS/quality page and unknown values remain;
detailed GNSS history for every peer is unavailable.

## Implementation and deployment checks

Require production-body election, freshness, handoff, source-expiry, duplicate,
schedule, diagnostic and timer tests; native GM/RC/DC/PC builds and resource
inspection; unchanged-client JOIN/G1/RC1/RC2 interoperability checks; and bounded
role-switch/maintenance checks. Native control integration must account for
complete primary/AUX events and actual RF start/end, including the first serving
timing transmission after takeover.

Flashing is conditional on the finished candidate's review and fresh exact-device
identity, ownership, recovery and app-only write/readback checks. Offline tests,
reference schedules and native builds do not establish radio delivery, the
physical three-second takeover target, endurance or calibrated UTC accuracy.

## Implementation and repeatable offline checks

Source0.15.170 selects GM7 by default for both normal GM/RC image defaults. The
RC backend remains ordinary supported firmware. Historical GM5/6 is available
only with `--legacy-gm-regression`; there is no runtime fallback. DC and PC omit
GM7. Enabling management-only LE2M does not enable public controller2M features:
ordinary G1/relay advertisements retain LE1M and ordinary ACL defaults remain
unchanged.

`gm7_owner` owns qualification, election, nomination and scope floors;
`gm7_session` owns fresh nonces and conservative causal intervals. Neither GATT
JOIN nor diagnostics can appoint HOT. Normal GM runtime has no legacy network
receive/tick owner and starts no outgoing HOT/coordinator GATT connection. The
zero-initialized, short-lived JOIN compatibility facade supplies current GM7
source truth to the original authenticated client protocol. Its full transient
expiry, original permission, nonce and drain checks remain. Legacy GM5 helper
symbols can remain linked through shared compatibility source; this does not
make a live GM coordination owner.

The dedicated controller reserves three immutable primary/AUX event contexts,
three user ticker nodes/thread operations and two additional event-done buffers.
Primary management uses channel37/LE1M; AUX uses a named data channel/LE2M with
an unchained complete event bounded by2ms and at most2ms start lateness. Aligned
passive management scan pins37 through an acknowledged stock scanner restart;
phase-unknown discovery rotates broadly. Capture routes share the existing
TIMER3/epoch extension: exclusive ADDRESS CC1 and separately allocated END CC5,
with CC0/2/3/4 owners preserved. HCI submission/callback times are never RF proof.

A1 prequeues its ordinary secondary ANNOUNCE copy with the immutable primary.
STATUS slots hash full identity, boot and changing cycle, with a bounded
SHA256-derived jitter in[-250,+250]us. A completed REQUEST freezes its next
valid hashed slot/jitter before computing the earliest rotating-residue frame
at least2s later (nominal less than3s). STATUS/capacity deferral keeps that
frozen slot/residue and records lateness, without inventing an on-air attempt.
Response admission never rolls into a later frame to bypass reserved secondary.

C1 provisional scope cannot enable G1, client source admission or an ACTIVE
timer. Complete first ANNOUNCE commits the scope; ACTIVE duration starts at
actual service commit processing time. Its measured primary START/final AUX END
remain separate. Possible AUX exposure retains birth/founder/term on failure
and emits a terminal WITHDRAW; missing END cannot create a fresh replacement
birth. First usable G1 is prospectively anchored on a safe62.5ms UTC phase and
requires an observed complete unchanged G1 packet with captured ADDRESS and a
conservative derived END enclosure. A takeover succeeds offline only when both
management and first timing event satisfy their original deadline. Live phase
nudges occur at most once per distinct captured G1 and remain within the declared
SCA displacement budget. GNSS/PPS/source faults fence immediately.

Shutdown/maintenance fences time output immediately and retains a healthy-clock
management owner long enough to attempt the three original WITHDRAW copies.
Actual completed, failed and unsent counts are distinct from queued copies.
Clock/radio failure can make copies unavailable; the service drains and reports
that fault without fabricating transmission. Public status/PT/console/UI times
stay in kernel uptime; wire/session/controller times stay in HF. Contemporary
paired brackets convert remaining horizons conservatively in either direction.

Run ordinary verification with public pinned dependencies:

```sh
CC=clang artifacts/deps/venv/bin/python firmware/t096/tools/test.py \
  --deps artifacts/deps --out artifacts/test-gm7-01
artifacts/deps/venv/bin/python firmware/t096/tools/build.py \
  --deps artifacts/deps --out artifacts/build-gm7-gm-01 --profile GM \
  --enrollment artifacts/local/enrollment.json --device-config artifacts/local/device.json
artifacts/deps/venv/bin/python firmware/t096/tools/inspect_build.py artifacts/build-gm7-gm-01
artifacts/deps/venv/bin/python firmware/t096/tools/gm7_resources.py \
  --build artifacts/build-gm7-gm-01 --deps artifacts/deps \
  --out artifacts/gm7-resources.json
```

Use the documented RC/DC/PC build commands for their respective separate defaults
and inputs. `--development` produces `DEVELOPMENT_NOT_FLASHABLE`; it does not
bypass or relabel the clean-source candidate requirement. The strict inspector
checks unique compiled native owners, exact input/output hashes for all four
GM7-composed generated controller sources, inherited transformations, RF PHY
feature isolation, image bounds and unchanged resource floors.

The production host runner exercises independent frozen vectors, causal renewal
and replay fences, election/merge/takeover/YIELD/WITHDRAW,3GM+1RC synthetic scope,
peer retirement, local and reported timers, native HAL/ticker/LLL lifecycle and
actual GM service bodies under explicitly fake physical/kernel/PT boundaries.
It tests normal unchanged-client JOIN codecs/exchanges under ASan/UBSan. Synthetic
80x160 peer previews include maximum finite, unknown, history and current cases.
These are code/contract checks, not RF or calibrated timing observations.

## Resource and qualification limits

The last incremental GM7 link used252,828B of253,952B application RAM, leaving
1,124B linker headroom. That is not runtime stack/heap qualification. Main/GM/PT/RC
stacks and5120B system-workqueue/2048B ISR reserves remain. Heap pool size remains0;
GM7 adds no malloc/calloc/realloc/k_malloc allocation. Existing RX8, ACL TX12,
event RX20, command TX1, connection6, extended advertiser3 and periodic-sync1 pools
remain; the explicitly named controller additions are accounted above.

Compiler `.su` and linked direct-call evidence are generated by
`gm7_resources.py`; indirect callback/exception/assembly allowances require
review and runtime high-water remains a later hardware observation. Large GM
source/timing/publish helpers have explicit call-frame boundaries so their
mutually exclusive scratch space is not all inlined into the JOIN owner frame.

The finite200ppm session envelope and named phase components are explicitly
unverified engineering operating assumptions. The public T096 schematic does
not identify HFXO crystal tolerance, temperature or aging. Retained HFXO selection
and lifecycle are verified in source, not measured oscillator performance. The
functional phase enclosure includes TX/RX chain ceilings, quantization, paired
association, declared100us propagation and250us implementation allowances plus
200ppm age growth and actual recorded residuals. Recorded RMS/max residuals do
not establish future UTC error. Unknown required components fail closed.

Final clean native GM/RC/DC/PC receipts and the complete source-frozen host
matrix are pending at this checkpoint. B remains exact-device/source/image/
transaction review; C remains physical RF airtime/coverage,3s takeover, loss,
partition, output continuity and calibrated UTC qualification. No device has
been opened or flashed by this implementation work.
