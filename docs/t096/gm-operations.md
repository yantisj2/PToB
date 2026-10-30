# Operate a T096 GM7 group

Copyright (c) 2026 Jonathan Yantis. MIT.

This guide describes **T096 0.15.209**, profile **GM7v4+S2+C1+TZ1**.
Use the [implementation guide](gm7-implementation.md) for exact behavior and
[build guide](build.md) for preparation. This documentation grants no hardware
operation or installation scope. Physical failover, sustained relay availability
and calibrated UTC/output accuracy remain unqualified.

## 1. Prepare trust and source roles

Use independently GNSS-capable T096s with distinct verified identities and
compatible coarse revisions. Provision trusted group credentials separately
from runtime selection; discover available meshes/sources locally rather than
pinning a production image to one mesh ID. Context changes require fresh
authenticated admission. Keep credentials/images ignored and use one
[shared display registry](../../registry/README.md); an alias grants no authority.
See the [setup](../GETTING_STARTED.md) and [identity rules](../security/identity-and-names.md).

Select **GM** for source-capable clocks. ACTIVE and HOT require their own valid
GNSS/PPS and continuous 60 s independent qualification. A GM can receive before
qualifying locally; selecting GM or receiving a good clock does not create
source eligibility. Shared reference failures do not supply source diversity.

## 2. Establish ACTIVE and confirm HOT

Begin with two GMs and one receiver in direct radio range. A qualified initial
GM waits 30 s of quiet observation plus up to 500 ms deterministic backoff;
a healthy cold startup normally takes about 90 s plus backoff. Observe one
ACTIVE serving and the other following its exact identity/boot/timing scope.

ACTIVE's ordinary ANN names one preferred successor. A qualified nominee echoes
HOT in ordinary STATUS, with a current same-scope shadow map/fit and physical
phase preparation. Coordination uses broadcasts; ordinary GM following needs
no JOIN/GATT exchange. Independent source qualification and actual delivered
shadow readiness remain necessary throughout.

Read `gm 00`: distinguish `preferred_hot`/`preferred_hot_boot` from fresh
`ordinary_hot_confirmed`. Preference can survive a temporary readiness gap for
up to 30 s; it is not confirmation or a time grant. ROLE reports, HOT duration,
low RMS and the strict precision LOCK badge describe different facts.
[Nomination diagnostics](diagnostics.md#hot-nomination-changes-015194) explain
expiry, explicit ineligibility and scope changes.

Add further GMs individually and observe the original pair plus each follower.
Additional capable clocks may remain COLD. Eligibility/ranking and bounded peer
capacity apply; no client-capacity or four-GM hardware acceptance follows from
an offline model. A healthy ACTIVE may serve with **NO HOT**.

## 3. Interpret loss, succession and merge

Keep selected root/boot, actual timing-map validity, own GNSS/PPS qualification,
nominee confirmation and output state separate. Advancing selected ANN,
self-ACTIVE ROLE or direct timing headers supply liveness; duplicate/reordered
traffic does not. Map loss alone does not mean the transmitter stopped.

Only a designated, still-qualified HOT has the fast silence path: trigger at
2.5 s from last advancing selected-GM communication, with first completed G1
targeted within 3 s of that endpoint. Actual source, capture, phase, stop/drain
and output guards still apply. Without usable HOT, an independently qualified
COLD GM uses ordinary 30 s quiet election plus backoff. These policies are not
measured continuity or recovery guarantees.

YIELD has an immutable 750 ms stop-before-start boundary; repeated copies cannot
move it. WITHDRAW/source loss stops positive output immediately. Succession
preserves mesh birth/founder and advances term. When ACTIVE meshes meet, the
newer lineage stops output before following the older one. A retained map,
control observation or timezone pair cannot authorize replacement serving.

Retain original receipts, outages, refused/missing observations and exact
source/boot scope. Source-loss, reboot, RF partition, controlled handover and
return tests need separately authorized device scope; this guide does not start
them. [Current TZ results](auto-time-zone-plan.md#four-device-follow-up--29-october-2026)
preserve passed direct checks and failed relay availability. Historical G5
observations remain in the [standby study](gm-standby.md) and Git history.

## 4. Add receivers, relays and local time

Add compatible **RC** receivers one at a time. Check their actual selected parent,
root/boot, fresh timing and unavailable periods before adding another hop.
RC7 paths authenticate at most two relay transmitters; a relay-only constraint
must not silently fall back to a direct GM. COLD GMs need no HOT designation to
receive. [W12 BLE50](../../firmware/w12/README.md) is client-only; optional
estimated-TX relays retain unknown bounds and their target-specific limits.

Use the same coarse profile along the path: GM4, JOIN5 and RC capability/control8.
T096 participants need .209 or newer to receive dynamic offsets after timing
synchronization. [W12/N30 ports](../esp32-time-zones.md) consume the same TZ pair.
In [automatic mode](auto-time-zone-plan.md), the serving GM selects its own
zone/calendar; manual-zone follows the chosen calendar and manual-offset stays
fixed. Receivers inherit the pair and return to UTC on unknown/expired metadata.
TZ does not qualify a clock or alter precision timestamps, PPS or USB UTC.

LoRa is a separately configured functional fallback with unknown bounds and
its own [source/authority rules](../../protocol/rc8/README.md); it does not supply
BLE mesh TZ or RC7/GM authority. Review target antenna/radio requirements before
any separately authorized transmitter operation.

## 5. Connect macOS over USB

Use either **GNSS → GM → BLE → T096 RC → USB → macOS**, or the separate
**GNSS → T096 DC → USB → macOS** path. USB timing is standard in RC7;
the normal GM backend does not publish a USB source map. [DC](direct-connect.md)
is flash-only, with Bluetooth disabled and no runtime GM/RC toggle.

Follow the [macOS guide](../../clients/macos/README.md) and
[build instructions](../macos/build.md). Normal launch opens hardware and starts
its owned unprivileged `chronyd -x` user clock; offline self-tests do neither.
OS clock discipline and LAN serving are not configured. SS3's CDC console is
not this USB timing source. Keep unavailable records and independent UTC
uncertainty distinct from local clock agreement.
