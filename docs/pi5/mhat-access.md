# M-HAT nRF52840 access

Copyright (c) 2026 Jonathan Yantis. MIT.

The Particle B504e contains an nRF52840. Particle identifies its Device OS
platform as **b5som**, with a separate bootloader, system and application layout;
it is not a T096 image target. Start with the vendor
[B504e datasheet](https://docs.particle.io/reference/datasheets/b-series/b504-datasheet/).
Stock Particle firmware is not a PToB precision receiver.

The [M-HAT datasheet](https://docs.particle.io/reference/datasheets/m-series/m-hat-datasheet/)
documents two host paths. USB data reaches the module through the HAT USB-C
connector. The header connects Pi GPIO14/15 (UART TX/RX) and GPIO16/17 (CTS/RTS),
but its UART isolation switch needs module firmware to drive A0/SEL high.
Power and the GPIO stack alone do not establish a USB/programming connection.

Use a data-capable USB-C connection and a compatible USB-C PD power arrangement.
Particle specifies 9 V PD and warns against powering both the Pi USB-C and HAT
USB-C together. Do not substitute a USB-A power cable or infer jumper settings.
IOEX is GPIO expansion. Verify SWD wiring before attaching a programmer.

Initial access reads USB VID/PID, module ID, runtime/DFU mode and firmware
information. Match the exact module ID before any state-changing command. Keep
those identities and raw USB/serial captures in ignored local artifacts. Do not
claim a Particle account, enable cellular tethering or install the vendor's full
tethering blueprint merely to access BLE.

Use the official [Particle CLI](https://github.com/particle-iot/particle-cli)
installed in an ignored local tools directory, with an exact version and npm
lockfile. Use its local USB commands; cloud login is not needed for USB inventory.
Do not open unrelated laboratory serial ports. Do not invoke reset, DFU, flash,
recover or chip erase during identification. A later firmware change needs exact
factory identity, present layout, protected-region preservation and recovery.

The bounded query helper uses `particle-usb` 4.3.1 from Particle CLI 3.52.4's
dependency tree and opens only the supplied exact module ID:

```sh
npm install --prefix artifacts/pi5/particle-tools --save-exact particle-cli@3.52.4
node gateways/pi5/tools/mhat_usb.cjs --id YOUR_VERIFIED_MODULE_ID
```

Node 22.20+ is required by this CLI release. The helper reads firmware version,
mode and module metadata; it never invokes the CLI's protection-changing wrapper,
cloud API, reset, firmware readback or flash commands. Its JSON output is private.
After identification, a private `~/.config/ptob/mhat.json` containing `device_id`
lets the same command run without an `--id` argument. No device ID is compiled
into the helper or a firmware image.
The helper preserves query failures in the report and exits with status 2 when
inventory is partial; a timeout does not establish firmware corruption.

The Pi helper supplies package provisioning and read-only PHC inspection.
An nRF52840 timing port still needs physical RF captures, authenticated source
association, expiry/uncertainty handling and a measured handoff to the Pi's
Ethernet clock. Transport latency and clock-read brackets do not establish the
requested sub-microsecond output accuracy.
