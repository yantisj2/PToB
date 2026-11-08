# W12 reference display and controls

The display is a small operating view for learning relative timing. It uses the
W12's landscape 128x64 SSD1315, with its physical green top band and blue lower
band. The independently drawn 5x7 font, framebuffer renderer and button decoder
are plain C and can be tested on a host. GPIO/I2C/power behavior stays in the W12
adapter; another board can replace that adapter without changing the PLL.

The clock page shows the role, local alias, counter rate, loop state and
`REL TIME`. Long elapsed times switch from seconds to whole hours. The follower's
`PHASE LEFT` is the signed phase backlog still to be consumed. Zero means the
software model has no pending correction. Large corrections direct you to USB
for the full value. The leader shows `LOCAL FREE RUN` because it supplies the
reference from its oscillator. Relative time has no civil or UTC epoch.

The follower's Info page keeps the loop state visible above the rate in ppm,
`BLE RMS`, `PAIR/MISS` and peer. BLE RMS is the bias-inclusive RMS of up to 64
saved pre-update prediction errors in the current quality window. It measures
the loop against BLE observations. `--` means no eligible predictions. Counts
use truncated K/M/G units when needed and a `>` marks shortened aliases.
USB status carries the full counts, maximum prediction error, and
scoreable/unavailable/refused populations. LOOP LOCK means the declared software
gates passed. The RF and output timing bounds remain unknown.
The second number in `PAIR/MISS` counts unmatched follow-ups plus event-queue
drops. A physical test accounts for nominal epochs and missing outputs separately.

The leader's Info page shows its local counter rate and tick size. Source loss
and faults replace Info with the current state and a recovery hint. The visible
states are SEARCHING, JOINING, ACQUIRING, TRACKING, LOOP LOCK,
TIME LOST, FAULT, REFERENCE and the shutdown states. The reference is a local
free-running leader; it supplies no GNSS or UTC authority.

## Button and off sequence

Tap to switch between Clock and Info when timing is available. Hold for four
seconds to request off. The UI redraws on the button event and says
`RELEASE THE BUTTON` while the radio drains. Sleep needs all owners stopped
within the original two-second deadline and a stable 40 ms button release.
A button held at boot cannot request another shutdown until released.

1. Withdraw usable timing/output and stop scanning, advertisements and sync.
2. Receive the SDK stop acknowledgments, stop the host and restore capture hooks.
3. Release pulse and counter resources, then deinitialize the controller.
4. Turn the OLED off and release its I2C driver.
5. Drive VEXT45 HIGH, reset47 LOW, SDA17/SCL18 LOW. Hold known auxiliary-radio
   reset/rails off when that physical profile was selected.
6. Hold safe pad levels, arm GPIO0 low wake and enter deep sleep only with the
   button still released. The next button press reboots the application.

Off means radio/controller stopped, display power removed and MCU deep sleep.
Physical sleep current, battery lifetime and assembled-board behavior still
need hardware validation. USB power may remain physically attached.

Any drain/API refusal or returning sleep call is failure. A late acknowledgment
cannot repair the earlier failed deadline. The display shows OFF FAILED when
available; `status` reports `power_stage`, `power_fault` and `failed_stage`
(`OWNER_DRAIN` or `BOARD_POWER`). The fault remains until reset/recovery; repeated
requests do not extend a drain or silently retry it. Sleep preparation failure
attempts to restore the display after released drivers so the fault can be read.

The `stop` command uses the same bounded owner drain but leaves the console and
display awake for maintenance. `REF1_READY` means the example's owners stopped;
it is not the production W12's framed maintenance protocol or hardware flash
authorization. Installation tooling must use the protocol of the installed image.

The [exercise screenshots](../01_wireless_softpll/README.md#read-the-w12-oled)
come from the same C renderer as firmware. Their values are illustrative and
their colors approximate the panel.

Copyright (c) 2026 Jonathan Yantis. [MIT](../LICENSE).
