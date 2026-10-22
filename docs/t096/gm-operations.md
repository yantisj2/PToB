# Operate a T096 GM group

Copyright (c) 2026 Jonathan Yantis. MIT.

The accepted development checkpoint is **T096 0.15.164**, using **G5 control
protocol 5**, **RC7 timing** and **LoRa wire 3**. This guide covers a connected
group with three or four GM-capable clocks and one or more receivers. Durable
failover, fourth-GM participation and independent UTC accuracy remain unqualified.

## 1. Prepare the group

Use independently GNSS-capable T096s with distinct verified device identities,
the same reviewed current GM firmware, and matching private group ID, PSK and
key epoch. Supply one [shared registry](../../registry/README.md) to all builders;
aliases grant no authority. Keep enrollment and enrolled images ignored locally.
Follow the [group setup](../GETTING_STARTED.md), [identity rules](../security/identity-and-names.md)
and [T096 build/update guide](build.md) for exact commands and installation guards.

Select **GM** on each source-capable clock. Eligibility for **ACTIVE/HOT**
requires a valid local GNSS fix, live PPS and independent source qualification,
including the existing 60-second dwell. COLD can receive timing before qualifying
locally. Selecting GM does not qualify GPS or create source time.
References with shared failure modes do not provide independent source diversity.

## 2. Establish a pair, then add GMs

1. Start with two GMs in direct radio range. Observe one **ACTIVE** serving time
   and one appointed **HOT** only when its local source, received precision,
   readiness and finite successor grant qualify. Confirm ACTIVE's acknowledged
   global HOT as well as the standby's local HOT; either alone is insufficient.
2. Add the third GM after the pair is stable. It normally remains **COLD**,
   receiving authenticated precision timing while continuing independent local
   GNSS qualification. COLD reception grants no HOT or takeover authority.
3. Add a fourth in a separate step, retaining observation of the original pair
   and every follower. Controlled fourth-GM acceptance on this checkpoint is
   still open; three/four-candidate simulations do not close it.

A healthy pair retains its appointment when another candidate arrives. For a
new nomination, ACTIVE compares quality tiers, successful acquisition history,
fewer local failures, then full identity. Unappointed clocks use bounded
inventory exchanges rather than persistent all-to-all GM control connections.
No qualified nominee means **NO HOT** while a healthy ACTIVE can keep serving.

## 3. Read status and retain interruptions

Local PPS duration, received-stream RMS and HOT duration describe different
things. A HOT timer can restart while local PPS stays continuous. Check source
validity, selected root/boot, map validity, precision lock and both pair members;
RMS measures relative consistency, not independently measured UTC error.

The existing **45-second control windows** and **90-second finite grants** do
not extend shorter SERVICE, source or precision deadlines. Takeover depends on
current authority and output fences; it is not instant. Timer/handoff redesigns
remain evaluation-only.

The [1,800-second observation](gm-standby.md#source164-extended-field-checkpoint)
completed 112 three-unit cohorts, but its offsite ACTIVE was not independently
observed. A [later real HOT withdrawal](gm-standby.md#source164-later-hot-withdrawal)
at **50:28 uptime** occurred on the same boot/source epoch with continuous local
PPS and a brief delivered-timing/readiness gap. Grant/management time remaining
was about 89/45 seconds. Raw SERVICE expiry is the leading inference; the
initiating delivery/renewal failure and SERVICE-versus-map diagnosis remain
unresolved. Preserve outages, missing samples and failures when evaluating a group.

## 4. Add receivers and relays

Add enrolled, compatible **RC receivers** one at a time in direct radio range
of ACTIVE. Verify their actual selected parent, fresh source and timing before
adding another device or hop. COLD GMs already follow timing;
they need no HOT appointment to receive it.

A relay needs its target's supported transmit and downstream admission path.
The RC name alone does not enable relaying: [W12 BLE50](../../firmware/w12/README.md)
is client-only, and optional estimated-TX relays remain experimental. Use the
[target guides](../../README.md#get-started) and retain each hop's unknown bounds.
No qualified client-capacity guarantee follows from this guide. A general
phone/Mac GATT time client is not implemented; direct Mac BLE is disabled.

LoRa is a separate optional fallback; ordinary BLE setup needs no tuning.
Configured solo GMs may listen passively; mesh participation suppresses GM RX
by policy, so RX suppression alone is not a fault. Fit the proper antenna before
enabling any configured transmitter. See the [LoRa contract](../../protocol/rc8/README.md).

## 5. Connect macOS over USB

Choose either supported path:

- **GNSS → GM → BLE → T096 RC → USB → macOS.** USB timing is compiled into
  ordinary RC7 by default; the normal GM backend does not publish a USB source map.
- **GNSS → dedicated T096 DC → USB → macOS.** [DC](direct-connect.md) is a separate
  flash-only image with Bluetooth disabled, no enrollment and no runtime GM/RC toggle.

Follow the [macOS client guide](../../clients/macos/README.md) and
[build/launch instructions](../macos/build.md). The client uses hardware-captured
USB frame boundaries and normally starts its owned, unprivileged `chronyd -x`
user clock. It does not set the OS clock or serve LAN time by default. Offline
self-tests open no hardware and start no daemon.
