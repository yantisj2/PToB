# T096 sync diagnostics

Copyright (c) 2026 Jonathan Yantis. MIT.

Version 0.15.56 adds a production RAM event journal for both GM and RC. Earlier
versions expose `network`, `precision`, `rc-parent`, `clock`, `join`, health and
watchdog snapshots, but do not retain a complete sequence of sync transitions.
Fixture `printk` traces are not the USB console in ordinary builds.

Version 0.15.57 quiets routine output. Health and watchdog records appear at
most once per minute while unchanged; fault/counter/retry changes remain prompt.
Empty fault capsules are omitted; nonempty retained capsules stay available in
the sparse reports. HOT reports keep state/blocker
changes and a one-minute heartbeat. Full host snapshots default to every 30
seconds, with event cursor polling every five seconds and continuous serial
draining. These settings reduce observer load without suppressing transition
or failure records. Health and watchdog output uses the idle-only diagnostic
path, so it cannot enqueue another record behind an outstanding command reply.
The two-slot queue and 3072-byte command capacity are unchanged. Diagnostic
notes allow up to 1536 bytes to retain real fault capsules.

Version 0.15.58 adds passive initiator counters in the exact pinned controller.
They distinguish configured target, native scan preparation, CRC-valid received
PDUs, eligible target advertisements, buffer/window refusals and prepared
connection requests. No hook changes a controller decision, deadline or radio
register. Counters are saturating, copied with IRQ exclusion and exported once
per connection result, rather than printing from an ISR. A prepared request is
not proof of on-air transmission or remote receipt. Match/request/refusal
counters observe legacy initiating advertisements, as used by the T096 GM;
connections established through AUX_ADV_IND use a separate native path.

Version 0.15.59 uses7.5ms cold-admission ACL events. A15ms connection interval
can produce120ms causal proof rounds: with a40ms source bound, the existing
uncertainty calculation exceeds100ms. Faster bootstrap leaves more margin
without relaxing that gate. Same-scope renewal remains50ms.
It also rejects G1 passive-client hints as direct GM candidates and
requires a connectable version1 report. Separately authenticated RC7 relay
capabilities remain available; direct and relay candidates use distinct keys
so a later G1 hint cannot change the type of a retained relay capability.
An initiating cancellation that races the native host timeout may return
EACCES. That return alone no longer becomes a terminal owner fault. The exact
reference stays owned until its real completion callback, with one500ms drain
deadline. A successful connection that races cancellation is disconnected
without entering MTU/JOIN. Missing/late callbacks remain failures, and shutdown
cannot extend a drain already in progress. No authentication, source expiry,
uncertainty or acquisition gate is relaxed.

## Events and interpretation

The journal holds the newest 32 events (1,280 bytes of records). It survives
logical owner restarts within one MCU boot. It does not survive a power loss or
hardware reset. Callbacks copy one fixed record; main formats and exports it.
There is no flash logging, allocation, RF operation or recovery action in this
observer. A slow reader can lose history: `overwritten`, per-cursor `missed` and
sequence exhaustion are explicit. Sequence numbers never wrap within a boot.

The read-only, source/silicon/boot-bound command is:

```text
r2 REQUEST8 SILICON16 SOURCE64 BOOT16 events CURSOR8
```

Use cursor `00000000` to read the oldest retained event, then use its `sequence`
for the next request. The ordinary CRC-framed reply includes `found`, `last`,
`overwritten`, `exhausted`, `missed`, `sequence`, 64-bit uptime `ms`, `kind`,
signed `result`, `peer`, `detail`, `a` and `b`. A cursor belongs to one exact
source and MCU boot; start a new segment after either changes. CRC checks
transport integrity, not authenticity or timing accuracy.

