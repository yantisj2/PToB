# GM7 diagnostics and UI migration plan

**Frozen plan for GM7-draft1 — REVIEW CANDIDATE / NOT FIRMWARE CLEARED.**
Copyright (c) 2026 Jonathan Yantis. MIT.

This plan is part of the required GM7 migration, not optional cleanup. It defines
future implementation and acceptance scope; it changes no production file or
existing wire contract. New diagnostic layouts/domains are separate review gates,
not claimed frozen by this plan. No GATT replacement service is proposed.

## Owner state and durations

HOT duration is local monotonic elapsed time from the owner-derived transition
to armed, independently eligible, shadow-prepared HOT. Keep that start through
routine ANNOUNCE/STATUS, unchanged nominee echoes, PPS/fit refresh, page changes,
and the intended short ACTIVE blackout. Expiry of a received timing map alone
must not reset an otherwise valid armed HOT timer. Do not feed display state
back into qualification or permission. Genuine revocation, independent source
fault, boot/profile change, leadership scope change, or transition into ACTIVE
ends/resets current HOT duration. Preserve completed durations only with explicit
historical labels. Unknown/stale owner snapshots cannot fabricate continuity;
mark current duration unknown until owner truth is available.

PPS continuous duration is separate: start at first accepted local physical PPS
in its current continuity scope; reset/end on actual PPS absence/invalidity,
capture/source discontinuity or boot. Radio/nomination/page changes alone do not
reset it. ACTIVE duration starts when the owner actually starts serving and ends
on actual output stop, source invalidity, WITHDRAW, completed timed yield or
losing merge, regardless of whether following has begun. A retained mesh birth
is neither PPS uptime nor ACTIVE/HOT duration. Changes to old timer semantics
must be explicit in schemas/labels and tests; do not silently recast old values.

Preserve role labels and distinctions: ACTIVE, prepared HOT, and other standby
(COLD where that legacy presentation remains). Distinguish nominated/preparing
from armed HOT. Keep independent GNSS/PPS status, source quality, Bluetooth beat,
identity/name mapping, ordinary RC and separate DC paths. DC remains flash-only,
Bluetooth disabled; no runtime GM/RC/DC unification. Shadow precision, received
fit RMS, local PPS comparisons and calibrated absolute uncertainty remain distinct.

Preserve shadow-PPS explicit measurement-page session/settling behavior and its
bounded worker ownership. Other page rendering/console reads must not trigger
sampling, requalification, radio/control startup or any daemon. Page navigation
cannot silently reset role duration or alter election. Preserve existing UI
stale-model handling and unknown bounds.

## Exact producer/consumer work inventory

| Surface | Current source and required migration |
| --- | --- |
| T096 local UI | `firmware/t096/src/ui_render.c` (`r2_ui_holds_update`, `hot_current`), `include/ui.h`, `src/ble_service.c`: replace current map-valid/precision-authorized HOT presentation gate with owner-derived armed state and truthful freshness. Preserve renderer paths, `src/display.c` and `legacy_ui` behavior as applicable. |
| T096 role producer | `firmware/t096/protocol/role_announcement.{h,c}`, `gm_role_status.h`, `role_status_v1.json`, `role_status_vectors.json`: current connected/authenticated coordinator/pair/grant-derived HOT snapshot must become an explicit GM7 semantic adapter. Do not manufacture coordinator/grant values. |
| T096 console | `firmware/t096/protocol/gm_diagnostics.{h,c}` and `src/ble_service.c`: version GM JSON snapshot/console pages and update owner snapshot publication atomically. |
| GNSS diagnostics | `firmware/t096/protocol/gnss_diagnostics.{h,c}`, `gm_role_status.h`, existing `protocol/gm_diagnostics_v2`, `tools/check_gm_diagnostics_v2.py`: preserve old strict GD1/GD2 contracts; create separately reviewed successor revision. |
| CoreS3-SE, actual GS/GD consumer | `firmware/corese/main/mesh_owner.cpp`, `mesh_wire.cpp`, `mesh_diagnostics.{hpp,cpp}`, `display.cpp`, `ui.{hpp,cpp}`: migrate decoder, scope/freshness cache and MESH/PATH/QUALITY/REFERENCE/WIRE pages together. Retire coordinator text. |
| SS3, actual GS consumer | `firmware/ss3/main/native_g1.cpp`, `td_role_update.hpp`, `ss3_td_model.hpp`, `ss3_native_display.cpp`, copied `r2/r2-g1/role_announcement.{h,c}`: migrate active build members and distinguish presentation stability/poll cadence from protocol state. |
| T096 RC, actual GS consumer | `firmware/t096/src/rc_dynamic_service.c` role cache: version decoder and cache fences while preserving RC behavior. |
| W12/N30 | Audit actual build membership and copied codecs. No direct GS/GD main consumer was identified in the current search; do not invent an implemented decoder or unsupported GM functionality. Record exact availability in implementation review. |
| macOS | Audit USB source/provenance consumers; no direct GD consumer was identified. Preserve USB expiry and unknown bounds. Offline checks must not launch hardware or chronyd. |

