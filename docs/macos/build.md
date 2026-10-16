# Build the macOS USB clock client

Copyright (c) 2026 Jonathan Yantis. MIT.

The current client uses **BLE → compatible T096 USB relay → macOS**, optionally
feeding an owned user-space chronyd. Direct Mac Bluetooth is disabled and is not
linked. The selected SS3 console does not expose the required UTS1 USB timing
interface and cannot be substituted by connecting its CDC port.

Install Apple's command-line development tools (`xcrun swiftc`, Clang, SDK and
ad-hoc codesign), Python 3, Make, curl and tar. Swift uses language mode 5 and
Foundation; the helper uses Foundation, IOKit and IOUSBHost. Apple Silicon macOS
is the checked host. Fresh chrony download and clean-checkout client builds passed during the
[public-source review](../PUBLIC_RELEASE_REVIEW.md). Other architectures/OS releases
and a freshly installed OS remain unvalidated. No Swift packages or private source dependencies are needed.

From the repository root:

```sh
sh clients/macos/chrony/build.sh
sh clients/macos/build.sh --registry artifacts/local/registry.json
```

Omit `--registry` to use the synthetic public example. The same schema and
validator used by the firmware generate read-only Swift aliases. IDs still bind
USB/source provenance; aliases confer no timing permission. Rebuilding is
required to change the map. The launcher retains the chosen map snapshot during
a source rebuild. All generated tools, map snapshots and captures stay ignored
under `clients/macos/.build/` and `clients/macos/runs/`.

Chrony 4.9 is fetched from its public upstream release and SHA256 checked against
`4924c6f530105bcd5b9e9e33c48a2ae1bfd889222c8480bc41601110efc864d0`.
The build disables readline/NTS and optional crypto libraries, compiles locally
and runs only `chronyd -v`. It never installs or starts a service. An existing
verified archive at `.build/chrony/chrony-4.9.tar.gz` supports an offline build.
Chrony's GPL terms are separate from the original MIT client; see
[notices](../../clients/macos/NOTICE.md).

## Hardware-free verification

```sh
clients/macos/.build/ptob-receiver --self-test
clients/macos/.build/usb/reference-probe --self-test
python3 clients/macos/test_usb_cli.py
python3 clients/macos/test_chrony_ui.py
python3 clients/macos/test_readout_hold.py
python3 clients/macos/chrony/test_precision_evaluation.py
```

These commands use native unit checks, decoder fixtures, synthetic chronyc
responses, terminal playback and synthetic phase fits. They neither enumerate
USB nor launch chronyd. Live transport, private-daemon startup/cleanup, OS clock
control and physical precision are separate acceptance work. Build success does
not qualify absolute UTC or continuous precision availability.

## Explicit live launch

After separately arranging compatible hardware access:

```sh
clients/macos/ptob --duration 600 --jsonl clients/macos/runs/NEW.jsonl
clients/macos/ptob --no-chrony --duration 60
```

The first normal launch opens the selected USB timing interface and starts an
owned unprivileged foreground `chronyd -x -U -u CURRENT_USER`, feeding a `BLE`
SOCK refclock. It supplies an experimental user clock; it does not change macOS
time, `timed`, sync settings or register a LaunchAgent. Exit stops only its own
child and removes its private socket runtime. NTP and command UDP ports are
zero; only private local Unix sockets are exposed. Network time serving/sharing
is not configured. The client never flashes firmware or changes relay roles.

Default fine input composes causal SOF/Mach timing with an immutable source map
and retained published offset. Absolute UTC, initial source bias and host/PHY
bias remain unqualified; `VerifiedUTCAnchor` stays closed. `--coarse-chrony`
selects request-midpoint input explicitly. There is no automatic coarse fallback
into the fine filter. Read [chrony isolation and interpretation](../../clients/macos/chrony/README.md)
before interpreting its estimator RMS or attaching another instance.
