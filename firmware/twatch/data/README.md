# Santa Rosa Beach calendar data

Solar reference location: **30.35° N, 86.23° W**, a fixed representative Gulf-side
Santa Rosa Beach/30A location, not a GNSS position. The calendar's civil zone is
America/Chicago, using the US DST rule effective since 2007. Firmware supports
2007–2099; future legal changes require a firmware update.

Tides use **NOAA 8729511, Destin, East Pass**, a nearby Gulf reference rather than
a measurement at Santa Rosa Beach. NOAA classifies it as a subordinate station
referenced to Pensacola (8729840). Predictions are used as returned by NOAA:
no second application of station offsets, no fabricated twelve-hour cycle, and
no interpolation into a tide-height curve. Events are predicted highs/lows,
not observed water level, surf, storm surge or beach conditions.

## Coverage and encoding

The bundled `tides.inc` covers **2026-01-01 00:00 UTC through 2028-01-01 00:00 UTC,
exclusive**. It contains 1,548 events, occupying 9,288 bytes of event arrays:
32-bit UTC minutes with a high/low bit, plus signed 16-bit millimetres relative
to MLLW. NOAA event timestamps have minute resolution. The API exposes the
coverage end and returns unavailable outside it; it never repeats a prior year.
Near the end, fewer than eight upcoming events may be available.

Sunrise/set use NOAA's general solar equations, with 90.833° zenith and 96° for
civil twilight. They are approximate calendar events, separate from the watch's
precision timing measurements. Independent USNO equinox/solstice and November
fixtures check sunrise, sunset and twilight within three minutes. Local terrain,
weather and refraction can change observed visibility.

## Runtime interface

`main/calendar.hpp` exposes `solar(Date)`, `snapshot(utc_seconds, clock_valid,
estimated)` and `current(Snapshot&)`. The UI worker publishes a fixed-size
snapshot once per UTC minute, or when clock validity/estimated status changes.
Consumers must check `sampled_utc`, validity/coverage, and `clock_estimated`.
Display-only retained time is marked estimated; this calendar grants no radio,
clock discipline, authentication or holdover authority.

A snapshot provides today's sunrise/set, civil dawn/dusk, next sunrise/set,
Central offset, eight upcoming tide events, and a continuous 0–1000 daylight
value. `brightness(snapshot, night, day, fallback)` maps twilight smoothly to
future brightness settings; unknown time uses the fallback. It does **not**
change current manual brightness or the USB keep-awake policy. All computation
is local, without Wi-Fi, GNSS, heap allocation, network requests or added radio
traffic. The small daily solar calculation runs only on snapshot refresh.

## Reproduce and refresh

```sh
# Explicit online download on the development host, not the watch.
python3 firmware/twatch/tools/calendar_data.py --fetch \
  --cache artifacts/twatch-calendar --years 2026 2027
# Exact offline regeneration from saved responses.
python3 firmware/twatch/tools/calendar_data.py \
  --cache artifacts/twatch-calendar --years 2026 2027
python3 firmware/twatch/tools/calendar_tests.py --out artifacts/watch-calendar-check
```

The importer rejects invalid kinds, out-of-range heights, duplicate/unordered
times, incomplete annual endpoints and gaps over 48 hours. Review the manifest,
station, requested coverage and tests when refreshing. Source responses stay in
ignored artifacts; selected runtime data and source URL/checksum provenance are
tracked. Predictions can be revised by NOAA; regenerate before later releases.

## Terms and sources

- [NOAA CO-OPS API](https://api.tidesandcurrents.noaa.gov/api/prod/): `predictions`,
  `interval=hilo`, `time_zone=gmt`, `units=metric`, `datum=MLLW`.
- [Destin/East Pass station](https://tidesandcurrents.noaa.gov/noaatidepredictions.html?id=8729511&legacy=1).
- [NOAA solar equations](https://gml.noaa.gov/grad/solcalc/solareqns.PDF).
- [USNO astronomical API](https://aa.usno.navy.mil/data/api); selected independent
  reference queries are recorded in `solar_reference.json`.

NOAA federal data remain public domain under the [NOAA NOS terms](https://oceanservice.noaa.gov/about/faq.html).
USNO federal ephemeris facts are public-domain government data. No exclusive
rights are asserted over either source. Original selection, encoding and dataset
provenance © 2026 Jonathan Yantis, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
Original software and ordinary documentation are MIT. See the repository's
LICENSING.md; these terms do not imply NOAA or USNO endorsement.

Solar-face astronomy uses selected SunCalc v1.9.0 equations, ported locally with
its BSD-2-Clause notice preserved in `vendor/suncalc/`. It supplies geometric
sun/moon elevation and north-clockwise azimuth, solar hour angle, approximate
lunar phase (0=new, 0.5=full) and illuminated fraction. It is calculated once per
UTC minute from the watch's displayed clock at the fixed Santa Rosa Beach
coordinates. Display holdover is explicitly marked estimated. There is no
GNSS activation, network lookup, compass heading or claimed topocentric precision.

The solar calendar now includes nautical twilight (Sun 12° below the horizon)
and astronomical twilight (18°), in addition to civil twilight (6°). All event
values remain UTC seconds; their local calendar date is independent of mesh TZ.
The Solar face's local clock must use the same Central offset as its location.

The tide pointer is an approximate cosine progression between consecutive,
alternating NOAA high/low predictions, not an observed water level or a harmonic
height reconstruction. It is unavailable when either bounding event is missing,
when event types do not alternate, or outside the table's coverage. Endpoint
transitions are continuous. The nearby station remains Destin/East Pass, not a
Santa Rosa Beach gauge.

`calendar_tests.py` checks the astronomy port against pinned upstream values and
eight independent [USNO primary lunar phases](https://aa.usno.navy.mil/api/moon/phases/date?date=2026-11-01&nump=8),
plus month-spanning twilight order, bounded positions and tide-edge continuity.
The selected phase comparisons are validation fixtures, not a full ephemeris
accuracy guarantee. Their sources and licensing are in `astronomy_reference.json`.
