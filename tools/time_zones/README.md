# Regional time zone data generation

Copyright (c) 2026 Jonathan Yantis. Generator code and this guide: MIT.

`generate.py` builds the [T096 runtime dataset](../../firmware/t096/data/README.md)
from public hash-pinned IANA tzdb 2026f and Natural Earth inputs. Firmware builds
consume the selected checked-in header and need no download or host geospatial
library. [GM modes and runtime limits](../../docs/t096/auto-time-zone-plan.md).

Use Python 3.9–3.12 and a C compiler with `make` for the pinned host `zic`.
Install the selected packages in a local environment. The first command needs
network access; regeneration can run offline with the verified source cache.

```sh
python3 -m venv artifacts/tz-generator-env
artifacts/tz-generator-env/bin/python -m pip install -r tools/time_zones/requirements.txt
artifacts/tz-generator-env/bin/python tools/time_zones/generate.py \
  --cache artifacts/tz-public-inputs --download \
  --work artifacts/tz-generate-01
```

Use a fresh `--work` directory each time. The generator rejects changed archive
sizes/hashes, incorrect embedded versions, altered package versions, missing
selected geography, overlapping cuts and a database larger than 16 KiB. It builds
the pinned `zic`, expands the complete `[2026, 2032)` window, interns only identical
offset calendars, and checks daily and transition-boundary offsets against those
compiled TZif files. It never uses the OS timezone database.

For an independent regeneration, choose a fresh output directory and compare:

```sh
artifacts/tz-generator-env/bin/python tools/time_zones/generate.py \
  --cache artifacts/tz-public-inputs --work artifacts/tz-generate-02 \
  --out artifacts/tz-regenerated
cmp firmware/t096/data/time_zone_data.h artifacts/tz-regenerated/time_zone_data.h
cmp firmware/t096/data/manifest.json artifacts/tz-regenerated/manifest.json
cmp firmware/t096/data/coverage.json artifacts/tz-regenerated/coverage.json
python3 firmware/t096/tools/auto_time_zone_tests.py --out artifacts/tz-host-01
```

The host runner requires LLVM Clang with ASan/UBSan (use `CC` to select it).
Its production C reader checks all 97 principal-location classifications and
217,204 daily/boundary offset cases. These deterministic checks are separate
from the source generator's exact-tzdb comparisons and from hardware testing.

`zones.json` is the append-only public ID registry, including aliases retained
as distinct names. ID 0 is unknown, 1 UTC and 2 Chicago. Never reorder existing
IDs or derive them from internal compressed indices. The selected 98 records
include manually usable zones whose automatic geographic scope is unresolved
or outside the cropped grids. Public IDs and automatic coverage are distinct.

`regions.json` records every whole-region mapping, interior rectangle and
deliberate omission. Unresolved parts stay ambiguous. The generator subtracts
the pinned Natural Earth lakes layer before classifying land: administrative
water jurisdiction alone must not make a neighboring land zone ambiguous.
Positive-area intersections count; touching only along a polygon/cell edge does
not claim land in a half-open cell. These are coarse cartographic choices,
without an accuracy or maximum border-error guarantee.

The generated `coverage.json` retains all 97 selected IANA `zone.tab` principal
locations: 38 resolve to the expected public ID, 59 are ambiguous, zero are wrong
and zero unsupported. This population includes deliberately unimplemented split
regions and small countries; it is not a representative accuracy sample.

## Terms and provenance

`sources.lock.json` records exact URLs, sizes, hashes and validity bounds.
IANA data use the [preserved license](IANA-LICENSE.txt). Natural Earth's
[terms](https://www.naturalearthdata.com/about/terms-of-use/) place the selected
map datasets in the public domain. Credit Natural Earth contributors Tom
Patterson and Nathaniel Vaughn Kelso. The selected admin-0/admin-1 release is
5.1.1 and the [lakes release](https://www.naturalearthdata.com/downloads/10m-physical-vectors/10m-lakes/)
is 5.0.0. These are geographic inputs, not authoritative timezone boundaries.
No raw archive or upstream executable source is included in Git.

Original region/ID dataset contributions, generated data and derived coverage
records are **CC BY 4.0**, credit Jonathan Yantis, under
[repository licensing](../../LICENSING.md). This includes `zones.json`,
`regions.json` and the runtime dataset files. Preserve the public-domain source
terms; the dataset license does not replace them. Executable Python/C code is
MIT except where an existing per-file notice applies.