| Kind | Meaning | detail / a / b |
| --- | --- | --- |
| 1 / 2 | State transition / 30-second unchanged-state heartbeat | flags / accepted / rejected |
| 3 | Root or selected scope changed | source epoch / root boot low / root boot high |
| 4 | Parent selected | relay flag / signed RSSI encoded as uint32 / reserved |
| 5 | Periodic-sync create result | SID / supervision timeout in 10ms units / reserved |
| 6 | Periodic sync established | SID / acquisition age ms / reserved |
| 7 | Periodic sync terminated; result is HCI reason | was established / cancel count / termination count |
| 8 | Sync cancel result, including deadline failure | was established / acquisition age ms / reserved |
| 9 / 10 | RC create / connected result | transport phase / attempt age ms / attempt count |
| 11 / 12 | RC operation error / local close request | phase or close cause / attempt age ms / attempt count |
| 13 | RC disconnected; result is HCI reason | phase / attempt age ms / server flag |
| 14 | GM admission result | source flags / causal proof round us / coarse uncertainty us |
| 15 | First owner fault | CRC32 of source reason string / reserved / reserved |
| 16 / 17 / 18 | GM connected / disconnected / MTU error | slot and flags / incarnation / reserved |
| 19 / 20 | Held map expired / accepted discipline resumed | count / discipline gap us / reserved |
| 21 | HOT blocker/withdrawal changed; result is withdrawal reason | blockers / hello width us / withdrawal count |
| 22 | Last prepared initiator target; peer remains the selected stable hint | address type plus channel at bit8, saturation bit31 / address bytes0..3 LE / bytes4..5 LE |
| 23 | Per-attempt native initiator reception | prepared scan windows / CRC-valid PDUs / eligible target advertisements |
| 24 | Per-attempt connection request preparation | prepared requests / insufficient window refusals / RX-node refusals |
| 25 | Per-attempt initiating scan preemptions | native abort count / reserved / reserved |
| 26 | RC close completion; result0 confirms callback-observed drain, timeout is a failure | reserved / remaining original drain budget ms / reserved |
| 27 | RC control retry scheduled | reason:0 failed attempt,1 PENDING,2 exhausted batch,3 create API error / delay ms / attempt count |
| 28 | RC advertising/gate transition | flags / requested discovery interval in625us units / reserved |
| 29 | Native RX reserve at a legacy initiating refusal | required nodes / last free nodes / lowest free nodes seen at a refusal in this MCU boot |
| 32 | Native GM control progress; signed result retains API/protocol error | stage / stage value / stage value |

Kind32 stages are: 1 create accepted (ACL interval in1.25ms units / local
connect budget ms; zero means no local cap), 2 local connect-deadline cancellation
(connection incarnation / reserved), 3 JOIN begin (server flag / original
lifetime ms), 4 JOIN send accepted (record kind / remaining original lifetime
ms), 5 decoded JOIN receive (record kind / control flag bit0 and server bit1),
6 exchange retirement (last pending kind / expiry bit0, disconnected bit1,
changed peer bit2, close deadline bit3), 7 control promotion (slot / control
flag), 8 MTU exchange callback (cached negotiated MTU / incarnation),
9 characteristic discovery (handle / incarnation), and 10 marker-read callback
(length / incarnation), 11 first native G1 discovery assignment (signed RSSI
encoded as uint32 / passive hint), and 12 disconnected provisional hint retired
(release flag / reserved). These observations grant no authority and do not extend
an exchange or restart a precision train.

Kinds22–25 carry the same connected/create-error result and per-attempt baseline.
The target in22 is meaningful only if23 reports a prepared scan window; otherwise
it is historical. These counters neither authenticate a discovery hint nor
establish RF timing. Saturation and any event loss must remain in analysis.

State flag bits, starting at zero: active, RC profile, established PA sync,
creating PA sync, map valid, locked, authorized, serving GM, follower, closing,
profile change pending, phase acquisition, HOT ready, source eligible, standby
authorized, takeover pending, GM capable. A locked state is a software
indication. GM health uses source eligibility, publication and capture progress;
the RC lock bit is not a GM health criterion.

RC transport phases: 0 idle, 1 connecting/waiting for callback, 2 MTU, 3 capability
or GM marker, 4 write, 5 reply, 6 GM request, 7 GM proof, 8 close. An ATT callback
error packs its callback kind in the upper 16 bits of `detail`; the lower bits
retain the phase. Close causes: 1 completed/aborted exchange, 2 quiet window,
3 acquisition yield, 4 demand rejection, 5 shutdown drain, 6 original attempt
deadline. A local close request does not prove it caused a later disconnect.
GM admission result codes: 1 codec, 2 exchange, 3 non-root, 4 coarse-time gate,
5 accepted. These diagnostic codes grant no authority.

Main also emits `#R2_TIMING {JSON}` notes when DTR is asserted and the console
queue is idle, at most ten notes per second. Backpressure retains the streaming
cursor until a note is queued; USB reset can still drop a queued note. Use the
framed cursor query to recover retained events. A transmitted note is not proof
of host receipt. Busy sync-create errors are rate limited to one per second.
Continuous collection is necessary: the ring is a recent-history buffer, not a
long-term recorder.

