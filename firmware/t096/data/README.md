# T096 regional time zone image

Dataset copyright (c) 2026 Jonathan Yantis, **CC BY 4.0**.
This ordinary guide uses MIT. [Terms, public inputs and reproducible generation](../../../tools/time_zones/README.md).

`time_zone_data.h` contains **5,738 bytes** of read-only packed data: 98 public
zone records, 20 shared offset calendars, 135 offset transitions, names and two
cropped geographic grids. Its SHA-256 as packed bytes is
`9ab1a6c7e0ebddaf1319e1ee392d3b46c9cfc0fb7464858d71f542893215e811`.
The selected window is `2026-01-01T00:00:00Z <= UTC < 2032-01-01T00:00:00Z`.
Expiry produces unknown; firmware does not extrapolate seasonal rules.

`manifest.json` binds archive pins, generator/mapping/dependency hashes, section
offsets, exact calendars and classification populations. `coverage.json` retains
all 97 IANA principal-location results, including 59 ambiguous cases. Public
zone names sharing a calendar retain distinct IDs. Manual-zone mode supports
every record; geographic lookup resolves only explicitly mapped cells.

The runtime reads little-endian bytes with checked offsets. No native struct
layout, heap, polygon engine, timezone interpreter or network request is needed.
Mutable selector state is separate from this image. The 32-byte header is:

| Byte | Encoding | Field |
| --- | --- | --- |
| 0 | 4 ASCII bytes | `TZ01` |
| 4, 8 | u32 each | Inclusive start and exclusive end, POSIX seconds |
| 12, 14 | u16 each | Zone and calendar counts |
| 16, 18, 20, 22 | u16 each | Zone, calendar, transition and name section offsets |
| 24, 26 | u16 each | Americas and Europe grid offsets |
| 28, 30 | u16 each | Total bytes and format revision 1 |

A six-byte zone record holds public ID, calendar index and relative name offset
(u16 each). A six-byte calendar header holds signed initial minutes, first
transition index and count. Each six-byte transition is u32 POSIX second plus
i16 new minutes. Transitions are strictly ordered and lie inside the window.

Each grid has signed i16 latitude/longitude starts, u8 row/column counts and two
zero bytes. Its `rows+1` u16 offsets are relative to the grid start. Each row
contains `(exclusive_end_column, internal_zone_index)` byte pairs. Index 0 is
unsupported, 255 ambiguous and 1–254 selects a zone record. Lookup floors signed
coordinates and scans at most the grid's column count. Internal indices grant
no public identity and may change on regeneration.
