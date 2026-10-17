# macOS USB / chrony client

[<img src="../../docs/images/chronycontrol-tracking-10min-20261016.png" width="641" alt="ChronyControl tracking graph showing a cleaner selected development interval of approximately ten minutes">](../../docs/images/chronycontrol-tracking-10min-20261016.png)

A cleaner, approximately ten-minute development view in
[ChronyControl by Bryan Christianson](https://www.whatroute.net/chronycontrol.html).
We are continuing to reduce timing transients, with GM authentication the next
planned focus. The graph shows chrony's tracking statistics against the
experimental feed. [Image provenance and measurement scope](../../docs/images/README.md).

A [historical client replay](../../docs/images/macos-chrony-replay.webp) also
retains its loss and withheld-UTC indicators. Its displayed 98 ns is chrony's
estimator RMS; independent UTC accuracy remains unqualified.

Copyright (c) 2026 Jonathan Yantis. MIT; third-party terms are preserved.

PToB owns this macOS client. It selects a compatible T096 UTS1 vendor timing
interface on USB and supplies an experimental user clock through a private
unprivileged chronyd. Normal launch uses the unqualified fine feed. macOS system
time is unchanged. Direct Mac BLE, SS3 CDC timing input and network time serving
are not supplied. See the [standalone build guide](../../docs/macos/build.md),
[chrony guide](chrony/README.md), [USB contract](usb/README.md),
[cutover boundaries](../../docs/macos/cutover.md) and
[per-file provenance](imports/2026-10-16.json).

The selected source is CLI 0.21 (`ptob-mac-cli/0.21-sof-map-gap`), with exact
file hashes recorded because its latest source included uncommitted additions.
Public changes replace laboratory identity fixtures with synthetic IDs and use
the shared display registry. Build/self-test success is separate from hardware,
calibrated UTC, independent precision and continuous availability acceptance.

Normal launch opens USB and starts an owned daemon; `--self-test`, reports and
precision replay are offline. `--no-chrony` selects USB-only operation. `c` opens
chrony statistics; `d` diagnostics, `h` help and `q` closes. The 500 ms display
hold is presentation only: withheld UTC remains withheld immediately.
Chrony's estimator RMS, direct shared-source relative error and independent UTC
accuracy are different metrics. Unknown error bounds remain unknown.

With a compatible T096, the optional UTE1 response also supplies a retained
local clock estimate. The client displays its growing observation age and
HOLDOVER / LOCAL ONLY state separately from valid source observations. A
same-source quality expiry pauses chrony input while preserving its owned
process; a source or counter change still fences old training. Extrapolations
are never fed back as measurements. See the
[continuous-estimate contract](../../docs/t096/continuous-estimate.md).

Admission and continuity fixes are integrated, including automatic rediscovery
and the normal 32 Hz USB rate. Long-term delivery remains under evaluation.
The [project changelog](../../CHANGELOG.md) and
[stability results](../../evidence/stability/2026-10-16/README.md) retain completed
checks and remaining investigations. Use an RC USB source: the reviewed normal
GM backend does not publish a USB source map.

For a complete group, follow the [two-GM setup guide](../../docs/GETTING_STARTED.md).
