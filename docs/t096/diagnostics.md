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
counters in versions0.15.58–0.15.100 observe legacy initiating advertisements,
as used by the T096 GM. Connections established through AUX_ADV_IND use a
separate native path, so zero legacy request/refusal counts in those versions
cannot diagnose an extended W12 connection attempt, even when it succeeds.

Version0.15.101 observes the exact pinned AUX initiating branch as well. Eligible
target matches, prepared requests, four-node RX reserve refusals, insufficient
windows and active initiating preemptions feed the existing counters. Five new
counters distinguish unavailable AUX context and response outcomes: accepted by
the native checker, radio not DONE, bad CRC, and invalid response. The raw DONE
and CRC observations precede the unchanged native checker. An accepted response
does not prove authentication or usable time; a prepared request does not prove
on-air transmission. Hooks neither change a controller decision nor format USB
output inside a callback.

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

From0.15.101 the journal holds the newest29 events (1,160 bytes of records),
previously32 events (1,280 bytes). Three40-byte records fund40 bytes of extra
counter/baseline storage and an80-byte sticky last-attempt summary. This trades
9.375% of recent journal depth for evidence retained after journal overwrite;
the controller/precision buffers, stacks and1,024-byte native RAM inspection
minimum are unchanged. Actual native inspection must confirm the resulting
margin. The journal survives
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
| 29 | Native RX reserve at a legacy or AUX initiating refusal | required nodes / last free nodes / lowest free nodes seen at a refusal in this MCU boot |
| 32 | Native GM control progress; signed result retains API/protocol error | stage / stage value / stage value |
| 33 | Per-attempt AUX initiation reasons | pair index / first reason count / second reason count |

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
(release flag / reserved). Stages13–16 follow native central connection
callbacks and contain cumulative controller counters since the MCU boot:
13 prepared scan windows / CRC-valid PDUs, 14 eligible target advertisements /
prepared connection requests, 15 insufficient-window / RX-buffer refusals,
16 initiating preemptions / native1M scan flags (passive bit0, active bit1,
initiating bit2, periodic-acquisition bit3). These are cumulative, unlike the
per-attempt kinds22–25; saturation and source/boot changes fence subtraction.
These observations grant no authority and do not extend
an exchange or restart a precision train.

Version0.15.105 adds kind32 stage17 for a native GM scan-stop refusal
(fixed retry budget ms / reserved), and stage18 for a duplicate disconnect
response (duplicate-owner slot / remaining original drain budget us).
Neither response proves resource release. Scan-stop failure uses transport
fault operation6. A terminal transport snapshot is saved even when no console
query requested one before the failure; its timestamp remains the original
owner observation.

Kind33 pairs are: detail0 unavailable AUX context / native accepted response;
detail1 radio not DONE / bad CRC; detail2 invalid response / reserved zero.
No-DONE takes precedence over CRC, which takes precedence over response
validation, so each observed response ISR contributes exactly one outcome.

Kinds22–25 carry the same connected/create-error result and per-attempt baseline.
The target in22 is meaningful only if23 reports a prepared scan window; otherwise
it is historical. These counters neither authenticate a discovery hint nor
establish RF timing. Saturation and any event loss must remain in analysis.

The `rc-parent` reply adds `initiation` schema2 from0.15.101. It retains the last
completed attempt independently of the journal and includes a saturating serial,
valid/saturated flags, peer, result, prepared address/type/channel, twelve
per-attempt counts and `rx_reserve` (last free / boot low-water / required nodes).
The count order is prepared primary scan windows, CRC-valid primary PDUs,
eligible target advertisements, prepared requests, window refusals, buffer
refusals, preemptions, unavailable AUX context, accepted AUX response, no-DONE,
bad CRC, invalid response. Address/type/channel are historical unless that
attempt reports a prepared primary scan window. Saturation makes the numerical
delta incomplete; source/MCU-boot changes require a new segment. Serial changes
identify new results, not a timing guarantee; the snapshot contains no
independent RF timestamp. Later ISR activity does not alter this frozen result.
The append uses the existing reply buffer with checked bounds; full-CRC framing,
maximal-width fields,29-record overrun/modulo and sequence exhaustion have host
regressions. `initiator_aux_tests.py --legacy-hooks` and
`rc_relay_renewal_tests.py --legacy-recovery` are expected-failure controls for
the formerly unobserved AUX branch and retry-resetting policy respectively.

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

