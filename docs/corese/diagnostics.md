# CoreS3-SE diagnostic field provenance

Copyright (c) 2026 Jonathan Yantis. MIT.

Version0.1.26 passively observes GM7 implementation3 management on SID14:
ANN, normal capability STATUS, ROLE, GNSS and WITHDRAW. The source remains a
receive-only client. The exact codec selection is recorded in
[its provenance](../../firmware/corese/protocol/provenance.json).

Legacy GS/GD remains readable on SID12. GD1 requires T0960.15.166 or later; GD2 adds real GGA HDOP with T0960.15.168
or later. Older producers leave the corresponding fields unavailable.
The client never manufactures a PTP dataset or treats display activity as a
renewed lease.

Reference Watch's `OBS` is local elapsed observation of selected-root reports
with known FIX and PPS both YES. It starts at the local accepted observation,
not a claimed remote lock time. Every accepted report is checked; NO, missing
fields, an original-deadline gap or source-context loss resets it. Replays
cannot renew it. `SAME` remains time since the HOT nominee identity/boot changed,
independent of HOT self-report freshness. Neither proves unobserved continuity.
Expired GNSS values say `LAST`; unknown fields stay distinct from known NO/zero.
The live Reference Watch SV-used numeral is orange below10 and green from10;
this color threshold does not reset OBS or alter FIX/PPS/QUAL or timing validity.

Epoch Counter milliseconds and its vernier follow modeled UTC, not physical PPS
or measured LCD edges. `P99 <1us HOLD` is the sampled shadow duration; `MIN`
marks a lower bound. Labeled LAST values retain only the existing500ms grace.
Mesh age uses current matched UTC minus signed GM7 birth; the birth/founder strip
stays UTC (`Z`) even when the clock uses local time. Missing/mismatched UTC or
legacy reports withdraw age. Long names shorten with a visible `>` marker;
the complete identity remains in diagnostics/console.

| Page | Source | Meaning and limits |
| --- | --- | --- |
| Mesh | GM7 ANN/STATUS/ROLE; legacy GS v1 | Reported ACTIVE/boot, timing epoch, leadership term, nominated HOT/boot and actual matching HOT STATUS. A nomination alone does not establish HOT. `UNCHANGED` timers track observed role events independently of packet cadence. Legacy GS retains its coordinator/membership fields. |
| Path | Current timing-owner snapshot | Canonical admitted RC7 path, node boots, trains and epochs; direct source has one node. RC9 cannot stand in for an RC7 path. |
| Quality | Current fine client | Causal RMS, signed prediction residual, matched/refused counts, source receipt age and remaining model/permission TTL. Delivery percentage remains unknown when its event denominator is unavailable. |
| Reference | GM7 GNSS; legacy GD2, 90-byte AD (GD1, 88-byte AD) | Selected root/timing incarnation/mesh epoch; reported GNSS FIX/PPS/source-QUAL lamps, satellites, real GGA HDOP and the current election quality tuple. Reported metadata carries no timing authority or measured UTC accuracy. |
| Wire | GM7 decoder/monitor | ANN/STATUS/ROLE counts, authentication/scope/age rejects, replay, term, receipt age, signed epoch start UTC, elapsed epoch time from current matching UTC, and separate local receiver uptime. Legacy GS shows only GM uptime estimate and local EPOCH SEEN because GS has no epoch birth. |

GM7 management uses SID14, manufacturer AD companyFFFF and the
`PToB/GM7/implementation3` authentication domain. The canonical random advertiser
must match the authenticated sender. ANN/ROLE leadership and GNSS descriptions
bind to the selected root, boot and timing epoch when selection exists. An empty
selection may retain an authenticated reported leader as descriptive history;
it does not become an admitted clock. Normal capability STATUS and non-ACTIVE
ROLE reports may have no epoch, and are counted without changing the selected
leader. Control, ROLE and GNSS use independent per-sender/boot sequence fences;
64-bit sequences are not truncated. The bounded eight-peer fence is RAM-only.

