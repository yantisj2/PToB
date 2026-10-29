# Compact regional time zone database proposal

Original regional design, 27 October 2026. Implemented in T096 source 0.15.206
with the coverage and validation limits in the [current guide](t096/auto-time-zone-plan.md).
Copyright (c) 2026 Jonathan Yantis. MIT.

The [29 October T096 implementation](t096/auto-time-zone-plan.md) follows the
design below, adds dynamic receiver metadata updates and records reproducible
generation, explicit geographic cuts and manual overrides. Hardware acceptance
is separate from this design and the generated database.

Keep regional zone selection and offset calculation on the GM. Generate a small
read-only database on a development host, containing shared UTC offset calendars
and a coarse geographic lookup. RCs continue to consume only the four-byte zone
ID/current-offset pair. Favor compact storage and useful interior coverage over
complete border, historical and territorial accuracy.

## Coverage and deliberate omissions

The initial geographic allowlist is:

| Region | Proposed coverage |
| --- | --- |
| North America | United States, Canada and Mexico; include Alaska, Hawaii, Newfoundland and Yukon |
| Central America | Belize, Guatemala, El Salvador, Honduras, Nicaragua, Costa Rica and Panama |
| Western Europe | UK, Ireland, Portugal, Spain, France, Belgium, Netherlands, Luxembourg, Germany, Switzerland, Austria and Italy, plus Denmark, Norway, Sweden and Iceland |
| Atlantic islands | Azores, Madeira and Canaries as explicit small regions |

Include enclosed microstates through explicit mappings. Greenland, Caribbean
islands, most overseas territories, ocean timezone bands and the Aleutians east
of the antimeridian are outside the first automatic map. Eastern Europe and the
rest of the world are also outside it. These omissions keep the first edition
bounded; they do not describe the full geographic meaning of North America or
Western Europe. Unsupported locations return unknown and retain manual offset
configuration as an option.

Cover common large exceptions in the regional manifest: Arizona versus nearby
US DST zones; Canadian fixed-offset areas and recent rule changes; Mexico's
interior versus northern border zones; Newfoundland's fractional hour; Panama
versus its western neighbors; Portugal versus Spain; and the Atlantic islands.
Do not spend the first edition's budget on US county-level boundaries, the
Navajo/Hopi enclave pattern, small Canadian municipal exceptions or every
Mexican border municipality. Mark unresolved split areas ambiguous instead of
assigning a precise zone ID to a known unresolved case.

## Shared offset calendars