CoreS3-SE primary/HOT “UNCHANGED” durations are observer-local identity stability,
not proven remote continuous service/HOT. Preserve that label and distinction.
Its `epoch_since_us` is observed old mesh-label age, not UTC birth. Add real UTC
birth separately, retaining unknown when unavailable; never synthesize remote
uptime. SS3's current 10-second HOT observation-stability filter and 120-second
poll require explicit diagnostic-lag assessment. They cannot delay GM7 takeover
or imply instantaneous remote HOT confirmation.

## Schemas, packets and caches

Before firmware clearance, inventory every existing GM console/GS/GD field and
its new source, scope, units, unknown state and consumer. Freeze separately
versioned replacement JSON and diagnostic packet layouts, HMAC domains,
reserved-field rules and public synthetic fixtures. Preserve strict legacy
length/version/MAC decoding and historical fixtures. Never reinterpret or widen
the old u32 mesh epoch as UTC; never truncate the new u64 UTC birth to fit it.

New descriptive snapshots need full UTC birth, founder, leader term, current
ACTIVE identity/boot, nominated HOT identity/boot, nomination/preparation state,
local source/capture/association generations, ACTIVE train, local shadow map,
source ages/expiry/known flags, independent dwell, original observation freshness,
blocked/revoked reason, last transition reason/counters and local timer validity.
Reason codes must distinguish no GNSS/PPS, dwell, shadow unready, wrong nominee,
wrong boot/term, stale evidence, terminal WITHDRAW, merge loss and UTC unknown.
Expose current/historical/unknown labels. Coordinator, grants and acquisition
attempt fields must be explicitly retired or marked unsupported in a new schema;
never populate legacy fields with invented truths or silently repurpose them.
Bound snapshot work/memory and counter overflow behavior; diagnostics cannot
renew timing authority or change control state.

Current GS role AD is 116 bytes and GD2 GNSS AD is 90 bytes, already exceeding
the current 191-byte ceiling together. GM7 PDU162 + AD framing =166 leaves only
25 bytes. Do not concatenate the existing diagnostic records. Design a bounded,
freshness-aware multiplex schedule that preserves primary timing and frequent
ANNOUNCE deadlines and STATUS jitter budget. Budget actual airtime/controller
capacity and maximum diagnostic gaps, including bursty withdrawals; GS expiry
can already occur under multiplexing. Diagnostic lag must be visible. Repeats
or cache redisplay never renew original diagnostic expiry. Diagnostic TTL is
not an authority lease and must not compensate for missed control deadlines.

Update producer → wire decoder → scope/freshness cache → UI together for every
actual consumer. New root/boot/source/capture/train/term fences affected caches,
even when birth/founder remain fixed. A retained negative WITHDRAW fence cannot
be cleared by a descriptive GS/GD or late positive report. Preserve unknown,
saturated and false distinctions in GNSS fix/PPS/satellites/HDOP/quality values.
Diagnostic packets remain descriptive and confer no election, timing, HOT,
precision or clock-control authority.

## Offline acceptance required after clearance

