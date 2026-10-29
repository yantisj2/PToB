# PToB time zone wire plan

Status: selected design direction, 27 October 2026, revised 29 October for a
breaking T096-first introduction. The 29 October lease decision below supersedes
earlier proposals to bind TZ to precision-source expiry. Local T096 .205 images
are installed on the four-device cohort, including the local clock display.
Direct-RC verification passed; full hardware certification remains pending the
[source and availability gates](t096/time-zone-test-plan.md#local-205-results-29-october-2026).
Copyright (c) 2026 Jonathan Yantis. MIT.

T096 source 0.15.206 implements [GM automatic zone/DST selection](t096/auto-time-zone-plan.md)
and scoped dynamic receiver updates using the same four bytes. The .205
observations above remain historical; .206 hardware acceptance is separate.

UTC remains PToB's primary time base. Add four authenticated bytes to coarse UTC
publications so receivers can display local civil time and relays can carry the
selected source's time zone. Begin with manual GM configuration. The proposed
[compact regional database](time-zone-database.md) is a later GM feature using
the same fields.

The [T096 test plan](t096/time-zone-test-plan.md) starts with manual CDT on
TC-01, TC-02, TC-03 and TC-04, and gates other targets on GM/RC wire certification.
The operator selected TC-02/03 for the GM rotations; results remain separate
from this design.

## Four byte extension

All integers are little endian. Offsets below are relative to the new extension,
not assigned offsets in any existing packet.

| Offset | Bytes | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 2 | `tz_id`, unsigned | Stable PToB identifier mapped to an IANA zone name |
| 2 | 2 | `tz_offset_min`, signed | Current effective offset in whole minutes east of UTC |

The display conversion is `local = UTC + tz_offset_min * 60 seconds`, with
checked signed arithmetic. Negative offsets are west of UTC. Support modern
offsets from -840 through +840 minutes, including fractional hours. Reserve
`INT16_MIN` (-32768) for unavailable offset; the canonical unknown pair is
`(0, INT16_MIN)`.

Start the append-only PToB identifier registry with these assignments:

| PToB ID | Zone |
| ---: | --- |
| 0 | Unknown |
| 1 | UTC, mapped to `Etc/UTC` |
| 2 | `America/Chicago` |

These numbers are PToB assignments, not IANA-assigned numeric IDs. Never derive
them from a database row number, reorder them during regeneration or reuse a
retired value. Future database versions may share storage between zones without
merging their public IDs.

The zone identifies a civil-time region, not a seasonal abbreviation. Chicago
retains ID 2 through CDT/CST changes. Receivers use the transmitted effective
offset; they need neither a timezone database nor DST calculations. A small
display table may label `(2, -300)` as CDT and `(2, -360)` as CST. An unfamiliar
nonzero ID with a valid offset remains usable as `UTC+hh:mm` or `UTC-hh:mm` and
must be relayed unchanged. Abbreviations are presentation only. IANA's
[North American source](https://data.iana.org/time-zones/tzdb/northamerica)
defines `America/Chicago` and its seasonal offsets.

On T096 page 1, replace the UTC label with the three-letter abbreviation and
apply the transmitted offset to both the digital clock and analog dial. Its
analog screensaver uses the same local time and label. Unknown or expired TZ
returns both faces to UTC. For an unfamiliar valid region/offset, use `LOC` in
the narrow label slot rather than inventing an abbreviation. This is display
formatting only; stored time, status UTC fields and USB output remain UTC.
LoRa fallback does not borrow the BLE mesh's TZ metadata.

ID 1 requires offset 0. Missing fields, ID 0, the unavailable sentinel, or an
invalid metadata combination produce TZ UNKNOWN and a UTC display. Unknown
metadata must not invalidate otherwise valid UTC or grant new time permission.
Ordinary framing, authentication and timing validity checks still apply.

No DST bit, zone-name string, transition schedule, database revision or new
timezone sequence counter is carried in this first extension.

## GM configuration and RC forwarding

The serving GM supplies one atomic `(tz_id, tz_offset_min)` snapshot with its
coarse UTC publication. Configure both fields manually in the first phase,
consistently across potential GMs. A timezone edit changes display metadata;
it does not create a new mesh epoch, source generation or precision fit.

An RC learns the atomic pair from its selected authenticated mesh parent and
forwards it unchanged. The first manual .205 phase cached one fixed pair.
From .206, fresh ordered selected-source metadata can change or withdraw it,
including at DST and root replacement. The [wire contract](../protocol/time-zone-v1/README.md)
specifies scope and ordering. Precision-map expiry and loss of lock alone do
not clear TZ; an RC never substitutes a local CDT default. Old-context, older
or same-token conflicting updates cannot replace the selected source's pair.

**The TZ lease is three times the advertising period.** The current coarse
cadences are 250 ms for GM announcements and 100 ms for RC discovery metadata,
giving 750 ms and 300 ms leases respectively. An accepted coarse publication
sets the pair. Existing authenticated advertisements from the selected parent
renew its presence lease, including periodic advertisements with no usable
precision correction. No new packet stream or scan schedule is needed.

This lease uses local monotonic receipt time. It is independent of precision
sample age, clock fits, source permissions, map expiry and UTC uncertainty.
Each relay renews its own coarse lease from its parent and advertises the same
pair to children. There is no end-to-end precision deadline for TZ and no new
TZ sequence counter. A local poll cannot renew the lease. After three missed
advertising periods, live TZ is unknown and presentation falls back to UTC;
the last accepted known pair remains cached for matching advertisements to
renew. Explicit unknown clears it, and parent/path replacement fences the old
context; ordinary presence cannot resurrect a withdrawn pair.

Unavailable or invalid TZ affects metadata only. It neither invalidates UTC nor
grants timing authority. Existing framing, authentication and path selection
checks still apply. New runtimes reject an old coarse revision; a valid new
packet carrying unknown TZ is a different case.

## UTC and compatibility boundaries

Timezone offsets apply only when formatting local civil time. Precision
timestamps, clock discipline, PPS, USB timing, chrony inputs, replay comparisons,
source lifetimes and mesh birth remain in their existing UTC or clock coordinate
systems. In particular, existing `utc_offset_ns` fields map clock coordinates
to UTC; they must not be repurposed for timezone offsets.

Carry the extension inside the authenticated coarse-packet body with an explicit
new revision and corresponding length/authentication definition. Keep the
existing precision timing layouts unchanged. The current
[JOIN codec](../firmware/t096/protocol/group_join.c) and
[GM7 receiver codec](../firmware/ss3/main/gm7/gm7_codec.c) enforce specific
versions and lengths; the [G1 precision codec](../firmware/t096/protocol/group_precision.c)
also rejects nonzero reserved bytes. Appending bytes or filling reserved fields
is not automatically backward compatible. Do not revise frozen historical
contracts or vectors in place.

The 29 October decision is a breaking introduction on T096 GM and RC profiles.
The [versioned coarse-wire contract](../protocol/time-zone-v1/README.md) records
the candidate lengths, offsets and authentication domains. The candidate runtime transmits and accepts only the new coarse
revision on affected paths, without old-format fallback, dual broadcasts or
version negotiation. Retain historical codecs/vectors for explicit offline
checks; they must not become an automatic live downgrade path. Existing
precision layouts remain unchanged, but matching precision bytes alone cannot
admit an old-format source through the new coarse admission path.

Isolate the new-wire cohort from old-wire participants during testing. A complete
cohort rollback restores the previous wire; selectively downgrading one serving
participant is not a compatibility strategy. Other targets remain on their
current software until the T096 GM/RC gates pass and their ports are reviewed.
This supersedes the earlier proposal for receivers accepting both coarse
revisions during migration.

## Initial Chicago configuration

| Configuration | `tz_id` | `tz_offset_min` | Display |
| --- | ---: | ---: | --- |
| Daylight time | 2 | -300 | CDT, UTC-05:00 |
| Standard time | 2 | -360 | CST, UTC-06:00 |

The 2026 fall change is Sunday, 1 November, at 02:00 CDT, equivalent to
07:00 UTC: local 01:59:59 CDT becomes 01:00:00 CST. The date and repeated local
hour follow [NIST's 2026 DST rules](https://www.nist.gov/pml/time-and-frequency-division/popular-links/daylight-saving-time-dst).
The UTC conversion uses Chicago's -300 minute pre-transition offset.

The first .205 cohort stays explicitly set to CDT (`2, -300`), including across
a calendar boundary until the operator changes it. In .206, automatic and
manual-zone modes choose the current offset at publication time. Manual-offset
mode remains fixed. Propagation is not a simultaneous scheduled display change;
advance transition announcements would need a separate protocol design. Local
repeated hours never replace UTC event identity. Update receivers for the
dynamic policy before relying on automatic seasonal changes.

## Implementation and verification sequence

1. Freeze the four-byte coarse layouts, revisions and authentication domains.
   Keep the three-advertising-period TZ lease separate from precision timing.
   Publish synthetic vectors separately from existing contracts.
2. Add manual GM configuration, atomic source snapshots, receiver caching and
   unchanged RC forwarding. Preserve all timing permissions and scheduling.
3. Verify old-revision rejection, new-format unknown TZ, unknown future IDs,
   invalid metadata, signed offsets, fractional hours and date rollover. Exercise
   delayed coarse observations, lease boundaries, relay forwarding and GM replacement.
4. Verify the CDT/CST repeated hour and spring gap while UTC, source epochs,
   precision corrections and USB timestamps remain continuous and unchanged by
   the timezone edit.
5. Develop the GM database and rough position selection separately. Enabling
   either later does not enlarge this extension or move timezone policy to RCs.

Offline codec/model checks and any later hardware checks retain distinct status.
Candidate implementation, installation and certification retain separate status.