## Console capture

The standalone macOS collector needs Python 3, `ioreg` and `lsof`; it has no
private-checkout dependency. Create an ignored, owner-only configuration with
your actual USB serial, silicon, exact installed source and commit:

```json
{"devices":{"PToB0":{"usb_serial":"YOUR_USB_SERIAL","silicon_id":"0123456789abcdef","source":"REPLACE_WITH_64_LOWERCASE_HEX_DIGITS","commit":"REPLACE_WITH_40_LOWERCASE_HEX_DIGITS"}}}
```

```sh
python3 firmware/t096/tools/console_capture.py \
  --config artifacts/local/console-devices.json --unit PToB0 \
  --duration 3600 --out artifacts/console-run-01
```

Repeat `--unit` to capture a known cohort concurrently. This issues only status,
network, precision, parent-status, clock, join and event queries. It performs a
fresh USB identity/ownership scan on entry and every five seconds while draining
serial on its owning thread. Between scans it retains the same exclusive FD and
verifies every reply's source and boot. It takes the unit lock, refuses
an existing client and ends its segment on disconnect, identity/boot mismatch or
three unanswered queries. It never stops another process. Original maintenance
and update guards remain necessary for flashing.

Raw bytes and timestamped JSONL rotate at 16 MiB by default; `--rotate-bytes`
changes that size. The collector retains host UTC and monotonic receive times,
CRC failures, timeouts, partial tails, event gaps and final closure status.
Capture artifacts stay under ignored `artifacts/`. Protect raw logs and real
identity configurations; do not add them to the repository. Earlier firmware is
sampled through its existing snapshots; it cannot supply the new event history.
The deployed command protocol requires increasing request IDs across console
owners; the collector uses the established millisecond seed and stops on
exhaustion. A future host-session handshake should remove this limitation.

Host receive times correlate observations; they do not measure RF timing,
independent output accuracy or UTC error. Query load can perturb scheduling.
Retain polling rate, missing intervals and diagnostic drops in any run analysis.

For an explicitly requested background run, `tools/console_background.py` uses
the same arguments and defaults to seven days, with hourly segments. It starts
no timing daemon and never resets hardware. A disconnect ends the current
segment; after proved descriptor closure and a 15-second backoff it starts a new
segment with a fresh USB identity/ownership scan. Gaps, errors and boot changes
remain in the segment journal. `run.json` records the owned PID, and SIGTERM
requests a graceful stop. Unknown closure stops the supervisor. A detached run
does not survive a host reboot; keep the host awake for continuous collection.
If a detached USB device rejects the exclusivity-release ioctl, the logger
retains that error and marks the segment incomplete. Successful descriptor
closure still permits a fresh guarded segment; a failed close remains unknown
and stops the supervisor. No ownership or identity check is waived.

## Recovery changes and acceptance

Version0.15.65 also reads the observer's comparison time after copying the main
heartbeat, then again after a stable mailbox copy. Main has higher priority and
can publish while the observer is preempted; comparing that new timestamp with
the observer's earlier wake time could wrap unsigned elapsed time and falsely
report a core stall or stale health. Production-body preemption tests retain the
exact2s core,1s report and3s owner bounds, including uptime wrap. This second race
is reproduced offline; no particular hardware reset is attributed to it.

Version0.15.64 compares watchdog service timestamps using the same native-tick
microsecond clock as their producer. Previously, flooring watchdog time to a
whole millisecond could put a newly published snapshot in its apparent future,
interrupting the required eight seconds of sustained RC health and eventually
requesting recovery even while accepted corrections advanced. Strict future,
one-second snapshot freshness, original service expiry, source, PLL and
three-second frozen-owner checks remain unchanged. Production-body regressions
cover fractional-millisecond publication, a real future timestamp, exact expiry,
stale/frozen owners and useful observations across a complete five-minute RC
inactivity interval. These offline results do not establish physical durability
or explain every shorter discipline expiry.