| Scenario | Required regression/preview evidence |
| --- | --- |
| Nomination and revocation | Nominated/preparing distinct from armed HOT; correct identity+boot; timer begins once, revocation ends it; no coordinator/grant fiction. |
| Routine traffic and transient timing gap | Beacons/status/fit updates/pages retain owner-authorized HOT start; unknown owner snapshot stays unknown; no diagnostic read causes sampling/control. |
| 2.75-second blackout and takeover | Original send/contact age drives deadline; delayed receipt adds no budget. Armed HOT timer survives intended short gap; takeover ends HOT and begins actual ACTIVE duration; no receiver-reacquisition guarantee. |
| Voluntary timed yield | Qualified ACTIVE stops by fixed boundary; successor scope matches; stale/missed yield and UI transitions remain truthful. |
| WITHDRAW/source fault | Output stops immediately; source/PPS timers reflect actual faults; negative fence blocks same-scope positives even at higher sequence; no unarmed takeover. |
| Epoch merge and returning former GM | Older comparable birth wins, equal-time founder and same-lineage term rules apply; losing output ends before follow; remote observed duration is not UTC age. |
| New term/reboot/source/train change | Correct cache/map fences and current timer resets; preserved lineage birth never fabricates source continuity or remote uptime. |
| Stale/replayed/mismatched diagnostics | Strict decode and scope checks; no expiry extension on repeats, no false eligibility or timing permission; unknown displays remain unknown. |

Future tests include T096 `tests/ui_precision_test.c`, `ui_test.c`,
`ui_framebuffer_test.c`, `shadow_session_test.c`, `shadow_worker_test.c`,
`gnss_diagnostics_test.c`; CoreS3-SE `tests/mesh_wire_test.cpp`; SS3
`tests/td_role_update_test.cpp`, `td_role_native_test.cpp`,
`td_health_display_test.cpp`; and the separately versioned diagnostic codec
fixtures plus renderer/preview tests for all affected pages. These are planned,
not run here. Add negatives for new schemas/domains, high u64 birth values,
missing/unknown fields and legacy coexistence. Record actual port build
membership and preview coverage, not assumed cross-target support.

The Sol6.1 Extra High engineer may implement and build only after user firmware
clearance and overseer protocol review. Review the complete consumer migration,
new packet budgets and tests before declaring the integration ready. Native
builds and offline previews do not qualify hardware. Hardware scope, recovery,
independent metrology and sustained multi-GM acceptance remain separate.

## Required redundant-event and RX/TX ownership work

The frozen [control-window contract](SPEC.md#bounded-event-repetition-and-aligned-windows)
adds three immutable copies of YIELD/WITHDRAW on consecutive 250ms frames.
Future owner integration must reserve ACTIVE control TX and standby control RX,
ACTIVE STATUS RX and bounded in-window standby STATUS TX; map those windows to
the accepted ACTIVE timing frame with bounded uncertainty/drift. Do not let
periodic timing work, half-duplex turnaround, scans, diagnostics or UI work
silently displace them. Diagnostics have lower priority than these required
control windows; multiplex/drop them with truthful freshness when capacity is
insufficient. No extra GATT/ACL connection or assignment handshake is added.

WITHDRAW stops source output immediately, independently of later negative
control copies. Cancel pending YIELD and stale positive TX queue entries on
withdrawal; recheck terminal scope at dispatch. Duplicate copies cannot restart
HOT/ACTIVE timers, renew physical source evidence, change nominees or extend
handoff/expiry. Preserve armed-HOT listening through the short blackout with
accumulated drift bounds. Fail-fast when mapping/guard trust is exhausted;
unqualified uncertainty must not be hidden by a successful scheduling call.

New versioned diagnostic snapshots must expose logical-event versus copy
counters: planned opportunities, actual TX timestamps/results, RX accepted,
duplicate/reordered rejection, late-window drops, missed-copy counts, guard
budget/exhaustion, alignment source/age, fixed yield deadline and superseding
withdrawal/cancelled positive-queue counts. UI labels should distinguish event
intent, scheduled copy and observed/accepted event; never present TX submission
as remote receipt or “guaranteed failover.” Bound diagnostic work and preserve
its descriptive-only role.

Offline schedule checks cover each single/two-copy loss, complete loss fallback,
late first success, duplicates/reordering, terminal supersession, jitter extrema,
guard exhaustion and late processing without deadline extension. They use
synthetic budgets and do not exercise a radio. Before hardware clearance,
measure worst-case timing/control/radio work, scan/advertising coexistence,
turnaround, actual on-air timestamps and end-to-end event processing under load.
Verify guards and primary-stream deadlines with actual observations, including
all-copy loss and asymmetric reception. Native/offline success alone cannot
establish RF delivery or the 3-second hardware target.
