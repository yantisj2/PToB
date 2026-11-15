# Build T-Watch Ultra

Copyright (c) 2026 Jonathan Yantis. MIT.

Read the [target guide](../../firmware/twatch/README.md) first. This is an offline
native port, using the same pinned ESP-IDF v5.5.5 and Xtensa toolchain as
[W12](../w12/build.md). The builder opens no hardware and marks output
`OFFLINE_NOT_FLASHABLE`. It uses the shared firmware retention lifecycle.

## Board boundary

The Ultra uses I2C SDA3/SCL2; AXP2101 at 0x34, XL9555 at 0x20 and CST9217 at
0x1a. Display QSPI uses CLK40, CS41, D0=38, D1=39, D2=42, D3=45 and RESET37.
The panel's visible columns start at 22. Radio SPI uses CLK35/MOSI34/MISO33,
CS36, RESET47, BUSY48 and DIO1=14. Radio SPI2 and display SPI3 are independent.
The expander uses pin 7 for display enable, pin 8 (P1.0) for touch reset and
pin 11 (P1.3) for the built-in radio antenna. This follows the vendor schematic
and board definition; the descriptive hardware table incorrectly lists touch
reset as pin 10, which is SD detection. GNSS UART43/44 is not opened;
native USB Serial/JTAG owns diagnostics. GNSS PPS13 is not configured.

The shared N30 timing core, selection policy and W12 G1/RC7 sources compile
directly. Selected main/radio/log adapters are recorded in
[imports.json](../../firmware/twatch/imports.json); W12/N30 firmware is unchanged.
The 20 MHz baseline is used: N30 GPIO4/5 fine capture and W12 calibration must
not be transplanted onto NFC pins. Independent output bounds remain unknown.

## Inputs and build

Use ignored owner-only device, enrollment and shared registry files. Device schema:

```json
{"board":"lilygo_twatch_ultra_v1","radio":"sx1262","factory_mac":"02:00:00:00:00:01","device_id":"0000020000000001","label":"Watch-test","legacy_wire_id":12}
```

This identity is synthetic, for offline compilation only. Actual onboarding
requires factory-MAC verification and a `twatch` row in the shared registry.
Do not infer radio type from the USB serial. An SX1280 cannot receive the current
922.4 MHz fallback. Enrollment uses the existing group/epoch/PSK schema; mesh
selection remains runtime discovery, not a compiled mesh ID.

```sh
python firmware/twatch/tools/build.py --idf "$IDF_PATH" \
  --out artifacts/watch-build-01 \
  --enrollment artifacts/local/enrollment.json \
  --device-config artifacts/local/watch.json \
  --registry artifacts/local/registry.json \
  --lora-transmitter-factory VERIFIED_T096_FACTORY_ID
```

Zero transmitter factory disables LoRa hardware for offline testing. The radio
must be correctly identified and configured before claiming always-on fallback.
The candidate uses a 16 MiB flash configuration and 1,536,000-byte application
ceiling. This is a compiler layout, not evidence of the incoming factory layout.

Before installation: verify exact USB/factory identity, exclusive ownership,
security state, actual app partition and active OTA selection, compatible recovery,
original bounded drain where supported, and selected source/build review. Use
app-only write bounds, actual new-application readback and protected-prefix/tail
comparison. No whole-flash backup is part of ordinary updates. Do not erase
factory partitions to make this candidate fit. Native success is not permission
for hardware deployment or a precision qualification.

Host rendering needs a C++17 compiler. Font regeneration additionally requires
Pillow and the OFL Rajdhani SemiBold font; the generated atlas and font license
are included. Preview values are synthetic and must not be shown as observations.

```sh
python3 firmware/twatch/tools/test.py --out artifacts/watch-host-01
```

This produces nineteen actual-renderer PPM previews and a host-check receipt.
The tests cover navigation, settings bounds, first-touch wake consumption,
expired timing, framebuffer guards and all screen destinations.

## USB ROM entry

The watch keeps the ESP32-S3 native USB Serial/JTAG console active, including
while its screen sleeps. Use the Console V3 CRC parser in
`firmware/twatch/tools/framed_console.py` with the exact installed build identity.
A fresh status response supplies a single-use ticket bound to factory MAC and
boot session. Send `prepare_update` using that ticket, then poll fresh status
until `application_drained=1` (`update_stage=2`). This stops BLE, LoRa, capture
and the display/settings worker within the original two-second deadline.

With a new ticket, send `bootrom`. The firmware waits for the complete response
and USB transmit drain, sets the S3 force-download flag and restarts into ROM.
The acknowledgment has a separate 1.5-second deadline; expiry cancels the reset
and cannot extend the maintenance drain. Failed drains and stale/wrong-session
commands cannot enter ROM. No flash or eFuse is written by this command.

This path requires a responsive application and USB connection. For a crashed
application, retain the physical recovery path: hold the lower-left BOOT button,
press/release the recessed right-side RESET button between USB-C and the SD
slot, then release BOOT. The upper-left button is power, not reset. See
[LILYGO's download-mode instructions](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/lilygo-t-watch-ultra.md#entering-download-mode-on-the-t-watch-ultra).

With `pyserial` and `esptool==4.12.0` installed, the bounded host tool performs
this exchange and verifies the ROM factory identity:

```sh
python firmware/twatch/tools/usb_bootrom.py enter \
  --factory VERIFIED_FACTORY_MAC --receipt artifacts/INSTALLED_BUILD/receipt.json \
  --out artifacts/watch-rom-entry-01
python firmware/twatch/tools/usb_bootrom.py run \
  --factory VERIFIED_FACTORY_MAC --receipt artifacts/INSTALLED_BUILD/receipt.json \
  --out artifacts/watch-rom-return-01
```

`run` clears the volatile force-download bit and uses Espressif's S3 watchdog
reset to exit ROM; verify fresh application status afterward. Neither command
writes flash. The tool requires exclusive native USB ownership and fresh ignored
output directories. Its diagnostic port suppresses DTR/RTS transitions so simply
opening the console does not reset the watch. Physical recovery remains available.