The existing sparse `R2_WATCHDOG` report adds `observations` (saturating total,
useful, future-snapshot and stale-snapshot counts) and current `refusal_flags`.
Bits are1 inactive/closing/pending/error,2 future snapshot,4 snapshot at least1s
old,8 no follower clock,16 expired service deadline,32 invalid map,64 unauthorized,
128 absent/stale correction age,256 RC PLL unlocked and512 held/expired RC source.
Follower-specific flags are omitted when fresh associated GM TX already supplies
useful activity. These are best-effort diagnostic snapshots, not output authority.
Event30 (`RC_DELIVERY`) copies bounded records only at actual holdover expiry
(`result=1`) and recovery (`result=2`), following the corresponding event19/20.
Version0.15.66 also records inherited-source expiry (`result=3`) and recovery
(`result=4`) while the original correction/context hold remains live; these
are separate from a500ms correction gap. No healthy-period packet log is added.
`detail` selects:1 `peer=last selected-parent physical RX tick,a=authenticated
selected-parent RX count,b=wire/capture decode rejects`;2 `peer=last accepted
pair completed_at tick,a=accepted,b=pair rejects`;3 `peer=context_until tick,
a=evicted originals,b=pending originals`;4 `peer=current observation capture tick,
a=global capture invalid,b=global capture queue overflow`;5 `peer=source expires
in source nanoseconds,a=map generation,b=local capture epoch`.
Version0.15.66 adds6 `peer=RX tuple at prior successful discipline,a=RX count
at that discipline,b=current RX count`;7 `peer=latest decoded RX tuple,
a=its correction count`;8/9 `peer=latest packet correction0/1 tuple` (zero
when absent);10/11 `peer=latest packet origin issued/expires source nanoseconds`.
Tuples pack publication in bits0–31, event in32–47 and channel in48–55.
These are decoded/authenticated packet fields, not proof of independently
received followups or GM RF transmission. The generic `peer` field carries
the stated numeric value for this diagnostic kind only.
RX counts are decoded records, not RF packet counts; wire/capture rejects and
capture statistics can include local TX. Global capture counters survive logical
owner changes; receiver counters reset with that owner. Use boot/source/scope,
journal sequence and lifecycle records to delimit comparisons. Zero or noncausal
ticks remain unknown; do not infer an elapsed interval by unsigned subtraction.
On the accepting path the receiver has already replaced `reference.completed_at`
before `holdover_poll` runs: detail2 can therefore describe the newly accepted
pair while the holdover event's `a` still measures the gap from the prior
successful discipline. Do not reinterpret that detail2 as the previous pair.
The ring's existing overwritten/exhausted/missed fields retain any missing event
population. These stage snapshots cannot alone establish missing GM RF TX.

Version0.15.66 publishes a newly disciplined USB map immediately, before
read-only witness/audit work, GATT/control polling and owner sleep. The offer
uses the same minimum of original map-effective+500ms, discipline+500ms and
context deadline. USB still enforces the original source expiry and chooses
its actual publication lower bound; it never authorizes an earlier SOF or
refreshes an existing generation. This removes avoidable control-work delay
between valid discipline and availability. It does not manufacture a missing
original/followup association or authorize an expired inherited source.
`tools/rc_publication_tests.py` exercises the production commit/publication
bodies against the real USB map bank, with a following control delay and strict
expiry, capture-failure and no-backdating checks. The optional negative fixture
omits immediate publication to reproduce the former loop-end-only behavior.

Version0.15.67 adds loss-only event30 details12–18. The existing32-record ring
holds one18-record loss snapshot, one11-record recovery and both holdover
records (31total); larger bursts can overwrite and retain explicit loss counts.
For12–15, `peer` packs current counter in upper32bits and its prior-successful-
discipline baseline in lower32bits. Detail12 pairs RX arms with `a/b=RX queue
copies current/baseline`;13 pairs RX abort callbacks with `a/b=no-DONE`;
14 pairs bad CRC with `a/b=PDU-filter refusal`;15 pairs RX queue overflow with
`a/b=no-ADDRESS`. Detail16 `peer=last RX abort tuple,a=total capture aborts,
b=their baseline`;17 `peer=last RX arm tuple,a=total completed queue copies,
b=their baseline`;18 `peer=last RX completed tuple,a=last arm owner token,
b=last abort owner token`. Native tuples pack event0–15, channel16–23,
hook-site24–31 and capture epoch32–63; these differ from packet tuples6–9.
Hook sites are0 unknown,1 active-cancel callback,2 prepare-overhead cancellation,
3 TX chaining and4 periodic RX `isr_done`. The site identifies code location;
`isr_done` alone does not prove preemption. Owner tokens are boot-local native
context addresses truncated to32bits, never peer identities or authority.
RX outcomes are passive selected-controller observations. A successful queue
copy precedes payload decoding; no-DONE/no-ADDRESS cannot distinguish absent
sender TX from an inaudible or missed reception. CRC/PDU refusals are separate;
TX totals remain global. Arm/configuration/PPI failures retain existing global
invalid counts and ownership guards; RX categories are not asserted exhaustive.
Counters saturate and survive logical clear, while prior-discipline baselines
reset with the logical owner. Zero, saturated or cross-scope values are unknown.
No new timer capture/task, radio scheduling, validity gate or routine packet
output is introduced.