Before0.15.97 all RC advertising was held off until an upstream precision capture was usable:
parent PA sync, valid fit and clock map, PLL tracking, unexpired source and
less than500ms since discipline are required. In those revisions capability and UCS1 discovery
withdraw when this gate fails; the periodic train and its SyncInfo discovery
stop together. Upstream scanning/admission remain enabled. Discovery does not
require an existing child demand or an already-running downstream train, so an
idle synchronized relay can still be discovered. Existing UCS1 clients retain
their sessions and explicit validity/withdrawal reporting.

The current RC7 codec defines the 144-byte authenticated capability projection
and a 360-second maximum lease, but no encoded discovery interval. From0.15.100,
T096 relay authentication renews after240 seconds; earlier T096 relay versions
used300 seconds. From 0.15.95, T096 direct-GM authentication renews after 240 seconds
to leave time for failed exchanges under the rolling two-starts-per-minute
budget and unchanged 20–40-second warm backoff. Starting at 300 seconds could consume
the entire remaining minute after two failures. Earlier renewal does not extend
the original context, the 500ms held-map limit, or source/proof validity; it cannot
repair an unavailable GM source. Production renewal tests exercise failed two-second
transactions, cancellation, minimum/maximum backoff and the actual start limiter
against the unchanged context deadline. `rc_renewal_tests.py --legacy-gm-renewal`
is an expected-failure control restoring the old schedule.
From0.15.101 a repeated same-parent correction-loss poll preserves an already
pending attempt, batch retry or prepared start jitter. It no longer resets the
20–40-second warm retry via the10-second recovery poll. Authenticated changed
scope can still request an immediate challenge, with the same original
transaction, authentication and rolling-start limits. The combined relay
regression executes the production recovery/retry functions with recorded
failure durations and pending-jitter/batch cases; it is not an RF simulation.
From0.15.102 an established relay can defer a new recovery start while a fresh
canonical count-zero original has passed the production admitted-path and
replay matcher. This is a scheduling hint, with a 500 ms age limit, not time
permission. Exact duplicates retain only the original observation time. The
original authentication context must remain live and younger than 240 seconds;
normal renewal and expiry, missing PA, startup and changed authenticated paths
bypass the hint. A pending path change cannot be masked by old-path packets.
In-flight exchanges and the rolling start budget are unchanged. Tests replay
production owner/codec/matcher functions with explicit timing assumptions;
their improvement is not a measured field latency claim.
The historical adoption specification retained the then-existing
advertiser cadence;100ms is an explicit current owner-policy change, not a
historical protocol requirement or a new wire version. Discovery grants no
timing authority. Validate cold joins, child admission, relay path acquisition
and renewal under load after changing this cadence.

Healthy T096 RCs have no per-second context renewal. Version60 introduced
2–3s,5–7.5s and10–15s waits plus30–45s between three-attempt batches; version62
replaces that compounded schedule as described above. Authenticated
PENDING waits5–7.5s, without rearming its original parent trial. Each schedule
is journaled separately from the steady renewal. The original
two-second transaction still bounds acquisition. Normal connected teardown
allows its negotiated supervision timeout plus500ms for callback delivery;
create cancellation and explicit profile/update shutdown retain their500ms
budgets. Thus a recoverable peer loss on a one-second-supervision ACL is not
declared fatal after only500ms. Missing callbacks still fail with references
owned; no wait is rearmed. Client-specific replay-anchor and
permission-gap refresh policies must be diagnosed separately; increasing a
lease alone cannot authorize stale output.

