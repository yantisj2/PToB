# macOS USB / chrony client

<a href="../../docs/images/macos-chrony-replay.webp"><img src="../../docs/images/macos-chrony-replay.webp" width="760" alt="PToB macOS chrony replay with time, estimator RMS, retained loss and UTC-gated indicators"></a>

*Historical USB/chrony replay with loss and withheld UTC retained. The displayed
98 ns is chrony's estimator RMS, not calibrated UTC accuracy.
[Image provenance](../../docs/images/README.md).*

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

For a complete group, follow the [two-GM setup guide](../../docs/GETTING_STARTED.md).