Counter/flag changes alone do not increase the existing one-minute console cadence;
retry/enable/reset transitions keep their existing prompt reports.


Version0.15.63 reserves eight controller RX buffers instead of the default one.
In the pinned controller the shared LLL pool is3+1+configured-buffers: five
nodes previously, twelve now. Legacy initiation with channel-selection2 needs
four free nodes before it prepares CONNECT_IND. Live failed attempts received
eligible GM advertisements but refused every match with ENOBUFS; window
refusals and prepared-request counts were zero. This local resource failure
is independently visible as event24's buffer-refusal count. Enlarging the
reserve retains the stock allocator checks and every controller assertion.
The system workqueue stack is5120bytes (previously8192; observed unused stack
exceeded7KiB). Other stacks, packet sizes, MTU, timing reservations and source
gates are unchanged. Inspection requires at least1KiB static RAM margin;
runtime stack high-water and future buffer refusals remain acceptance evidence.

Version0.15.62 applies each failed-control wait once. Cold recovery waits2–3s
after its first failure,5–7.5s after its second, and one10–15s wait after the
third before starting a fresh batch. Healthy-clock renewal can use30–45s for
that batch wait, capped to reserve10s before its original context expiry;
within that reserve it uses2–3s. This avoids compounded per-attempt/batch delays
and never extends context, source permission or a transaction deadline.

Version0.15.61 samples the physical capture counter again inside the advertising
gate. Processing a received precision capture can install a map newer than the
owner's loop-start timestamp; comparing that fresh map with the stale loop
sample must not look like clock reversal or stop an otherwise healthy train.
The fresh timestamp still enforces discipline age and source expiry.

Event28 and `rc-parent.transport[9]` report bit0 server role, bit1 acquisition
pause, bit2 capability advertising, bit3 usable precision gate, bit4 periodic
train/SyncInfo advertiser running and bit5 UCS1 advertising. These are owner/API
states, not independent RF observations. Snapshots may briefly capture a gate
transition before the same owner iteration finishes stopping its advertisers;
use the subsequent transition and preserve any failed stop.

Version0.15.60 requests100ms for the RC's connectable capability advertiser
(SID13, previously30ms) and nonconnectable SyncInfo discovery advertiser
(SID12, previously20ms). These are extended discovery events, separate from
SID12's periodic precision train, which remains62.5ms (16Hz), with unchanged
slot placement, authentication and source/map expiry. These are requested
intervals; controller delay, scheduling and skipped events affect actual RF.
The nominal discovery event rates fall from33.3/s and50/s to10/s each.
The separate UCS1 service discovery remains318.75ms.

All RC advertising is held off until an upstream precision capture is usable:
parent PA sync, valid fit and clock map, PLL tracking, unexpired source and
less than500ms since discipline are required. Capability and UCS1 discovery
withdraw when this gate fails; the periodic train and its SyncInfo discovery
stop together. Upstream scanning/admission remain enabled. Discovery does not
require an existing child demand or an already-running downstream train, so an
idle synchronized relay can still be discovered. Existing UCS1 clients retain
their sessions and explicit validity/withdrawal reporting.

The current RC7 codec defines the144-byte authenticated capability projection,
300-second normal renewal and360-second maximum lease, but no encoded discovery
interval. The historical adoption specification retained the then-existing
advertiser cadence;100ms is an explicit current owner-policy change, not a
historical protocol requirement or a new wire version. Discovery grants no
timing authority. Validate cold joins, child admission, relay path acquisition
and renewal under load after changing this cadence.