Cold RC admission requests 7.5ms ACL events. From 0.15.86, direct-GM renewals
also request 7.5ms; relay renewals retain 50ms. A failed GM renewal can otherwise
exceed the original 250ms reply-age gate across dependent ATT exchanges despite
live periodic timing, leaving too few successful retries before binding expiry.
This gives the dependent JOIN exchanges more opportunities to meet the unchanged
100 ms initial coarse-uncertainty gate through 0.15.106. From 0.15.107 RC uses
the coarse RC7 envelope described below. Each attempt randomizes a finite scan
interval (50–100ms) and window (10–20ms), preserving the split controller's
non-continuous scan path and original two-second attempt deadline. This reduces
repeated phase collisions; it does not guarantee reception.

From **0.15.107**, initial direct-GM RC admission and renewal use the same RC7
coarse replay envelope (at most 1.2 s wide). The normal GM/control adapter keeps
its strict 100 ms half-width. The RC adapter retains all record validation and
authenticated exchange/boot/root scope checks. Its original <250 ms causal RTT
and ≤50 ms independent source bound cap the admitted half-width at 175,564 µs;
permission still subtracts the full RTT, source bound and drift margin. The
observed 145,246 µs round/40,000 µs source bound yields 112,978 µs half-width and
fits this envelope. This changes coarse admission, never a precision fit,
output accuracy claim or original source/map expiry.

From **0.15.109**, RC passive search reserves 10 ms per 20 ms interval; the
PA-synced/unusable-parent watch remains 5 ms/20 ms. The pinned controller retains
nonzero `ticks_window` for these finite windows and cancels them on preemption.
A continuous scan instead requests a retained resume prepare. A saved kernel
fault after RC-to-GM switching mapped to `lll_prepare_resolve`'s failed prepare
FIFO allocation; queued-context contents were unavailable. The new finite path
must be distinguished from a proved full fault fix and from a transition canary.
Host-owned search exposure remains owner time, not RF listening time; the
original 60 s wall ceiling is unchanged. Preserve any unexpected boot change and
its bound reset/fault capsule rather than treating a recovered current state
as uninterrupted service.

From **0.15.108**, native GM G1 connectable advertising remains stopped while
its single incoming provisional JOIN slot owns a connection, including the
interval between a close request and its real disconnect callback. This lets
another already-initiating receiver await the next available advertisement
within its original attempt instead of being accepted and immediately rejected
as a duplicate. Outgoing bootstrap, existing control links, passive discovery
and the independent periodic train retain their gates. This prevents the
observed admission-slot collision; it cannot guarantee RF reception. A marker
read now retains its original ATT error; only an otherwise successful malformed
marker is labeled generic error 1.

The RC MTU callback records kind 32 stage 21 for a callback error (actual
negotiated MTU / live-state flag). Unlikely Error may advance only if the host
callback observes a connected link and MTU ≥215. Other errors, insufficient MTU
and an unknown/disconnected link remain refused. ATT inspection stays inside
the host callback; the owner does not walk ATT channels. This adds no MTU retries
or transaction lifetime. A refused duplicate or vanished ACL still needs proven
teardown before any later ordinary admission attempt.

An authenticated direct JOIN from a non-root ends that locator's acquisition
trial and defers it for 60 seconds. G1 discovery alone still grants no role or
time authority. Later promotion can be retried after that bounded cooldown.
Source expiry, authentication, replay fences, corrected-capture association,
500ms held-map limit, PA supervision and proven controller drains remain in force.

From 0.15.87 an initial relay capability read may retry ATT unlikely-error or
insufficient-resources up to three times, 50ms apart, on the same connected ACL
and inside its original two-second transaction. This tolerates a capability read
arriving before the server owner is ready. It grants no source authority and
does not retry MTU, authentication, response/write or disconnected-owner errors.

Version 0.15.90 first discovers the relay characteristic declaration, then reads
the 212-byte capability by its value handle. The pinned W12 NimBLE SDK's ATT
Read By Type path uses a 19-byte flat buffer and returns ATT unlikely-error for
that larger value, even with a sufficient MTU. Retrying the same read-by-UUID
operation cannot repair this restriction. Discovery copies metadata only; the
complete authenticated capability, fresh challenge, source/path checks and
original two-second transaction remain required. Handles are cleared on every
new client ACL. Ordinary response reads also use the discovered value handle.

