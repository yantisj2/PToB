# T096 GM automatic time zones

Source 0.15.208, 29 October 2026. Software implemented; hardware acceptance is
recorded separately below. Copyright (c) 2026 Jonathan Yantis. MIT.

The GM selects its timezone from its own GNSS position and computes the current
UTC offset from pinned IANA calendars. Receivers inherit the authenticated
four-byte pair and display local time. UTC calculations, precision payloads,
mesh/source epochs and USB UTC remain unchanged.

## Build modes

Normal GM7/RC builds default to automatic selection. The same image can switch
between GM and RC; its local selection applies only when it serves as GM.

| Mode | Builder options | Behavior |
| --- | --- | --- |
| Automatic | `--tz-mode auto` (default) | Own GNSS position selects a supported region; its calendar supplies the offset |
| Manual zone | `--tz-mode manual-zone --tz-id 2` | Chicago calendar follows CDT/CST without position lookup being required |
| Manual offset | `--tz-mode manual-offset --tz-id 2 --tz-offset-min -300` | Fixed CDT until the operator changes configuration |

The [append-only ID registry](../../tools/time_zones/zones.json) retains ID 1 for
UTC and ID 2 for Chicago. Calendar modes need usable UTC inside the database
window. Manual-offset mode validates the pair and can operate outside it.
Bad settings fail the build. DC, PC and historical GM regression builds reject
these options. Runtime persistent configuration remains separate work.

The receipt records the mode, manual values and database identity. Generated
build identity binds that configuration. Existing manual .205 images do not
gain DST behavior through a GM update alone: update every participating T096
receiver/relay for dynamic changes before relying on automatic offsets.

## Location and hold behavior

- Only newly accepted, checksum-valid local RMC navigation epochs supply
  position. Coordinates use checked signed integers. Coordinate failures do
  not change otherwise accepted RMC time; rejected/duplicate RMC does not
  refresh a fix. Position validity grants no timing authority.
- A position may be at most 30 seconds old. Lookup runs at startup and at most
  once per minute. Three distinct matching samples establish or replace a
  region, normally about 120 seconds after usable UTC/position acquisition.
- Invalid, stale, ambiguous or unsupported samples reset a pending candidate.
  A previously selected zone remains usable for at most ten minutes from the
  last confirming fix, with an `AUTO HOLD` diagnostic. Other candidates do not
  extend that deadline. Afterward the GM publishes unknown and the clock
  presentation falls back to UTC.
- The selected zone's offset is evaluated at each new coarse packet encoding.
  A DST transition needs no new position fix, including during bounded hold.
  Receivers change on fresh reception; there is no simultaneous scheduled
  transition announcement.
- Reboot and GNSS-owner stop/init clear learned selection. A standby GM may
  prepare its own zone from local GNSS, while its follower display uses the
  serving GM's metadata. On takeover it publishes its own result, including
  unknown if not ready. No peer position or old saved location is substituted.

The `clock` reply exposes local position and age; this is not broadcast over
BLE. `status.tz` reports the received/effective pair, mode (0 manual offset,
1 manual zone, 2 auto), locally selected ID, ages, hold state, reason, database
version and expiry. These are display/configuration diagnostics.

## Calendars and rough geography

The [selected image](../../firmware/t096/data/README.md) uses IANA **2026f**,
Natural Earth admin-0/admin-1 **5.1.1** and lakes **5.0.0**. Its 5,738 bytes cover
`2026-01-01 <= UTC < 2032-01-01`. Database expiry disables automatic and manual-zone
offsets; it never freezes the last seasonal offset indefinitely.

The original [regional proposal](../time-zone-database.md) defines the countries,
islands and omissions. The first image uses conservative whole-region mappings
and documented interior cuts. County/enclave and unresolved municipal boundaries
stay ambiguous. All 97 selected IANA principal-location samples are retained:
38 resolve correctly, 59 are ambiguous, zero wrong and zero unsupported. This
is a coverage population, not a representative accuracy estimate. A coarse
coastal cell can include sea and supplies no land-membership guarantee.

Use manual-zone mode where geographic coverage is unresolved; it still follows
the selected region's DST calendar. [Generator commands, exact input hashes,
cuts and licensing](../../tools/time_zones/README.md) are public and reproducible.
The firmware build itself needs no downloads, geography library or private SDK.

## Dynamic metadata and verification

The [wire contract](../../protocol/time-zone-v1/README.md) retains the existing
lengths, authentication domains and three-advertising-period leases. Fresh
selected-source observations can update or withdraw the atomic pair. Direct
RCs consume ordered GM announcements as well as initial admission metadata.
Relays reuse the existing monotonically increasing admission challenge to
order changed coarse snapshots. Older or same-token conflicting observations
cannot restore a previous offset. Matching advertisements renew presence
independently of precision lock/corrections; explicit unknown cannot resurrect
an older known value. Parent/path replacement fences metadata to the new scope.

Offline checks cover the actual NMEA/time-owner boundary, all selector bounds,
manual overrides, reboot/stop, direct and relay receipt bodies, authentication,
scope/replay, unknown withdrawal and unchanged UTC. The production C reader
checks 217,204 calendar cases and all 97 principal-location classifications;
generation separately compares daily offsets and every transition boundary
against the pinned compiled IANA source. Chicago's 2026 fall transition is
07:00 UTC on 1 November: `(2,-300)` becomes `(2,-360)` without changing UTC.

Native inspection caps generated data at 16 KiB and the complete added mutable
state at 256 bytes against the same .205 configuration, including position,
queues and copied snapshots. Existing controller ABI, radio/USB reserves,
stack allocations and the absolute 1,024-byte static margin remain enforced.
Host/native results are separate from hardware operation and UTC metrology.

Frozen .207 commit `69f1b978cd3b26ce056b8922c35eb198d1c9e067` passed the complete
production host suite, eight provisioning checks and nine console checks.
An independent pinned-source regeneration reproduced the header, manifest and
coverage file byte-for-byte. The three own-device native images passed inspection:
224 bytes of total added mutable state, 4,148 bytes of static margin and the
same 5,738-byte read-only dataset. These figures include the GNSS position and
copied queue/snapshot state.

The earlier [.205 hardware results](time-zone-test-plan.md#local-205-results-29-october-2026)
remain historical: direct-RC display passed, relay availability failed and other
successor gates were incomplete. The operator authorized automatic-TZ testing on
TC-01, TC-02 and TC-04; TC-03 is excluded. All three .206 application readbacks
and fresh boots passed. Its first direct-topology acquisition failed within
the original 240-second budget: the local position was in unresolved western
Florida, and auto-TZ remained unknown. Source qualification and receiver timing
availability are recorded separately from this geographic failure. The .207
mapping adds a conservative western Panhandle cut, with public regression
samples. All three .207 readbacks passed. Its direct acquisition also failed
within 240 seconds: both receivers accepted `(2,-300)`, but the serving GM's
display reported unknown because it consulted its absent follower clock. The
.208 display uses the same own-UTC snapshot as admission; an actual-body
regression reproduces the .207 failure and covers DST and source withdrawal.
New hardware results are pending.