Healthy T096 RCs have no per-second context renewal. Version60 introduced
2–3s,5–7.5s and10–15s waits plus30–45s between three-attempt batches; version62
replaces that compounded schedule as described above. Authenticated
PENDING waits5–7.5s, without rearming its original parent trial. Each schedule
is journaled separately from the five-minute steady renewal. The original
two-second transaction still bounds acquisition. Normal connected teardown
allows its negotiated supervision timeout plus500ms for callback delivery;
create cancellation and explicit profile/update shutdown retain their500ms
budgets. Thus a recoverable peer loss on a one-second-supervision ACL is not
declared fatal after only500ms. Missing callbacks still fail with references
owned; no wait is rearmed. Client-specific replay-anchor and
permission-gap refresh policies must be diagnosed separately; increasing a
lease alone cannot authorize stale output.

Cold RC admission requests 7.5ms ACL events, keeping same-scope renewal at 50ms.
This gives the dependent JOIN exchanges more opportunities to meet the unchanged
100ms initial coarse-uncertainty gate. Each attempt randomizes a finite scan
interval (50–100ms) and window (10–20ms), preserving the split controller's
non-continuous scan path and original two-second attempt deadline. This reduces
repeated phase collisions; it does not guarantee reception.

An authenticated direct JOIN from a non-root ends that locator's acquisition
trial and defers it for 60 seconds. G1 discovery alone still grants no role or
time authority. Later promotion can be retried after that bounded cooldown.
Source expiry, authentication, replay fences, corrected-capture association,
500ms held-map limit, PA supervision and proven controller drains remain in force.

Durable-chain acceptance requires extended GM and RC capture together, including
late admission while other clients remain locked; source loss and return; root
promotion and reboot; relay path changes; hours of renewals; event-counter and
host-request limits; console saturation; and unsuccessful recovery attempts.
Separate availability, corrected delivery, fit quality and independent output
metrology. Report failure/recovery durations and denominators, not just final
lock state. Keep a working GM's periodic train running during individual client
recovery; a global GM restart is a last-stage recovery, not the normal cure.

Offline regressions:

T096 0.15.70 adds journal kind **31** for RC RF placement. Result 1 is an
out-of-slot restart, 2 is a successful bounded phase adjustment, 3 is a start
request, 4 is quality withdrawal, and 5 is downstream demand expiry. Negative
results are adjustment API failures. `peer` carries the physical parent anchor
tick and `detail` the local train. For results 1/2/negative, `a` is signed phase
error in ns (decode as int32) and `b` is clearance in ns. Adjustment summaries
are rate limited to ten seconds or a result change. A request is not RF proof;
use actual TX captures and the resulting train/phase progress.

The fractional placement servo moves by at most 8 us per 62.5 ms frame while
the captured phase remains legal and upstream precision is usable. It preserves
the advertised interval, event counter and train. The 250 us deadband, stale
capture guard and original out-of-slot restart remain. This is experimental
placement control, with hardware durability and interoperability still to be
verified. The [Bluetooth Link Layer timing rules](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/low-energy-controller/link-layer-specification.html)
distinguish average clock drift from instantaneous jitter; RF phase is separate
from the numerical time map and its unknown accuracy bounds.

```sh
python3 -m unittest discover -s firmware/t096/tests -p 'test_console_capture.py' -v
CC=clang python3 firmware/t096/tools/rc_selection_tests.py --out artifacts/selection-check-01
CC=clang python3 firmware/t096/tools/health_tests.py --out artifacts/health-check-01
python3 firmware/t096/tools/test.py --deps artifacts/deps
```

Zephyr's [periodic-sync API](https://docs.zephyrproject.org/latest/doxygen/html/group__bt__gap.html)
does not impose an acquisition timeout. The application's bounded acquisition
and [supervision timeout](https://docs.zephyrproject.org/latest/doxygen/html/structbt__le__per__adv__sync__param.html)
are separate controls. A passing host test or build does not qualify long-term
hardware reliability.

Stability is the prerequisite for timing-performance metrology. Before treating
a long run as a metrology candidate, account for every reboot, unintended root
change, HOT withdrawal, admission/renewal failure, held-map expiry, sync outage,
recovery interval and logger gap. Distinguish planned installation interruptions
from spontaneous faults. Retain counts and observation time, outage duration
distributions, recovery outcomes, and periods without a usable HOT alternative.
An unfinished or interrupted soak is not a clean stability pass; an apparently
stable software map does not establish independently measured output precision.