Version 0.15.91 withdraws RC7 mesh admission advertising when the current route
is receive-only, including a client of an RC2 parent. A peripheral connection
racing that withdrawal is drained through the existing bounded close path.
Receiving the RC2 stream, USB source streaming and existing UCS1 client ownership
remain separate. The parent/child slot rules, maximum depth and all timing gates
are unchanged. Independent paths may reuse a slot; occupancy observations remain
advisory and do not prove an RF collision.

### RC parent selection and relay dwell

Version **0.15.110** clears a previously working parent once usable time has
been absent for ten continuous seconds and PA is no longer synchronized. This
bypasses remaining acquisition protection. Before first usable lock, cold
acquisition keeps its existing allowance; an audible PA with an unavailable
source keeps its degraded-recovery allowance. Exhausting either allowance also
retires the selection without waiting for another candidate. Retirement drains actual ACL
and PA owners, clears old timing authority and begins a fresh bounded search.
Failure history and rolling admission starts remain intact, and a returning
identity needs fresh authentication. Explicit parent and relay-only settings
remain configured. Retry journal kind 27 reason 5 records failed-parent retirement;
the arguments retain admission stage and elapsed selection age. Host tests do
not establish field failover latency.

From 0.15.103, an explicit `rc-parent ID` selects one mesh identity through either its
ordinary direct-GM locator or authenticated RC7 relay capability. The receiver
stays in RC mode; it does not promote itself or its parent to GM. Actual GM
JOIN identity/proof, source, coarse-time, expiry and clock-fit gates still apply.
An absent, non-root, wrong-group or unavailable source cannot supply time merely
because its identity was requested. Enrollment that excludes direct GM retains
that exclusion. `rc-parent auto` restores ordinary automatic selection and
healthy-parent retention. A parent command performs the existing bounded RC
owner transition; it is not a hardware reset. Use a separately reviewed finite
measurement window and restore the desired parent afterward.

Version **0.15.99** allows 30 minutes without usable RC timing before requesting
watchdog recovery. Each live, unexpired usable timing observation refreshes the
loss deadline. Thirty seconds of sustained recovery clears retry escalation;
a brief map cannot erase retries. Hardware, CPU, RC-owner and recovery-observer
watchdog failure allowances are at least 30 seconds. These allowances do not
extend source/map permission, precision lock or controller cleanup deadlines.
Normal bounded relay acquisition/recovery and selection act first.

RC1 and RC2 are eligible under the same relay ranking. There is no depth bonus.
An eligible RC2 can replace a degraded RC1 after its recovery interval and scan;
the authenticated RC2 path makes the receiver a leaf. Paths containing the
receiver itself remain refused. A nearby relay's advertised lock is a hint,
while fresh authentication and actual usable-time observations establish its
quality. The production selector/watchdog and path-role tests cover fallback
and both legal RC2 slots. Hardware field durability remains to be observed.

Version **0.15.98** adds a separate 500 ms presentation delay before an already
displayed RC analog face changes to Syncing. A short loss advances the cached
display time while the live timing/lock indicators follow their actual gates.
The cache does not arm during startup or grant USB/relay/precision permission.
Continuous loss has one fixed deadline; changing profiles, withdrawing transport
or observing a stale/backward model cancels it. Diagnostic `display_time` marks
the cached face as coarse, while `ui` analog eligibility and `ui_input` map/role
fields continue reporting current validity.

Version **0.15.106** gives a newly detected authenticated serving-GM report one
direct authentication opportunity before ordinary relay acquisition protection,
quality history or non-root cooldown. The report must match a connectable G1
locator and BLE address observed within two seconds; its own queue residence
must fit the original validity (at most two seconds). Boot/source epoch and
monotonic sequence identify the event. The normal JOIN, source, map and expiry
gates still determine permission. Old connection, PA sync and downstream
publication must drain before the new parent is selected. Explicit parent and
relay-only overrides suppress this automatic preference.

