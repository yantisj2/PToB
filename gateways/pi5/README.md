# Raspberry Pi 5 development target

Copyright (c) 2026 Jonathan Yantis. MIT.

Regular Raspberry Pi 5 Model B, Raspberry Pi OS arm64. This target supplies
repeatable SSH provisioning, hardware inventory and a native build/clock smoke
test, clock services and a private dashboard for an experimental PToB gateway.

The first external BLE timing-source candidate is the nRF52840 in a Particle
B504e on an M-HAT. The [native receiver](../../firmware/mhat/README.md) consumes
current authenticated RC7 periodic timing and outputs PPS to GPIO16. Chrony
uses PPS for phase and NTP for coarse UTC seconds. NTP serving, Wi-Fi software
PTP and the Ethernet hardware timestamp path have separate services. Ethernet
cable testing and the below-one-microsecond objective remain unqualified.

The read-only dashboard shows admission, source freshness, PPS edges, chrony
selection, receiver diagnostics and dated wire checks. Timing charts compare
clock offsets, RMS/source scatter, GPIO PPS variation and receiver drift, with
window statistics and complete CSV exports. It binds to loopback
behind private Tailscale Serve, with no flash/reset/clock-control API.

The network tile separates current PToB transfer from chrony holdover. Ethernet
readiness comes from fresh PHC-transfer and PTP-server guard observations, not
just active supervisor processes; a disconnected port remains explicitly
unconnected. These observations do not measure output accuracy.

Source reporting names the admitted GM and immediate BLE parent separately,
shows root boot/source epoch and recovery state, and labels a withdrawn source
as last admitted. Signed GM/HOT announcements describe reported roles, with
receipt age and replay-cache limits; they are not timing admission or a remote
liveness test. The source-transition list starts with the collector; recorded
history/CSV includes GM incarnation and selected parent for later analysis.
Rows with `lineage_schema=1` identify the actual selected upstream parent;
`gm_admitted` distinguishes current admission from a retained last valid root.
Earlier rows without that schema do not establish upstream-parent lineage.
The [raw-tail capture tool](../../docs/pi5/timing.md#timing-history-and-jitter-diagnosis)
retains unique kernel PPS assertions for bounded tests, with P99, maxima, spike
counts and explicit source/gap populations independent of chart snapshots.

## Access and preparation

```sh
ssh ptob-pi5
python3 gateways/pi5/tools/remote.py inventory
python3 gateways/pi5/tools/remote.py provision
python3 gateways/pi5/tools/remote.py smoke
node gateways/pi5/tools/mhat_usb.cjs
python3 gateways/pi5/tools/remote.py timing-install --wifi-peer auto
python3 gateways/pi5/tools/remote.py timing-status
python3 gateways/pi5/tools/remote.py clock-install
python3 gateways/pi5/tools/remote.py tailscale-install
python3 gateways/pi5/tools/remote.py tailscale-status
python3 gateways/pi5/tools/remote.py dashboard-install --mhat-build artifacts/INSTALLED-MHAT-BUILD
```

The private connection profile defaults to `~/.config/ptob/pi5.json`; keys and
pinned host keys belong in `~/.ssh/`. None belongs in Git. The remote helper
requires the enrolled Pi factory serial, arm64 model and image instance ID
before every operation. Reports go under ignored `artifacts/pi5/reports/`.
Use `--connection PATH` for another explicitly enrolled device.
The USB helper reads its separate exact-module binding from
`~/.config/ptob/mhat.json`; its dependencies are described in the M-HAT guide.

Provisioning installs missing development packages using the OS's signed APT
repositories. It refuses active PTP services and does not change Bluetooth
firmware, clocks, network routing, mesh enrollment or HAT power controls. Package
versions are recorded, rather than claiming all future APT resolutions identical.
The smoke action compiles a small native program under `~/ptob/pi5/`, links BlueZ
and systemd, and reads clock capabilities. It does not send timing packets.
`timing-install` is a separate operational action: it configures system clock
discipline and enables NTP/PTP serving. Do not run baseline provisioning over an
active timing experiment. See the [timing guide](../../docs/pi5/timing.md).
For an already configured gateway, `clock-install` updates only the Ethernet
clock supervisors. PHC discipline runs independently of link state; Ethernet
serving waits for fresh admitted timing and PHC readiness. See
[clock ownership and recovery](../../docs/pi5/clock-chain.md) for policy and limits.

Tailscale authentication, SSH keys, factory bindings and enrollment stay outside
Git. Serve uses the device's tailnet HTTPS name. A custom `pi.ptob.dev` name
requires separate DNS/certificate provisioning; never enable public Funnel for
this private dashboard.

Mesh selection must remain local discovery with fresh authenticated admission;
trusted credentials must not pin a production image to a mesh ID.

[M-HAT access](../../docs/pi5/mhat-access.md) explains the external USB/UART
paths. Hardware identities, module IDs, credentials and raw observations remain
private. Flashing nRF firmware needs its exact identity, current layout and a
verified recovery plan; these provisioning commands contain no flasher.

## Offline checks

```sh
python3 -m unittest discover -s gateways/pi5/tests -v
python3 -m unittest discover -s gateways/pi5/dashboard/tests -v
node gateways/pi5/dashboard/tests/source_ui_test.cjs
node gateways/pi5/dashboard/tests/chart_ui_test.cjs
node gateways/pi5/dashboard/tests/output_ui_test.cjs
```

Original tools use MIT. Raspberry Pi OS, Debian packages, Linux, BlueZ, systemd
and the separately acquired Particle CLI retain their own terms. No third-party
firmware source or binary is included here. See [notices](NOTICE.md) and
[licensing](../../LICENSING.md).

Raspberry Pi OS can retain a completed cloud-init run with a missing
`cc_netplan_nm_patch` warning. Check its actual errors, installed packages,
network and service state rather than deleting the historical status to make it
green. The vendor tracks the [missing-module reference](https://github.com/raspberrypi/trixie-feedback/issues/45).
