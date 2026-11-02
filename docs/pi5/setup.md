# Set up a Raspberry Pi 5 timing gateway

Copyright (c) 2026 Jonathan Yantis. MIT.

Use this guide with the [Pi target README](../../gateways/pi5/README.md).
It prepares a regular Pi 5 Model B running Raspberry Pi OS Lite, 64-bit,
Debian 13 (trixie). CM carriers and other releases require their own inventory.
The repository distributes source and configuration; it contains no SD image,
SSH key, Wi-Fi password, enrollment or enrolled firmware binary.

## 1. Boot and establish private SSH access

Use [Raspberry Pi Imager](https://www.raspberrypi.com/documentation/computers/getting-started.html#install-an-operating-system)
to install the OS on your selected microSD card. A 64 GB card is suitable for
this development target. Configure your own hostname, user, network and SSH
public key using Imager's customization. Keep the private key outside this
checkout. Verify the chosen disk before writing; these scripts do not flash SD
cards. Boot the Pi with a suitable power arrangement. Consult the
[M-HAT power/access guide](mhat-access.md) before combining supplies.

From the Pi console or a separately trusted session, inspect its model,
architecture, factory serial and SSH host-key fingerprint:

```sh
uname -m
tr -d '\000' < /proc/device-tree/model
tr -d '\000' < /proc/device-tree/serial-number
sudo ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub
```

Require `aarch64` and the exact Pi 5 Model B identity. Follow Raspberry Pi's
[SSH documentation](https://www.raspberrypi.com/documentation/computers/remote-access.html#ssh)
for first access. Compare the first connection's host-key fingerprint through
that trusted console; an unverified `ssh-keyscan` result is not a pin.

On a newly enrolled Pi, create its one-time image-instance marker. This is a
local installation identifier, not a reusable factory serial:

```sh
sudo python3 - <<'PY'
from pathlib import Path
import uuid
p = Path('/opt/ptob/pi5/IMAGE_ID')
if p.exists():
    raise SystemExit('Already enrolled: inspect and preserve the existing marker')
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text('ptob-pi5-' + str(uuid.uuid4()) + '\n')
p.chmod(0o644)
print(p.read_text().strip())
PY
```

On the workstation, create an owner-only connection profile at
`~/.config/ptob/pi5.json`. Replace every placeholder with the independently
verified value. Paths must be absolute and outside the repository:

```json
{
  "host": "pi-admin@pi-gateway.local",
  "serial": "YOUR_16_LOWERCASE_HEX_FACTORY_SERIAL",
  "image_id": "ptob-pi5-YOUR_GENERATED_UUID",
  "identity_file": "/home/you/.ssh/id_ed25519_ptob_pi5",
  "known_hosts": "/home/you/.ssh/ptob_pi5_known_hosts",
  "host_key_alias": "ptob-pi5-enrolled"
}
```

Use your actual home path on macOS or Linux. The dedicated known-hosts entry
must bind `ptob-pi5-enrolled` to the verified SSH public host key, in OpenSSH
known-hosts format. Set the profile, private key and known-hosts file to mode
0600 and their private parent directories to 0700. The operator account needs
noninteractive sudo for the root provisioning actions; read-only capture does
not require an additional system-clock owner. Do not weaken host-key checking.
`remote.py` checks the pinned SSH key, exact factory serial, arm64 model and
image marker before executing its payload.

## 2. Inventory and prepare the host

Run from the repository root on the workstation:

```sh
python3 gateways/pi5/tools/remote.py inventory
python3 gateways/pi5/tools/remote.py provision
python3 gateways/pi5/tools/remote.py smoke
```

Reports are written beneath ignored `artifacts/pi5/reports/`. The inventory
records Bluetooth, Ethernet timestamp and PHC capabilities. Provisioning installs
missing signed OS development packages, including the leap-aware timezone
package; it refuses active baseline PTP services. Run it before timing services
are enabled. The smoke test builds a native read-only clock/BlueZ/systemd probe.
It neither transmits precision BLE timing nor measures accuracy.

The Pi's internal Bluetooth controller is not the implemented precision source.
This target uses an external MCU's hardware event capture. The native Ethernet
PHC must be inventoried: packet timestamp support does not imply external PPS
capture or periodic output pins. Keep OS/package versions with each experiment;
signed APT installation is repeatable setup, not an immutable OS build.

## 3. Prepare the M-HAT and host bus

Identify the exact B504e and retain its verified recovery image using
[M-HAT access](mhat-access.md) and the
[firmware build/install guide](../../firmware/mhat/README.md).
The firmware guide covers local public dependency acquisition, private trust
inputs, build retention, DFU identity and protected-region/readback checks.
Do not substitute a generic nRF52840 image. No flash operation is part of host
package provisioning.

The GPIO stack supplies this route:

| Function | M-HAT / nRF52840 | Pi header |
| --- | --- | --- |
| Hardware PPS | D2 / RTS, P1.02 | GPIO16, pin 36 |
| UART diagnostics TX | D9 / TXD, P0.06 | GPIO15 RX, pin 10 |
| UART diagnostics RX | D10 / RXD, P0.08 | GPIO14 TX, pin 8 |
| Common reference | Ground | Ground |

The firmware enables its internal bus switch. In the Pi's
`/boot/firmware/config.txt`, enable `dtoverlay=uart0-pi5` under `[all]`, without
`ctsrts`, and reboot cleanly during setup. The header UART is normally
`/dev/ttyAMA0`; `/dev/serial0` may instead identify the debug UART.
RTS cannot also provide flow control while it carries PPS. GPIO4 is HAT power
control and must not be substituted. Attach the module's separate BLE antenna.

After installing an image, retain that exact successful build bundle under
ignored `artifacts/`; the collector installer binds its source hash and the
private verified module profile. Runtime mesh selection is local authenticated
discovery. Trust enrollment never pins a production image to one mesh or GM.

## 4. Install the timing chain and monitor

These commands configure clock discipline and enable serving. Choose a network
and peers intended for a timing bench; the included chrony configuration allows
NTP clients. `--wifi-peer auto` selects the workstation's route address for the
Wi-Fi PTP unicast test; use an explicit lab peer address when routing differs.

```sh
python3 gateways/pi5/tools/remote.py timing-install --wifi-peer auto
python3 gateways/pi5/tools/remote.py dashboard-install \
  --mhat-build artifacts/YOUR_ACTUALLY_INSTALLED_MHAT_BUILD
python3 gateways/pi5/tools/remote.py jitter-install
python3 gateways/pi5/tools/remote.py timing-status
```

Do not substitute a candidate build for the actually installed HAT image.
`dashboard-install` restarts telemetry and the web service, so use it outside
measurement windows. `timing-install` installs GPIO PPS/udev mapping, chrony,
Wi-Fi PTP and the guarded Ethernet chain. It saves prior owned configuration
under `/var/lib/ptob/pi5/timing-before/`. `jitter-install` binds GPIO16's IRQ to
CPU3, selects the performance governor and keeps services on CPUs0–2; record
that policy and thermal/power conditions in comparisons.

Chrony is the only system-clock servo. It combines `/dev/pps-ptob` phase with
NTP coarse UTC. Debian 13 requires `tzdata-legacy` for `right/UTC`; verify a
valid kernel TAI offset after boot. Never hard-code an offset to bypass a
withdrawn Ethernet guard. See [clock ownership and recovery](clock-chain.md).
A new gateway may need time to acquire and settle before PPS is selected.

The web service listens only on `127.0.0.1:8080`. Use an SSH forward for local
monitoring, substituting your own already verified SSH destination:

```sh
ssh -N -L 8080:127.0.0.1:8080 pi-admin@pi-gateway.local
```

Open `http://127.0.0.1:8080/` locally. For private remote access, the optional
operator-owned VPN setup is:

```sh
python3 gateways/pi5/tools/remote.py tailscale-install
python3 gateways/pi5/tools/remote.py tailscale-status
```

Authorize your own login URL if requested, then repeat installation to enable
[private Serve](https://tailscale.com/docs/reference/tailscale-cli/serve).
Use the HTTPS name assigned to your own device. No account-specific address is
provided in this repository. Funnel, exit routing and Tailscale SSH are not
enabled. A custom hostname needs separate DNS/certificate provisioning.

## 5. Verify operation and measure separately

Use the [monitoring README](../../gateways/pi5/dashboard/README.md) to inspect
source admission, fresh edges, PTOB selection and the actual guard child state.
Check that the complete path recovers after a planned reboot before starting
an experiment. Retain the original failed observation if a check fails.

```sh
python3 gateways/pi5/tools/remote.py timing-status
python3 gateways/pi5/tools/ntp_probe.py --out artifacts/pi5/ntp-check.json
sudo python3 gateways/pi5/tools/ptp_observe.py \
  --connection /absolute/private/pi5-lan.json --out artifacts/pi5/ptp-check.json
mkdir -p artifacts/pi5/reports
python3 gateways/pi5/tools/pps_capture.py --capture --seconds 600 \
  --out artifacts/pi5/reports/pps-window.json
python3 gateways/pi5/tools/pps_capture.py \
  --analyse artifacts/pi5/reports/pps-window.json \
  --out artifacts/pi5/reports/pps-window-reanalysis.json
```

The PTP receiver binds local UDP319/320; use the explicit LAN profile and
required local socket privileges, and keep those ports free. Choose unused
report names. These are operational probes except the last
command, which is offline. A cached assertion is a seed, not a new pulse.
Keep all new edges, tails, source changes and missing opportunities. Do not
select a run by low error magnitude. The normal bounded PPS tool is distinct
from the exact frozen observer used for the paper's final population.

NTP replies and local PTP egress demonstrate protocol health. GPIO IRQ phase
is relative to the system clock being disciplined and includes Linux latency
and clock slew. Source fit RMS, chrony's RMS estimate and raw phase are separate
metrics. Ethernet accuracy needs a connected, independently measured peer;
its cable test is not yet complete. See [timing diagnostics](timing.md), the
[paper and results](../../papers/pi5-mhat-gateway/README.md) and the
[next bridge experiments](bridge-work.md).

## Service and update map

| Service | Owner / responsibility |
| --- | --- |
| `chrony` | Sole Pi system-clock writer; PPS phase and NTP coarse UTC |
| `ptob-telemetry` | Bound UART collection and history; no clock writes |
| `ptob-dashboard` | Read-only loopback web API and frontend |
| `ptob-pps-tuning` | Exact-device GPIO IRQ / CPU policy |
| `ptob-phc2sys` | System clock → Ethernet PHC, with source/TAI guards |
| `ptob-ptp4l` | Guarded server-only Ethernet PTP child |
| `ptob-ptp-wifi` | Wi-Fi software PTP, no system-clock adjustment |

For an existing installed gateway, `remote.py clock-install` updates only the
owned Ethernet supervisors/configuration; it does not reinstall chrony or the
collector. Every installer is an operational change and belongs outside a
capture window. Full firmware installation/recovery follows the separate M-HAT
guide. The public scripts were exercised on the enrolled development target;
a newly flashed host must still pass its own inventory, reboot and wire checks.