Normal control-start history is preserved. One additional start per receiver per
60 seconds can bypass exhausted ordinary admission/backoff for a new GM event;
that start is also recorded in normal history. Repeated announcements from an
already attempted scope do not rearm the reserve. Age uses host monotonic time,
independently of the precision capture timer. Scanning resumes when the old
parent remains PA-synced but usable time has expired. No periodic healthy-parent
survey is added. Journal kind32 stage19 records the new serving-GM hint; stage20
records the direct attempt (result1 means the additional start reserve was used).
These scheduling changes do not establish timing accuracy or failover durability.

The current **0.15.97 field candidate** changes selection and management
discovery as follows. These defaults need field validation.

| Policy | Field candidate |
| --- | --- |
| Automatic collection | 10 seconds host-owned scan exposure; 60 seconds elapsed ceiling |
| Locator horizon | 65 seconds; fresh admission always required |
| Relay admission | 180 seconds from selection, including PENDING |
| Relay acquisition | 300 seconds from first admission, restarted once at first PA establishment |
| Relay absolute ceiling | 600 seconds from selection |
| Settled relay degraded recovery | 300 seconds; individual lock flickers do not clear an unstable window |
| Exhausted relay probation | 30, 60, then 120 seconds |
| Quality memory | 8 rows, 304 bytes including sampler; five-minute confidence half-life, 30-minute expiry |

Scan exposure excludes known scan stops, PA creation, ACL ownership and local
pauses over 250ms. It is host-owner accounting, not verified controller RF
airtime. A search reaching its wall ceiling may choose from incomplete coverage.
An idle relay can be selected at cold startup. Quality confidence scales with
observed samples and age; usable-time/delivery evidence dominates small RSSI
differences. Locator reception and failed attempts do not refresh quality age.
An authenticated path change discards proven usable-time status and discounts
old delivery confidence; a group/enrollment change clears advisory history.

RC7 management capability advertising can remain available during precision
loss and replies PENDING until the normal source/map/clock gates allow READY.
Periodic timing and UCS1 discovery retain their original validity gates, and an
RC2 receiver still cannot advertise mesh admission. Discovery grants no timing
authority. A bad packet is rejected independently of retaining the selected peer.
Healthy selected relays remain selected. Renewals and repeated PA establishments
cannot restart an acquisition or degraded episode. Direct-GM policy is unchanged.
Journal parent-change kind4 now includes collected host-owned scan milliseconds
in its final argument. Retry reason4 records an exhausted parent episode, its
admission stage and elapsed selection age; it is separate from individual errors.

The following records describe preceding policy revisions and their rationale.

T096 0.15.85 retains each automatically chosen parent through a bounded admission
and precision trial. The defaults are deployment policy, not measured convergence
guarantees. Version 0.15.88 extends settled-relay recovery to 120 seconds.
Version 0.15.92 gives relay admission 180 seconds before the separate 120-second
precision trial, with a five-minute absolute ceiling. A publisher can first
return PENDING while demand starts its train; retained control start limits and
failed attempts can otherwise exhaust an earlier one-minute admission trial.
This changes parent retention, not the individual two-second transactions or
the source, map, proof and 500 ms correction-hold gates.

| Interval | Direct GM | Relay |
| --- | --- | --- |
| Admission, measured from selection | 60 seconds | 180 seconds |
| Precision acquisition, from first authenticated admission | 120 seconds | 120 seconds |
| Absolute acquisition ceiling, from selection | 180 seconds | 300 seconds |
| Degraded-time recovery after sustained lock | 60 seconds | 120 seconds |

PENDING replies consume the original admission interval. Same-parent renewals,
repeated admissions and individual lock flickers cannot restart these deadlines.
Sustained lock requires a full eight-second observed window with at least 90%
usable time. Once settled, a parent becomes unstable below 75%; recovery requires
90%. A relay gets the recovery interval even after an eight-second window turns
unstable. Individual lock reports cannot clear that unstable episode. Local
listening pauses remain unknown observations.

The former 30-second settled-relay recovery interval could expire after only one
admission opportunity under the independent two-starts-per-minute control budget.
The 120-second allowance permits more attempts to renew or bind a changed train
without cycling to cold relays. It begins with the degraded episode; repeated
renewals and lock flickers cannot restart it. The control budget, original
transaction deadlines and immediate withdrawal of expired timing remain intact.

