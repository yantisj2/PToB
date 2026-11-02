# Particle M-HAT B504e receiver

Copyright (c) 2026 Jonathan Yantis. Original adapters use MIT.

Experimental **nRF52840 / B504e (`b5som`, platform 25)** leaf firmware for the
Raspberry Pi 5 gateway. It reuses the current T096 RC receiver and authenticated
codecs in this repository: GM7 coarse revision 4, JOIN revision 5 and RC control
revision 8, with G1/RC7 periodic precision reception. It discovers sources locally
and performs fresh admission; no mesh or parent is pinned into the image.
Direct-GM admission includes the reciprocal R2G1 GATT reply service as well as
the RC7 relay service. Replies enter the shared owner's exact-connection,
authenticated JOIN exchange; exposing the characteristic grants no clock access.

The outer context supervisor allows the native owner's bounded discovery and
longest RC7 acquisition trial to finish before rediscovery (currently 690 s
without valid time). A proved stopped owner recovers separately. This recovery
budget never extends timing admission, source expiry or PPS permission.

The firmware replaces Particle's system region with a Zephyr application. It
does not run Particle Device OS or use its BLE API. The existing Particle MBR,
SoftDevice bytes, user partition, bootloader, external flash and factory data
must be preserved. Stock Device OS is the recovery image, not the timing engine.

## PPS wiring and validity

| Signal | nRF52840 | B504e / M-HAT | Raspberry Pi |
| --- | --- | --- | --- |
| PPS output, 10 ms rising pulse | P1.02 | D2 / RTS, M.2 pin 42 | GPIO16, physical pin 36 |
| Bus switch enable | P0.03 | A0 / SEL, M.2 pin 23 | Internal HAT switch |
| Diagnostic TX | P0.06 | D9 / TXD, M.2 pin 36 | GPIO15 RX, physical pin 10 |
| Diagnostic RX | P0.08 | D10 / RXD, M.2 pin 38 | GPIO14 TX, physical pin 8 |
| Ground | GND | Shared HAT ground | Shared ground |

No jumper is needed for this route. Hardware UART flow control cannot use D2
while it carries PPS. GPIO4 / module D8 is Pi power control and is not PPS.
The board needs a BLE antenna on its BT connector and a suitable existing power
arrangement; the firmware does not change PMIC, boost or Pi power controls.

RADIO ADDRESS is captured by PPI into the dedicated 16 MHz TIMER3. The unmodified
receiver associates authenticated originals and follow-ups, then publishes an
immutable clock offer with its original source/session/map expiry. PPS is
scheduled with TIMER3 compare channels 4/5 through PPI and GPIOTE SET/CLR. The
falling event disables both routes, making each pulse a hardware one-shot.
The planner re-projects the latest admitted map until the edge is within 20 ms;
it does not commit a one-second forecast while newer fits arrive. The final
compare uses the nearest attainable timer edge, avoiding always-late rounding.
A valid imminent pulse is still considered when the one-second search endpoint
is past source expiry; both its rising and falling edges must remain admitted.
Repeated identical owner offers do not advance the arm revision; real map,
authority and expiry changes remain fenced, and revision saturation withdraws
permanently. The logical map keeps fractional phase; aggregate phase estimates can resolve
below a timer tick when their error/correlation model supports it.
Invalid or replaced contexts withdraw the output; a free-running timer never
substitutes for an unavailable source. Coarse UTC second numbering on the Pi
comes from NTP. The nRF uses the authenticated source's UTC translation to choose
the second boundary, without treating a host NTP packet as a radio observation.

This board has **no measured phase calibration**. It deliberately excludes the
T096-specific empirical phase correction. Timer quantization, RF detector delay,
HAT delay and Pi GPIO interrupt capture need independent measurement. A PPS
selection in chrony proves an operational path, not sub-microsecond accuracy.

UART telemetry distinguishes the current admitted timing root from the selected
BLE parent. It retains the last valid root, root boot and source epoch explicitly
as history after withdrawal. Passive signed GS announcements supply descriptive
GM/HOT reports with receipt age; they add no scans and cannot authorize timing.
Signatures do not independently establish current liveness or the age of a
previously unseen old report. Eight incarnation replay fences are retained per
HAT boot; a full diagnostic cache refuses further unseen incarnations instead
of evicting replay state. Timing acquisition remains independent of that cache.

