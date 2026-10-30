# PToB time zones and coarse metadata

Copyright (c) 2026 Jonathan Yantis. MIT.

Current implementation: **T096 0.15.209**, with [W12 0.2.21+ and N30 0.2.37+
receiver ports](esp32-time-zones.md). The [versioned wire contract](../protocol/time-zone-v1/README.md)
defines exact coarse layouts, authentication and synthetic fixtures. Full hardware
certification remains open; implementation, installation and qualification are
separate. [Current T096 results](t096/auto-time-zone-plan.md#four-device-follow-up--29-october-2026)
retain passed direct checks, failed relay availability and untested handover/DST gates.

UTC is the primary time base. Four authenticated coarse bytes carry the serving
source's time zone and current effective civil-time offset. GMs select the pair;
receivers display it and relays forward it unchanged. Precision packets receive
no TZ fields.

## Four-byte metadata

Offsets below are relative to the extension. Both fields are little endian;
GM7's other coarse fields retain their declared big-endian encoding.

| Offset | Bytes | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 2 | `tz_id`, unsigned | Stable PToB identifier mapped to an IANA zone name |
| 2 | 2 | `tz_offset_min`, signed | Current effective whole minutes east of UTC |

Use checked signed arithmetic: `local = UTC + tz_offset_min * 60 seconds`.
Negative offsets are west of UTC. Valid offsets span -840 through +840 minutes,
including fractional hours. The canonical unknown pair is `(0, INT16_MIN)`,
encoded `00 00 00 80`; the unavailable sentinel is -32768.

The [append-only registry](../tools/time_zones/zones.json) reserves these IDs:

| PToB ID | Zone |
| ---: | --- |
| 0 | Unknown |
| 1 | UTC (`Etc/UTC`), requiring offset zero |
| 2 | `America/Chicago` |

These are PToB assignments, not IANA numeric IDs. Never derive IDs from row order,
reorder/reuse them during regeneration or merge distinct public IDs. A zone
identifies a region rather than a seasonal abbreviation: Chicago remains ID 2
through CDT/CST changes. Receivers use the transmitted offset without a database
or local DST calculation. An unfamiliar nonzero ID with a valid offset remains
usable and must forward unchanged.

Missing/unavailable fields, ID 0 or an invalid metadata combination produce
UNKNOWN and UTC display. Invalid TZ metadata must not invalidate otherwise valid
authenticated UTC or grant time permission. Framing, version or authentication
failure rejects the record. No DST bit, zone-name string, transition schedule,
database revision or separate TZ sequence counter is transmitted.

## GM selection and receiver presentation

The serving GM publishes one atomic `(tz_id, tz_offset_min)` snapshot. Supported
[T096 modes](t096/auto-time-zone-plan.md) are:

| Mode | Behavior |
| --- | --- |
| Automatic, normal default | Own fresh GNSS position selects a supported region; its calendar supplies the offset |
| Manual zone | Chosen zone/calendar supplies DST offsets without location lookup |
| Manual offset | Explicit fixed pair until configuration changes |

Automatic selection uses conservative regional coverage, bounded location hold
and finite calendars. Ambiguous/stale location cannot preserve a pair indefinitely;
calendar expiry produces unknown immediately. The [GM guide](t096/auto-time-zone-plan.md)
and [database design](time-zone-database.md) record exact coverage and limits.
A standby may prepare its own local selection while displaying the serving GM's
pair. On takeover the new ACTIVE publishes its own result, including unknown if
unavailable. A TZ edit does not create a new mesh/source epoch or precision fit.

T096 and the W12/N30 ports apply the received offset to digital/analog clocks
and their supported savers. Chicago `(2,-300)` displays CDT; `(2,-360)` displays
CST. Other supported regions use their declared label or `LOC`; unknown/expired
metadata returns to UTC. Labels are presentation only. LoRa/RC9 fallback does
not borrow BLE mesh TZ.

Receivers change offset on fresh reception, including at a DST boundary. There
is no simultaneous scheduled display transition. Repeated/skipped local hours
never replace UTC event identity. Every participating T096 needs .209 or newer
for coarse updates after timing synchronization; a GM update alone cannot add
that receive lifecycle to older receivers.

## Ordering and presence leases

An RC learns the pair only from its selected authenticated parent and forwards
it unchanged in coarse capability/admission records. Direct updates require
current admitted root/boot/epoch scope, increasing GM sequence and the original
coarse replay envelope. Initial JOIN metadata cannot overwrite a later ordered
ANN. For a relay path, a higher existing admission challenge permits a changed
pair; an equal challenge renews only an identical pair. Older or equal-token
conflicting updates are refused. Fresh nonce-bound responses can bootstrap or
renew that same metadata scope.

**The lease is three nominal parent advertising periods:** 750 ms for the
250 ms GM cadence, 300 ms for the 100 ms RC cadence. Original monotonic receipt
time fixes the deadline; delayed processing consumes it and polling cannot
renew it. Matching authenticated parent advertisements, including periodic
presence without usable corrections, renew this metadata lease independently
of lock, precision-map expiry, source permission and UTC uncertainty.

Each relay renews its own parent-presence lease and advertises the same pair to
children; there is no end-to-end precision deadline for TZ. On expiry, live TZ
is unknown while the last accepted pair remains available for matching-presence
renewal. An explicit unknown update clears that pair; later presence cannot
resurrect it. Parent/path replacement fences the old context. Foreign/invalid
traffic cannot renew or replace it.

Receivers must keep the selected coarse receive path active after timing lock.
T096 .209 retains the existing passive scan with its original interval/window
and resource guards. Precision presence can renew a cached pair but contains
no changed pair. TZ availability grants neither a timing map nor GM authority.

## Wire compatibility and UTC boundaries

The selected coarse revisions are **GM7v4 (`G7V4`), JOIN5 and RC control/capability8**.
The [contract](../protocol/time-zone-v1/README.md) records exact lengths, offsets
and MAC domains. Current [GM](../firmware/t096/protocol/gm7_codec.c),
[JOIN](../firmware/t096/protocol/group_join.c) and
[RC](../firmware/t096/protocol/rc_dynamic.c) codecs require the selected revision;
matching precision bytes cannot admit an incompatible coarse source. No automatic
old-format fallback, dual broadcast or version negotiation is defined.

Precision G1/GR3/RC7 payloads/authentication, clock discipline, PPS, USB UTC,
chrony inputs, source lifetimes, replay comparisons and mesh birth retain their
existing coordinates. `utc_offset_ns` maps a clock coordinate to UTC and must
not carry civil-time offsets. Missing/invalid TZ affects metadata only.

Use matching coarse revisions along a selected path. Other targets require
their own declared port; do not infer compatibility from shared precision
layouts. Frozen contracts/vectors remain unchanged. A firmware rollback needs
its own reviewed compatible cohort and hardware scope.

## Verification and limits

Use the [automatic-TZ checks/results](t096/auto-time-zone-plan.md),
[receiver port checks](esp32-time-zones.md) and
[public database reproduction](../tools/time_zones/README.md). Offline coverage
includes signed offsets, unknown/future IDs, malformed records, ordered changes,
withdrawal, lease boundaries, date rollover and calendar transitions. Source and
precision independence remain separate assertions from display correctness.

The original [manual-wire test plan](t096/time-zone-test-plan.md) and all failed
windows retain their dated scope. The .209 follow-up does not close physical
DST, all GM/topology replacements, reboot, extended renewal or calibrated
UTC/output gates. Passing codec/calendar tests does not establish RF delivery
or simultaneous display changes.
