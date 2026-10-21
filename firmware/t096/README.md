# T096 firmware example

<a href="../../docs/images/t096-rc-preview.png"><img src="../../docs/images/t096-rc-preview.png" width="160" alt="T096 RC display preview with digital time, analog dial and BLE source status"></a>

*Illustrative firmware-rendered RC screen; displayed values are not measured
performance. [Image provenance](../../docs/images/README.md).*

**PToB owns T096 RC7 development.** Current source version **0.15.145** supplies the
ordinary **GM** and **RC** mesh profiles, with USB timing compiled by default.
[Build from source](../../docs/t096/build.md) with pinned public dependencies
and ignored local enrollment. No enrolled binary is distributed.

The display uses an exact 17-color palette and one bounded DMA staging buffer,
recovering 7,680 bytes of static RAM without changing its pixels or cadence.
LoRa hardware defaults to disabled unless a private exact-factory binding is
explicitly supplied. Host checks and native memory inspection do not establish
physical SPI/BLE coexistence or delivered-time accuracy.

The [RC8 wire3 integration](../../protocol/rc8/README.md) adds exact actual-GM or
selected authenticated locked-BLE follower exports, an independent lower-quality functional
estimate, page7 controls and a bounded priority11 radio worker. BLE remains
preferred while usable. A transmitting follower needs no local PPS; an actual
GM retains its own PPS/source gate. Timing packets carry the full root identity and source epoch; a missed companion
still permits functional acquisition. Companions describe boots, parent and measured
quality about every120 seconds and on significant changes. Cold passive freshness and total uncertainty are UNKNOWN;
optional independent admission improves assurance. RC8 grants no RC7 timing,
GM promotion, RC9 or USB authority. Version131 adds a separate negative GM
election constraint: a live foreign LoRa root inhibits local ACTIVE/HOT unless
current direct matching-root BLE admission permits normal coordination. An
immediate relay parent cannot substitute for the actual GM. Self-root echoes
remain rejected, and original hold/withdrawal/source fences are retained.
Version134 implements [G5 ACTIVE/HOT/COLD coordination](../../docs/t096/gm-standby.md):
one serving ACTIVE, one appointed successor and a separate bounded COLD inventory.
Physical source qualification continues independently of coordination inhibition.
Version135 applies the same5s retry policy to missed and completed inventory
opportunities while ACTIVE has no selected peer, retaining the original1s window,
100ms connection attempt cap and2s discovery cooldown.
Version136 prevents foreign inventory JOIN from briefly putting a serving G5
ACTIVE into passive timing mode. CONTROL consumes PT's latched close intent even
if the scope recovers between cycles, preserving the first resource-drain deadline.
Version137 keeps one bounded contact phase before actual successor acquisition;
sparse COLD discovery cannot spend the PA acquisition deadline before appointment.
It also retains the original causal JOIN anchor across control promotion so a
delayed precision-owner job can acquire the selected root without fabricated timing.
Version138 restores the existing change-driven HELLO scheduling for new source permits
and epochs before the G5 return. It changes no source, map, grant or timeout bound.
The `network.control` diagnostic exposes existing management/pair deadlines, last
HOT withdrawal and receive latency/backlog counters without new stored state.
Version139 applies the existing30s CONTROL-publication stall observer to GM as
well as RC, including pending profile transitions. Terminal service errors or an
unfinished drain past its original deadline stop watchdog feeding without another
drain allowance. The existing hardware parent supplies the eventual reset;
healthy COLD operation and GPS/RF silence do not trigger this observer. No state,
timer or static RAM is added. Host tests do not establish physical reset latency.
Version140 gives a new successor acquisition one fixed45s budget, including the
existing5s stable-READY dwell. Contact remains30s; repeated exchanges cannot renew
either deadline. This allows more time for cold delivered-map convergence without
changing source, map, management, grant or repair gates. No role or state is added;
an unsuccessful selected candidate can occupy the pair15s longer. Physical
convergence and continued HOT renewal still require independent testing.
Version141 makes discovery windows start new work only. An accepted G5 transient
retains its original100ms initiation cap and5s resource expiry; JOIN/STATUS share
that original expiry, clipped by the current phase. One consumed initiation sets
the existing G5 retry opportunity, without a separate completion cooldown or
selected-anchor override. A fair pair dispatcher retains original message due
cadences and prioritizes an expiring response without restarting the ordinary
turn. CONTACT/ACQUIRE/HOT/REPAIR reuse the existing phase deadline. No stored state,
buffer, slot, source/grant lifetime or watchdog allowance is added. Host checks
cover software boundaries; physical durability remains a separate check.
Version142 permits a newer non-ACTIVE inventory report to describe physical
requalification in the same source epoch. The withdrawal floor remains stored:
old/equal reports and same-epoch ACTIVE claims stay rejected until the existing
causal HELLO path supplies its independent proof. Inventory can nominate a peer;
it supplies no source map, HOT status or grant. No timer, state or resource changes.
Version143 bounds each authenticated remote timestamp by its own causal-map
clock-drift distance. A future finite grant keeps its full conservative guard
without shortening a separate short message expiry. Original response clipping,
source/grant lifetimes, all cadences and repair dwell remain unchanged. A fixed
host delivery-burst regression checks continued HOT within valid map bounds;
physical RF durability and the controller preparation assertion remain open.
Version144 issues normal connection closure once and retains the original2s
retirement deadline through disconnect, TX and public-call callbacks. Missing
retirement becomes a terminal service error for the existing watchdog; buffers
and references remain owned until real completion or reset. READY freshness now
shares the existing2s management lease, preserving source/PT validity and all
original message/grant bounds. An explicit diagnostic canary records controller
prepare FIFO contents in the existing fault capsule; it is off by default.
These changes do not establish the cause or resolution of the controller assert,
physical HOT durability, failover latency or UTC accuracy.
The [remaining acceptance plan](../../docs/t096/gm-standby.md#development-checkpoint-and-remaining-acceptance)
keeps admission, source qualification, HOT renewal, watchdog reset measurement
and fourth-participant testing as separate open checks.
Finite successor grants, bounded repair/cooldown and paged `gm` diagnostics support
unstable/off-host testing. The sole requested LoRa transmitter may retain ordinary
BLE leaf timing while COLD; that exception adds no GM-control/HOT authority.
G5 has an explicit new outer control envelope; AUTH_CONFIG5 and RC7 timing are
unchanged. Host/simulator success does not qualify physical multi-GM durability.
This source corrects prior-correction validation ordering: the full immutable context
is populated before validating a carried prior event. Version124 discarded those
corrections despite mapped physical TX events. It remains an unqualified canary integration until its
separate native/resource and authorized exact-device checks pass.

Page7 shows source/parent diagnostics and an RX-only LoRa link affine residual
RMS from4..16 matched physical radio event pairs. COLLECTING/NO TIMING identify
missing local fit data. The metric measures relative consistency; it is separate
from advertised parent/BLE quality and grants no absolute accuracy bound.
Reset-aborted software launches consume their possibly transmitted sequence,
retire fully and restart at a bounded cadence; real completed-launch timeouts
remain faults. Timing and companion hardware cutoff durations are unchanged.
Version126 also resets an actual BUSY-high deadline only while SetTx is
unfinished and DMA has retired without a transport fault. The ambiguous sequence
is consumed before bounded closure/retry. Other BUSY deadlines stay faulting;
new diagnostic count/opcode fields distinguish this path without added RAM.
Version127 captures the first LoRa fault through the existing event journal and
uses the hardware watchdog for real transport faults or a frozen owner. Unsafe
DMA is retained until MCU reset. The record distinguishes actual closure and
journal success/failure; a conditional retained reset capsule carries the cause
across a same-image watchdog boot. Source/RF silence alone never requests reset.
The journal's startup scan scratch funds this observer without added static RAM.
Version128 also cancels an in-progress start before the out-of-band fault fence;
a returning worker cannot release reset after the watchdog has inhibited it.
Version129 keeps the original chip/FEM launch budget at software retirement,
and wakes/prioritizes inhibited drain work above console/diagnostic writers.
BLE priorities, the200ms drain bound and all existing reserves remain unchanged.
Version130 fires the LoRa toggle once at1.5s and consumes that press, while
intentional4s poweroff stays on other pages. Its schema6 journal carries the
primary fault through ordinary commits/page erases until a newer terminal
cause supersedes it, within the existing bank0 owner and RAM reserve.
Version132 gives the dedicated radio owner its GPIO NSS line. Successful
completion of the SetTx bytes starts the original FEM window and releases NSS
in one bounded GPIO/timer fence. A delayed worker return cannot turn an already
committed healthy launch into a reset/retry storm. Precommit, source deadline,
BUSY/DMA and true completed-command timeout guards remain separate. No SPI or
wait is added to the completion callback, and BLE priorities stay unchanged.

T096 participates in the shared G1/RC7 mesh with W12 and StickS3 receivers and
experimental relays. RC selection considers aged timing/delivery history as
well as signal strength, gives authenticated relays time to acquire or recover,
and retains a healthy parent. Both RC1 and RC2 streams are eligible; a receiver
following RC2 stays a leaf. Earlier authentication renewal, preserved retry
backoff and source-aware recovery retain opportunities before the original
binding expires. Explicit parent identities support bounded comparisons;
normal operation uses automatic selection. A newly detected signed serving-GM
announcement requests a direct authentication attempt before relay acquisition
protection or old non-root cooldown. Scanning resumes when a connected parent
stops supplying usable time. A previously working parent is retired once usable
time has been absent for ten seconds and PA is no longer synchronized. Exhausted
acquisition or degraded recovery also clears the selection even when no other
parent is visible. The old owners drain before selection and timing state are
cleared; returning identities require fresh admission. The announcement grants
no timing permission.

An established downstream train can survive a timing interruption for up to
30 seconds using packets with no corrections. Timing permission withdraws
immediately; source/map expiry, child leases, authentication, startup lock and
controller drain deadlines retain their bounds. A brief analog-display grace
and longer watchdog recovery allowances do not extend timing authority.
See the [changelog](../../CHANGELOG.md) for policy details and the
[selection and diagnostics guide](../../docs/t096/diagnostics.md) for checks.

RC exports its accepted BLE clock over USB independently of downstream relay
transmission. Use a compatible RC for the current
[macOS USB timing chain](../../clients/macos/README.md). The normal GM backend
has no reachable USB source-map publisher; its USB interface alone does not
establish UTC delivery. Continuous BLE/USB service and calibrated UTC/output
precision remain unqualified. The dated
[stability collection](../../evidence/stability/2026-10-16/README.md) preserves
earlier failures and its own source identities.

**[Direct Connect (DC)](../../docs/t096/direct-connect.md)** is a separate,
flash-only GNSS-to-USB image with Bluetooth compiled out. It uses no group
enrollment and cannot be selected through the GM/RC toggle. A second USB serial
function carries original GNSS output and bounded RTCM/configuration input;
raw observations depend on receiver firmware and do not imply onboard RTK.
DC and RC share an immutable USB publisher whose original source, map and
session deadlines cannot be renewed by polling.

**[Power Control (PC)](../../docs/t096/power-control.md)** is a separate,
flash-only awake BLE leaf experiment. Fixed listening/HOLD budgets and causal
BLE/PPS observations characterize experimental relative precision. It does not
implement sleep or measure energy, and cannot select an ordinary profile.

All targets use the [shared display registry](../../registry/README.md).
[PSKs](../../docs/security/psk.md),
[identity rules](../../docs/security/identity-and-names.md) and the
[group setup guide](../../docs/GETTING_STARTED.md) describe local deployment
inputs. Real enrollment, maps and secret-bearing images remain ignored.
Mutable [provisioning/storage](../../docs/t096/provisioning-design.md) and a
generic public installation/recovery flow remain pending. Hardware updates
require separately authorized exact-device guards.

[Source provenance](imports/README.md),
[baseline limitations](../../docs/t096/baseline.md) and the
[cutover record](../../docs/t096/cutover.md) distinguish the current application
from selected historical sources. The
[V14 papers](../../papers/README.md),
[RC1 contract](../../protocol/v14_rc1/README.md) and
[R2 evidence](../../evidence/v12/r2/README.md) retain their own editions and
qualification limits. Build or simulation success does not qualify hardware.

## Timing schematic

[<img src="../../docs/images/t096-timing-schematic.jpg" width="800" alt="T096 timing architecture schematic">](https://ptob.dev/hardware/t096/timing/)

Logical capture, clock-discipline and delivery paths.
Select the preview to open the [full interactive schematic](https://ptob.dev/hardware/t096/timing/).
[Capture provenance](../../docs/images/README.md).