## Build and checks

Run `python3 firmware/mhat/tools/test_pps.py` for offline production-projection
and planner boundary checks with UndefinedBehaviorSanitizer. These synthetic
checks do not establish physical timing accuracy.


Use the public dependency lock and acquisition tool from T096:

```sh
python3 firmware/t096/tools/dependencies.py --dest artifacts/mhat-deps
artifacts/mhat-deps/venv/bin/python firmware/mhat/tools/build.py \
  --deps artifacts/mhat-deps --out artifacts/mhat-rc-01 \
  --module /absolute/private/mhat.json --enrollment artifacts/local/enrollment.json
python3 -m unittest discover -s firmware/mhat/tests -v
```

The private module profile must contain its verified Particle `device_id` and
`platform: "b5som"`. The builder derives the Nordic identity using the inspected
Particle FICR mapping and Zephyr hwinfo byte order, checks it at startup, and
keeps keys, source snapshots and enrolled binaries in ignored owner-only output.
The shared firmware retention lifecycle retains the latest two successful
bundles plus installed/recovery pins. A build does not access hardware.

The output is a bootloader-compatible CRC32 module beginning at `0x30000`, with
vectors at that address and module-info at `+0x200`. It must fit below `0xb4000`.
Never use an arbitrary nRF52840 DK image or a whole-chip erase. Preserve and
compare all protected bytes around the system region and read back the actual
new image before leaving DFU. USB DFU recovery and the matching official Device
OS system image must be verified before installation.

## Discovery and diagnostics

The image embeds a trusted PSK and key epoch, not a runtime mesh ID or parent.
It discovers a local G1 context, derives its admission address, then runs current
authenticated JOIN/RC admission. Advertisements grant no clock authority.
Sustained loss drains the owner and withdraws PPS before repeating discovery
and fresh admission.

USB CDC and the HAT UART emit JSON once per second, at 115200 baud / 8N1 without
flow control. Data includes source/device/boot binding, acquisition/admission,
radio outcomes, validity, PPS counters, last arm lead/map age, route, fit and a
bounded event journal.
Unknown bounds remain unknown. UART uses interrupt buffering; diagnostic
storage is static and the builder checks main-stack call margin.
The [Pi collector/dashboard](../../docs/pi5/timing.md) reads this stream without
a BLE connection. Presentation never grants timing permission.

## Exact-device installation

Use the verified recovery manifest and the actually installed image:

```sh
node firmware/mhat/tools/dfu.cjs --source INSTALLED_FULL_SOURCE_SHA256 \
  --out artifacts/mhat-dfu.json
node firmware/mhat/tools/install.cjs --build artifacts/NEW-BUILD \
  --current-build artifacts/INSTALLED-BUILD \
  --module /absolute/private/mhat.json --recovery artifacts/RECOVERY/manifest.json \
  --out artifacts/mhat-install.json
```

The USB command binds factory identity, full installed source and current boot.
It withdraws PPS and proves owner closure before requesting the existing
bootloader through retained RAM. It is unavailable over UART, BLE and the web.
Older images require physical MODE/RESET until the LED blinks yellow.

The installer validates actual current bytes, exact DFU identity, candidate
bounds/CRC and recovery identity. It writes only the system window, verifies
new-image readback and compares both protected regions before startup. Failed
readback never automatically repeats a write; `--resume REPORT` only verifies
the bound previous write. No full-flash backup or protection change occurs.
Keep failures, receipts and enrolled images private.

[Pi preparation and services](../../gateways/pi5/README.md) ·
[M-HAT interfaces](../../docs/pi5/mhat-access.md).

The source reuses existing Apache-2.0 codec/controller adapters, Nordic/CMSIS and
Zephyr dependencies under their existing terms. Original new adapters and tests
are MIT. Zephyr's default USB VID/PID is used only for this local experiment;
shipping a USB product requires an assigned identity. No Particle firmware or
secret-bearing binary is distributed in this target. See [notices](NOTICE.md).