Generate current/future offset changes from a pinned
[IANA tzdb release](https://www.iana.org/time-zones), initially covering
`2026-01-01T00:00:00Z <= UTC < 2032-01-01T00:00:00Z`. This six-year window avoids
shipping historical transitions or a general timezone rule interpreter. Extend
the window with a reviewed database update, rather than extrapolating forever.
The original source candidate was tzdb **2026e**. Execution preparation verified
the newer **2026f** archives and recorded their actual SHA-256 values in the
[generator source lock](../tools/time_zones/sources.lock.json).
Use those pinned bytes from the [release archive](https://data.iana.org/time-zones/releases/)
for the first implementation; re-evaluate later releases explicitly.

Each supported IANA zone keeps a stable public PToB `tz_id`, even when another
zone currently has identical offsets. Deduplicate only the internal calendar
storage. The initial 40–64-record estimate became 98 distinct records in the
[selected ID registry](../tools/time_zones/zones.json), including UTC and aliases.
Adding a needed ID is preferable to incorrectly
collapsing two civil regions into one public ID.

| Stored item | Proposed packed representation |
| --- | --- |
| Zone record | `u16 tz_id`, `u16 calendar_index`, `u16 name_offset` |
| Calendar header | `i16 initial_offset_min`, `u16 first_transition`, `u8 count` |
| Transition | `u32 utc_posix_second`, `i16 new_offset_min` |
| Image header | Format revision, validity start/end, counts, section lengths and source version |
| Optional strings | Deduplicated IANA names and a small display-label table |

These are proposed byte encodings, not native C struct sizes. Use explicit
packing and bounds checks. Six-year calendars generally need only a few entries
per year; fixed-offset zones have none. Intern calendars only when their initial
offset and every transition throughout the whole validity window match.

At UTC `t`, select the initial offset or the last transition with
`transition.utc <= t`. Treat transition seconds as unsigned and convert against
PToB's 64-bit time representation with checked arithmetic. No UTC-to-local
reverse conversion, `isdst` interpretation, leap-second table or past civil-time
lookup is needed. Labels may differ between zones sharing an offset calendar;
keep them separate or use numeric UTC-offset labels.

Shared generated calendars are preferable to assuming all of Canada follows US
DST or all nearby European locations have the same base offset. IANA updates
offsets and rules as jurisdictions change them; its
[2026e release notes](https://data.iana.org/time-zones/tzdb/NEWS) already report a
forthcoming Manitoba change. Pinning the source makes the selected policy
inspectable, but cannot predict later legislation. Regenerate before deployment
when a relevant rule changes. An expired database disables automatic offset
selection; it must not freeze the last seasonal offset indefinitely.

## Coarse geographic map

Use one-degree latitude/longitude cells, compressed into horizontal runs. This
keeps runtime lookup to integer cell selection and a short bounded row scan.
There is no runtime polygon engine, reverse geocoder, nearest-city search or
network request. Longitude alone is insufficient for political time zones.

Two cropped grids contain 13,344 cells before compression:

| Grid | Latitude interval | Longitude interval | Cells |
| --- | --- | --- | ---: |
| Americas | `[5, 85)` degrees north | `[-180, -50)` degrees east | 80 x 130 = 10,400 |
| Europe and Atlantic islands | `[26, 72)` degrees north | `[-32, 32)` degrees east | 46 x 64 = 2,944 |

The grids bound lookup storage, not supported countries. Keep only the allowlist
inside them. Use a one-byte internal zone index per cell: 0 unsupported, 255
ambiguous, and 1-254 indexing records with full 16-bit public IDs. Internal
indices may change between database builds; wire IDs must not.

Represent each row as `(exclusive_end_column, zone_index)` byte pairs plus a
16-bit row offset table. Lookup uses floor division for negative coordinates,
half-open cell boundaries and normalized longitude in `[-180, 180)`. Reject
nonfinite or invalid GNSS coordinates before conversion. A scan is bounded by
the grid's column count, even in a poorly compressed row.

Generate the map from pinned public-domain Natural Earth
[country polygons](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-0-countries/)
and [state/province polygons](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-1-states-provinces/),
with an authored region-to-zone manifest and a small set of documented split-region
cuts. Natural Earth supplies administrative geography, not authoritative TZ
boundaries; the mapping and any cuts need their own provenance and review.

During generation, intersect cells with the selected simplified regions. Assign
a zone only when supported land in the cell maps to one resolved zone. Mark
cells touching different zones, unsupported land or an unresolved split as
ambiguous. Cells without selected land are unsupported. Coast cells may include
nearby sea; the coarse detector cannot certify land membership. Do not select a
majority zone across a known zone boundary merely to increase coverage.

One-degree cells and simplified input boundaries can leave locations tens of
kilometres from a border unresolved or misclassified. This is an accepted
approximation, not a maximum error bound. Small countries or narrow land areas
can have little automatic coverage; preserve manual selection rather than
adding an increasingly detailed map. Return a representative region ID only
for the manifest's resolved geographic scope, not whichever city is nearest.

## GM selection policy

Keep three distinct proposed modes:

| Mode | Zone selection | Offset selection |
| --- | --- | --- |
| Manual offset | Operator supplies ID and offset | Operator supplies current offset; first wire phase |
| Manual zone | Operator pins a supported ID | Database follows that zone's offset calendar |
| Automatic zone | Rough lookup from the GM's own valid GNSS position | Database follows the selected zone's calendar |

Manual configuration wins. An RC, a standby merely hearing another position,
or a GNSS diagnostic cache must not rewrite the selected serving GM's setting.
Potential GMs should share a manual group zone for a stationary installation;
automatic zones on different GMs can legitimately disagree if geographically
separated. A later serving-GM change carries the new selected source's metadata
through the ordinary wire rules, without pretending that position is consensus.

The first manual implementation deliberately caches an immutable mesh pair.
Automatic DST and geographic changes therefore require the explicit
[dynamic receiver policy](t096/auto-time-zone-plan.md#dynamic-metadata-and-verification),
implemented in .206. The four-byte layout is sufficient; existing immutable-pair
.205 receiver behavior is not.

For automatic zone selection, begin with these tunable bounds:

1. Require the GM's own valid position, at most 30 seconds old, and valid UTC
   within the database window. Position validity never grants timing authority.
2. Look up at startup, then no more than once per minute. Require three
   consecutive usable fixes spanning at least 60 seconds to select or replace
   an automatic zone. All must yield the same resolved zone. Reset the pending
   candidate on a different, invalid or ambiguous result.
3. Treat ambiguous or unsupported cells as no new candidate. Retain the prior
   zone for at most ten minutes from the last fix confirming that selected zone,
   with a local `AUTO HOLD` indication. Ambiguous samples, stale positions and
   pending candidates for another zone do not extend that bound. Afterward
   publish TZ UNKNOWN unless a manual override applies.
4. A stable selected zone does not require a new fix at every DST change. Use
   the current UTC calendar to update its offset, including during the bounded
   position hold. At database expiry, publish unknown in either database mode.
5. Recompute the effective offset before each coarse publication, or through
   a cached next-transition deadline checked there. Broadcast the current
   four-byte snapshot. RCs change on fresh reception, not from a local schedule.

On a reboot, reacquire automatic selection; do not turn an old saved position
into a fresh fix. The initial proposal uses no persistent learned location.
Expose mode, selected zone, position age, database version/expiry and hold state
in local diagnostics. Confidence and database details need no new wire fields.

## Storage budget and reproducibility

The initial design targets at most **16 KiB of generated flash data** and
**256 bytes of additional mutable runtime state**, excluding the existing GNSS
state and code. These are budgets, not measured sizes:

| Component | Target |
| --- | ---: |
| Compressed geographic runs and row indices | 8 KiB |
| Zone records, shared calendars and transitions | 6 KiB |
| Image header, names and small label table | 2 KiB |

Lookup operates directly on read-only flash with no decompressed grid, heap or
dynamic TZ library. Fit the measured code/data cost into each target's existing
resource limits before enabling it; no timing reserve may be assumed available.
If generation exceeds budget, trim optional labels or automatic coverage and
report the resulting omissions. Never merge different calendars, renumber wire
IDs or silently shorten the advertised validity window to make a build fit.

Use public, pinned host dependencies. Record generator revision, tzdb release,
geographic source release and hashes, selected IDs, mapping edits, validity
window, byte counts and output SHA-256. A deterministic generator should emit a
small checked-in runtime image/header and a reviewable manifest. Builds consume
that selected image without downloading maps or needing a private checkout.
The [generator commands and pinned host dependencies](../tools/time_zones/README.md)
now reproduce the checked-in [runtime dataset](../firmware/t096/data/README.md).

Preserve the [IANA data terms](https://data.iana.org/time-zones/tzdb/LICENSE) and
[Natural Earth terms](https://www.naturalearthdata.com/about/terms-of-use/), both
public domain for the selected data. Record the actual generated dataset terms
at import; original PToB dataset contributions use CC BY 4.0 and generator code
uses MIT under [repository licensing](../LICENSING.md). The original proposal
imported no geographic dataset. The .206 generated image records its actual
terms and public input hashes; no raw source archive is tracked. No license is
assumed for a future alternate boundary source.

## Offline delivery sequence

1. Implement and verify the manual four-byte wire phase first.
2. Freeze the public ID allowlist and pinned source manifest. Generate calendars
   alone; compare every transition just before, at and after its boundary, and
   daily offsets throughout the window, against that exact tzdb release.
3. Enable manual-zone calendar selection before position detection. Cover
   Chicago's fall/spring changes, Newfoundland, fixed-offset areas, European
   changes, recent Canadian changes and database expiry. Separate numeric offset
   checks from abbreviation behavior, especially for Ireland.
4. Generate the coarse map and publish all source cuts/omissions. Review sample
   cities and borders across each included country. Report correct, ambiguous,
   wrong and unsupported outcomes separately; report offset agreement and
   regional-ID agreement separately. Do not hide a wrong result because two
   zones happen to share today's offset.
5. Test ambiguity, stale fixes, motion across borders, reboot, manual override,
   source changes, calendar expiry and unchanged RC forwarding. Measure actual
   flash, RAM and lookup bounds before native integration.

Automatic detection remains an approximate display convenience. Its errors must
not alter UTC, clock quality, election, source permissions or precision output.