From 0.15.90 every automatic search collects locators for three seconds, including
recovery after a rejected or failed parent. Selection/refusal clears that search's
collection timer; advertisements during a protected parent trial cannot start
the next search early. An empty scan also ends its collection. A four-second
advisory locator horizon retains observations from the whole collection, rather
than only the final 200 ms. It grants no timing or source authority; subsequent
live admission retains its original validity gates. Explicit diagnostic overrides
keep their 40 ms collection and 200 ms locator horizon.

Version 0.15.93 extends GM admission, precision acquisition and settled recovery
to the intervals above. Normal HOT/follower participants can also emit G1 discovery.
An authenticated non-root refusal disqualifies that locator; the next collected
search tries a remaining fresh direct locator before a relay. It grants no
authority to the refused participant and does not change HOT/election behavior.
An actual GM that remains unusable still yields after its bounded trial.

Cold startup prefers a fresh GM.
During recovery, a fresh eligible GM with sustained usable-time history and no
recorded failure keeps first preference. Otherwise an eligible relay takes
priority over an unproved or failed GM; hearing GM packets and coarse admission
alone do not establish usable time. Among eligible relays, smoothed RSSI ranks
first; history and publishing status break equal-signal ties, followed by stable
identity. Deferred, expired or rejected candidates remain ineligible. This avoids
choosing a weaker relay merely because its advertisement arrived first or its
publishing/history bonus outweighed signal strength. The candidate table remains
bounded to eight entries; discovery is not proof that every physical neighbor
was heard. GM ranking retains its existing history/signal policy.
Each chosen relay gets its own acquisition interval. Once it supplies stable
usable time, stronger peers and a returning GM cannot evict it. If no eligible
relay is available, a fresh eligible GM can still be tried within its retry policy.

A relay path rejected during capability read is deferred before clearing the
selected parent. Recovery keeps other relays eligible; it does not fall into a
GM-only search. Loop rejection remains mandatory. A depth-two parent is still
eligible as a time source even when this receiver cannot extend its path.

The parent trial is separate from each periodic-sync establishment attempt.
Each PA attempt still gets 1.5 seconds plus the original final 500 ms for proven
cancellation. Cancellation retries the same parent while its authenticated
binding remains live, without forcing another GATT renewal. A binding that
expires, a changed authenticated path, or an established PA stream with stalled
corrections still requests a fresh challenge. The 120-second precision trial
begins at first authenticated admission; an unadmitted relay is bounded by its
60-second admission interval instead.

Use journal kind 4 to identify actual parent changes. Kind 8 records PA
cancellation and does not by itself mean a parent switch. The RC1/RC2 time-source
label and its loss likewise do not establish which selection deadline expired.

A failed admitted timing source is deferred for 300, 600, then 1200 seconds on
repeated failures. Version 0.15.89 uses 60, 120, then 240 seconds for a GM that
previously completed the sustained-lock window before its interruption. A GM
that never settled keeps the longer deferral. Expired deferral only permits a
fresh trial: it does not clear failure history or evict a healthy relay, and each
selected relay still gets its complete admission/precision allowance.
Admission-only failures also use 60, 120, then 240 seconds.
This bounded history is advisory, scoped to stable identity and role,
with failures cleared by sustained observed recovery. Entries expire after 30
minutes or can be evicted from the eight-row table. History restores no locator,
authentication or timing authority. Diagnostic pins and relay-only overrides
retain their explicit role filters.

Parent retention never extends a map, source, lease or replay deadline. Expired
time is withheld throughout acquisition and recovery. Offline selector tests
exercise late relay lock with multiple stronger competitors, failed-GM return,
PENDING and renewal deadlines, recovery, failure history and source expiry.
They run under the normal T096 host suite; hardware relay acquisition and field
durability remain unverified for this change.

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


### Shadow PPS collection and display idle