GM7 epoch identity is its UTC birth plus founder. `TERM` changes during leadership
handover without restarting that epoch. `EPOCH START / UTC` formats the signed
birth to whole seconds; console `birth_utc_us` retains its exact microseconds.
`ELAPSED` subtracts that birth from the current scope-matched UTC model; it
withdraws when UTC is missing, expired, retained-only, earlier than birth or tied
to a different root/boot/timing epoch. `LOCAL UPTIME` is this receiver's monotonic
boot duration, never epoch age or GM uptime. No message receipt or UI poll extends
the timing model. ANN UTC samples are not used to create a clock.

An ACTIVE's HOT field is a nomination. HOT NOMINEE / CURRENT and its green
identity follow the authenticated current ACTIVE report, independently of extra
HOT self-report freshness and local precision qualification. After that ACTIVE
report expires, the unchanged cached identity is labeled LAST SEEN. The UNCHANGED
timer starts when that identity/boot is selected, and resets only on a changed or
cleared selection. `hot_nomination_current` exposes the same distinction.
Matching same-epoch/term/train STATUS=HOT or ROLE=HOT supplies independently
reported state. Both keep their original4.5s receipt TTL, covering one lost
nominal2s report. Older cross-channel samples cannot undo newer peer state.
The SELF role and its real report age remain a separate line; stale self-reports
turn that line amber without withdrawing a current ACTIVE nomination or resetting
its timer. An explicit non-HOT report changes reported state;
term/train changes withdraw current report scope while preserving unchanged
selection age. Neither a nomination nor a descriptive report grants HOT takeover
or precision timing authority. ACTIVE YIELD and WITHDRAW are shown explicitly;
withdrawal expires the descriptive ACTIVE report immediately. GNSS observations
have their own original deadline and cannot restore a withdrawn timing source.
`mesh diag` exposes `protocol=3`, exact birth/founder/term, known/elapsed, local
uptime, nomination/readiness, counters and the most recent peer kind/flags/role/
blockers. `gd_seen_root/boot/epoch/advertiser/received_us` records the most
recent authenticated legacy GNSS scope, including a rejected scope; it never
replaces accepted Reference values or their expiry. GM7 GNSS carries the same FIX/PPS/QUAL/SV/HDOP/PTP fields and optional
reported UTC error; the latter remains raw diagnostic metadata, not measured
CoreSE UTC accuracy.

The0.1.26 listener uses current admitted UTC as a scheduling hint for the
actual250ms GM7 calendar:41–55ms receives ROLE43ms and ANN discovery copies45/50ms and85–113ms covers
STATUS90–107ms with jitter and a complete primary/gap/AUX event. The first
window opens after primary ANN38ms, so its AUX reception cannot consume the
nearby ROLE opportunity. A55ms discovery copy falls outside this protected
window; normal discovery and the other copies preserve leader observation. A fresh,
authenticated physical PA receipt younger than25ms is required to reserve each
window. Physical capture guards require opening at least8ms after that PA and
closing by56ms, before the next62.5ms timing reception. Original current-parent, physical reception, finite clock-model coverage and
scope gates remain checked while pending. Ordinary management visibility does
not require the raw sub-1us/one-second precision hold qualification. Precision
validity retains its separate requirements. Protected scan intervals are50ms;
normal discovery/admission keeps its prior parameters. This increases diagnostic
receive duty to about17%; it does not change fine timing validity or source
permission. Earlier sparse rotating windows could miss ordinary2s reports for
tens of seconds despite correct decoding. All reception remains passive, with
no SCAN_REQ or GM-to-GM GATT exchange.

Complete nonstale passive diagnostic callbacks are decoded even if the radio window
has since closed. Callback arrival time is not an RF event timestamp. The existing
window filter still protects parent/RC9 selection actions, and each accepted
diagnostic keeps its own authentication, replay fence and finite advertised TTL.
`scan_outside_callbacks` counts accepted reports arriving outside the latest scan
window while the timing parent is protected.

