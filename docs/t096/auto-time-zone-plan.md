# T096 GM automatic time zones

Source 0.15.209, 29 October 2026. Software implemented; hardware acceptance is
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
receiver/relay to .209 or newer before relying on automatic offsets. Development
images .206–.208 do not receive later coarse changes while their normal scan is
stopped after timing synchronization.

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

Normal T096 receivers retain the existing passive scan after synchronization
to receive changed GM ANN and relay capability metadata. The configured
synchronized interval/window remains 20/5 ms, with existing GATT, acquisition,
closure and slot-scan guards. PC and historical non-TZ policies remain unchanged.
This receive lifecycle change is necessary for updates after initial admission;
G1 presence alone contains no new pair.

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

Frozen .209 commit `b5732ed7e5ec988138f341ad485899ae53c1d074` passed the complete
production host suite, eight provisioning checks and nine console checks.
An independent pinned-source regeneration reproduced the header, manifest and
coverage file byte-for-byte. The three own-device native images passed inspection:
224 bytes of total added mutable state, 4,148 bytes of static margin and the
same 5,738-byte read-only dataset. These figures include the GNSS position and
copied queue/snapshot state. The .209 suite also exercises the actual serving-GM
display and synchronized receiver scan/ingress bodies; the corresponding older
implementations fail the regression assertions.

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
All three .208 readbacks passed. Its 240-second direct acquisition failed with
the GM reporting known TZ and both timing-locked receivers reporting unknown:
initial admission preceded selection, and the normal RC policy had stopped
coarse scanning. The .209 actual-ingress/owner regression reproduces that quiet
scan failure and retains listening for later updates.

## Local .209 results — 29 October 2026

All three application updates passed complete new-application readback,
protected-prefix and final-sector-tail comparison, followed by fresh source/boot
checks. TC-03 was not operated and retains its older image; it needs a compatible
update before depending on automatic DST propagation.

The TC-02 GM acquisition exhausted the original 240 seconds with zero reported
satellites and no usable own GNSS fix. The GM did not serve, and its automatic
offset stayed unknown. Testing then used TC-01 as GM, with TC-02 and TC-04 as
receivers. The direct route acquired the correct Chicago pair `(2,-300)` in
226.75 seconds, inside the same 240-second limit. Its complete 600-second
observation failed the unchanged 99% availability gate:

| Direct route sample population | TC-01 GM | TC-02 RC | TC-04 RC |
| --- | ---: | ---: | ---: |
| Known correct pair / status replies | 77/117 | 76/117 | 76/117 |
| Explicit unknown / status replies | 40/117 | 41/117 | 41/117 |
| Correct local conversions / known replies | 77/77 | 76/76 | 76/76 |
| Serving / network replies | 76/116 | — | — |
| Correct selected route / route replies | — | 75/116 | 75/116 |
| Precision lock / precision replies | — | 74/116 | 75/116 |
| Valid precision map / precision replies | — | 74/116 | 75/116 |

No incorrect accepted pair was observed. All 116 GM position/calendar checks
resolved to Chicago; position validity did not confer timing authority. The GM's
qualified timing source dropped during the window, and receiver metadata became
unknown during unavailable intervals. Both receivers changed bindings twice.
The samples include every captured interval, rather than a selected stable tail.
All console captures closed without a timeout or refused frame. Source thresholds
and failed windows are retained; reception could not be adjusted during the run.

The finite TC-04 vendor-USB probe completed all 600 requests with zero request
failures or malformed records. All 600 precision-source records were unavailable,
so both its availability gate and live UTC-coordinate check failed. A successful
USB transfer does not establish a usable clock. The probe started no daemon and
changed no device or host clock.

The forced relay route TC-01 GM → TC-02 RC → TC-04 RC acquired `(2,-300)` in
101.46 seconds, including receiver precision lock and valid maps. TC-02 inherited
the pair while its own geographic selection was unknown; a receiver's local
position is not substituted for the GM's metadata.

Its full 600-second observation also failed the unchanged 99% gate. Each unit
reported 26/117 correct known pairs and 91/117 unknown pairs, with all 26 known
local conversions correct. The GM served in 25/116 network replies. Each receiver
had the intended route, precision lock and valid map in 25/116 respective replies;
neither changed bindings during the window. The GM had valid position in 25/116
clock replies and invalid position in 91/116. All valid position/calendar checks
resolved to Chicago. No incorrect accepted pair, console timeout or refused frame
was observed. Source loss and every unknown interval remain in the population.

Final inspection confirmed the volatile layout TC-01 GM, TC-02/TC-04 RC forced
directly to TC-01, with LoRa off. All console handles and unit locks were released.
No extra acquisition window was selected after these failures. Build-time boot
profiles are unchanged. Live availability remains failed, independently of the
passing installation and resource-release checks.

Chicago's forthcoming physical DST boundary has not been observed. Calendar and
production-owner/packet regressions cover that boundary and the complete database
window; they do not replace live transition, sustained availability, GM successor
or independent UTC accuracy qualification.
Raw enrolled-device captures and images remain local private validation material;
the public record contains these aggregates and the reproducible offline tests.