From 0.15.96, the page first collects ten seconds of consecutive usable local
PPS observations against frozen BLE maps with the same parent/root identities,
boots and root source epoch. The screen shows `SETTLING 10S`. Missing records,
invalid reference/maps, capture-domain changes or a different timing source
restart startup qualification. The final qualifying PPS edge is the boundary;
statistics then include every later eligible sample, including large errors.
No phase magnitude is used to qualify startup. This is a fixed observation
interval, not proof that the clocks have converged or a guarantee about MAX ABS.

The raw `shadow` cursor remains available throughout startup. The `shadow_ui`
console object adds `startup`: `[records, invalid, missing, restarts, span_ms]`.
These counts are separate from the measured population's `samples`, `skipped`
and `missing`. Keep startup records and missing intervals when comparing runs;
do not compare a warmed population with an unqualified cold-start population
without identifying that difference. A first-sample maximum alone does not
establish a measurement fault or GNSS sawtooth error.

From 0.15.94, explicitly selecting shadow page 6 wakes the display and excludes
that page from the normal five-minute screensaver. The measurement session ends
when the page is left or its existing scope/reference conditions stop it. The
change does not keep a map, source or timing output valid. Collect raw signed
`shadow` records and their sequence/counter, map, parent/root and reference
metadata; the displayed mean is phase, not a rate measurement. Unknown PPS/UTC
association and uncalibrated RF timestamp origins remain explicit. Displayed
upper-tail percentiles use approximate histogram bins above 1023 ns; signed
mean and maximum retain the underlying usable session samples.

Version **0.15.100** retains an already running RC periodic train for up to
30 seconds through timing loss. Canonical count-zero packets preserve radio
synchronization while READY, correction, map, USB and output authority remain
withdrawn. Original source/authentication/child-lease deadlines are unchanged.
The RC-serving badge still requires current usable time; activity lamps blink
by design and are separate from precision status. `precision.rejected` now
reports the same wire-plus-timing rejection total as `network.rejected`.
Earlier RC99 precision payloads left this field unpopulated; their zero values
are not evidence of zero rejections. Retain source/version and counter scope.

## Precision page (0.15.118)

In the GM profile, Precision uses the single-line `SHD PPS MEAN` heading
above its value, followed by the separate `RMS` label and value. It displays shadow PPS mean and uncentered RMS from
the same independent BLE-clock-versus-local-PPS observer as the Shadow PPS page.
Remain on either page for the one-second dwell and ten-second contiguous
settling interval. Collection requires local GPS fix/PPS and a valid delivered
BLE map. Switching between these two pages retains the session; leaving them,
source identity changes or becoming a serving GM ends it. A serving GM displays
`LOCAL SOURCE` and unavailable comparison metrics. Missing inputs never fall back
to UART age or clock-fit RMS. Values are relative witness diagnostics, not
independently calibrated UTC/output accuracy.

`PPS mm:ss` and `HOT mm:ss` show consecutive observed hold durations, tracked
across page navigation. PPS loss and HOT withdrawal reset their respective
timers. A stale model resets both. The green padlock and `SoftPLL` identify an
eligible locked BLE follower; eligible HOT status preserves its green indication
during short fit-lock changes. HOT retains its reason line throughout acquisition:
GPS/PPS wait, source dwell countdown, peer/source wait, authentication wait,
BLE-map wait, control wait and qualified/active-GM state. These display states do not extend any source,
map, PPS, authentication or HOT deadline.

In RC mode, version **0.15.119** replaces the old correction-age readout with
`BLE RMS`: this receiver's own BLE capture-fit residual RMS, in ns/us/ms. It
uses the local timing owner's precision snapshot, independently of local GPS,
shadow PPS, advertised upstream bounds or the clock-face metric. Missing RMS,
a withdrawn map, expired BLE time, stopped BLE or stale UI input shows `--`.
This is a relative receiver-fit diagnostic, not calibrated UTC/output accuracy.
The old `CORRECTION` label and its age-display path are removed from Precision.

The five-minute screensaver is available on every page when the existing
clock/source eligibility checks pass. A selected Precision/Shadow PPS observer
continues behind the saver, retaining its session and independent-GNSS fence.
Navigating to another page wakes that page and restarts its idle interval.