The following legacy GS/GD semantics remain available for older producers.

The source radio address must match the existing group-derived canonical address.
The selected root, boot and source epoch must match the timing owner. A replay
cannot refresh arrival time or validity. Accepted reports remain cached until a
newer authenticated selected-scope report replaces them during this observer boot.
A primary identity/boot change resets only the primary timer. A HOT identity/boot
change or explicit withdrawal resets only the HOT timer. Missing or expired GS,
source epoch updates, coordinator and membership changes do not reset those
identity timers. They keep advancing between reports; report age is shown
separately. They measure time since the last *observed* role event, not remote
oscillator lock or proof of uninterrupted remote operation. Cache retention never
extends the original GS validity window or any clock/source lease.
The bounded eight-context replay fence survives source selection changes within
the running image. It is volatile across device boots.

GS remains unchanged. The separate GD1/GD2 report uses the existing group-derived
canonical advertiser on SID12, a distinct HMAC domain and an independent
eight-context root/incarnation replay fence. Its boot field is the GM's
timing/profile incarnation, as in GS/G1, not its physical USB boot token.
The original local receipt plus authenticated valid interval (at most30s) determines
expiry. Duplicate sequence numbers, older remote sample times and replay after
scope changes cannot refresh receipt or expiry. The mesh epoch is a runtime
label: no deployment epoch value is compiled into the client. New GM identities
and timing incarnations use their own sequence namespaces.

Reference is diagnostic page4 and console `ui page 5`; navigation stays fixed.
The single AGE / CACHED indication shows local report receipt age. FIX/PPS/QUAL describe the
source's original sampled state, not a renewed local lock or source lease.
The UI omits RMC/PPS/GGA age rows. Their raw console values remain fixed ages
at the GM sample, never advancing remote measurements. Satellites zero is known
zero, distinct from unknown. A missing or different-scope report shows
dashes. A same-scope report remains visible after expiry as amber LAST REPORT /
CACHED data, with its actual report age. Known NO and zero remain distinct from
unknown. If selected timing scope temporarily becomes entirely empty, LAST GM / CACHED
retains that authenticated descriptive history. A different nonzero selection
hides it. Only the separate receipt AGE advances; the source flags
and PTP tuple are historical observations, never renewed current claims. Retained
report bytes never extend validity or reset receipt/expiry on duplicates.

`C` (clockClass), `ACC`/`VAR`, and `P1`/`P2` are the actual reported election
tuple. Unknown accuracy codeFE displays ACC UNKNOWN; absent varianceFFFF
displays VAR NONE. Other accuracy/variance codes retain their hexadecimal value. It does not establish IEEE1588 transport conformance, calibrated
UTC accuracy or traceability. HDOP is the dimensionless checksum-valid GGA value, displayed with two decimal
places only when the authenticated GD2 known flag is set. GD1 has no HDOP and
cannot populate it. Blank, unsupported, stale or malformed HDOP remains unknown.
HDOP is receiver geometry, not measured timing quality. PDOP, VDOP, antenna
health, timePropertiesDS and IEEE timeSource remain unavailable; GD1/GD2
source-kind1 means local GNSS only.
JOIN GNSS capability is not a substitute for GD1 sampled state. `mesh diag`
exposes separate `gd_` scope, original receipt/expiry, wire version, sequence, flags, fixed
source ages, HDOP centi-units, tuple and acceptance/rejection counters for bounded field verification.

`status`, `fine read 0` and `mesh diag` are separate CRC-protected snapshots.
Timing permission can expire or recover between their request timestamps. A
status reply with an empty selection followed by a live diagnostic reply does
not establish that both states existed simultaneously. When comparing scopes,
bracket the diagnostic with normal status replies and retain changed or empty
selections as unknown comparisons. Keep those samples and their denominators;
check each report's original receipt and expiry independently. Even matching
brackets are an observation interval, not an atomic timing-owner snapshot or
proof of uninterrupted source availability.

Diagnostics never query or modify a GM. Scanning is passive: it sends no
BLE scan requests. In version0.1.12, a protected healthy fine clock and a fresh,
authenticated, physically associated periodic reception may permit a12ms local
listening window, at least200ms after the previous opening. A receipt older
than4ms cannot open one in0.1.12;0.1.13 admits receipts less than10ms old to
account for fine association and decode latency. Repeating a captured event cannot reopen or
extend it. Earlier0.1.10/0.1.11 used80ms windows every800ms. Normal timing admission/renewal
remains independent; reading a screen or its cache triggers no master request.
GD-only occasional windows do not clear GS role history or reset its independent
PRIMARY/HOT observation timers.

RC9's authenticated 190-byte time snapshot contains functional UTC plus some
upstream link metadata. That RMS is a remote link observation, never the CoreSE's
fine causal RMS or an independent UTC bound. Current CoreSE rendering withholds
local precision readouts whenever RC9 supplies the UTC display.

WIRE uses stable cached labels and values between reports. `GM UPTIME EST` is a
coarse extrapolation of the GM boot clock at its last report, never UTC. `EPOCH
SEEN` starts when this client first accepts the reported mesh epoch label and
persists across root/timing-incarnation replacements at that same label. A new
label restarts it; it is not the epoch's actual creation time. Durations use compact day/hour/minute
notation. Neither GS expiry nor the latest rejected packet clears these values.

## CoreSE0.1.27 shadow holdover and local TZ

The sixth page is a rate-only shadow servo. It freezes phase and BLE-trained
frequency for120s; a bounded16/32s repeated-channel fit may trim its entry rate.
Later accepted physical BLE pairs are witnesses, never feedback to that frozen
candidate. The temperature fit is excluded and the delivered clock is unchanged.

P95/P99/MAX are absolute errors from the latest128 witnesses in the current run.
Sub1us production qualification admits a new trial; later RMS or lock changes
do not cancel that frozen trial or censor its tails. Scope replacement still
clears it, and finite run/witness deadlines preserve missing outcomes.
P95/P99 require100 witnesses; MAX is available earlier. Errors range from ns to
us at1000ns, ms at1000000ns and s at1000000000ns. The separate run maximum
retains tails that have rolled out of the128-event window. Gaps above250ms are
counted, and their interiors are unmeasured. Missing data is never zero-filled.

**P99 <1us HOLD** is seconds from the separate W12 sampled-prefix-max estimator,
with at least100 confirmed trials at each qualifying age and a rolling128-trial
population. Seven short2s lanes feed early results alongside one adaptive long
lane, up to120s. The first actual witness at/after an age includes sampled prefix
peaks and overshoot. Failed and unknown ages remain in their denominators.
**MIN / LOWER BOUND** means longer ages lack evidence; it does not mean a
measured failure. A numeric0.00 means the first250ms age failed the criterion.
A fresh percentile or duration is not a continuous bound or independent UTC/PPS
accuracy measurement. Scope change clears populations; stale/RC9/retained source
states withhold current shadow values. UI refresh never renews a witness.

The main clock and register use the leased selected-parent coarse TZ, matching
W12 labels CDT/CST/LOC and UTC fallback. Current GM4 ANN ordering and JOIN5/RC8
metadata remain distinct from fine timing. Direct metadata has a750ms lease;
relay metadata has300ms. Original authenticated receipt/presence supplies expiry,
not lock or polling. Explicit unknown cannot be resurrected by presence. Source
replacement clears TZ ordering. RC9 and retained HOLD show UTC. Quality residuals,
Reference and Wire epoch dates remain UTC; Quality names the clock display zone.

`shadow diag` carries quantiles, run maximum, coverage counts, horizon
population/unknowns, entry-rate selection, and the coarse TZ ID/offset/deadline.
`production=0 temperature=0` identifies this shadow-only population. Native
builds, host tests and synthetic previews do not establish hardware acceptance.
