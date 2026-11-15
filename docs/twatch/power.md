# T-Watch Ultra power plan

Copyright (c) 2026 Jonathan Yantis. MIT.

Revision one prioritizes continuous timing reception. BLE listens at 100%; LoRa
runs passive continuous RX on its own SPI host. Neither source is suspended when
the display sleeps. This sets a real power floor: deep-sleep battery figures from
the vendor do not describe this receiver workload.

## First revision

- Default to 40% panel brightness and a 90-second display timeout; selectable
  brightness 10–100% and timeout 90/120/180 seconds.
- Render the face once per second, on navigation, or on source-validity changes.
  No continuous animation, background graphics task or frame queue.
- Keep the framebuffer and one 411,640-byte immutable Instrument dial layer in
  PSRAM. Warm the dial once during first presentation; subsequent face swipes
  reuse it, and ordinary redraws paint only moving indications over the copy; use a small internal DMA strip. Bound SPI and
  I2C transactions; fence a failed display transfer instead of reusing in-flight
  buffers. The UI runs at low priority on the core opposite BLE.
- Disable the main GNSS BLDO1 supply. Preserve backup and CPU rails. SD, audio,
  NFC and sensor supplies are off. Do not rewrite battery gauge calibration,
  charge voltage or charge current during app startup.
- Persist user settings only on changes. Do not persist radio candidates,
  clock fits or timing permission. Reboot requires fresh authenticated admission.
- Keep the timing counter continuous. CPU scaling/light sleep/deep sleep are
  deferred until capture continuity, radio deadlines and wake behavior are tested.

## Measure before optimizing

Record battery current, voltage, display duty cycle, uptime, reset reason and
source continuity for screen-on, screen-off, BLE acquisition, BLE lock and LoRa
fallback. Include display interaction and worst-brightness runs. Measure charging
and battery-only operation separately; a USB power reading is not battery drain.
Record missing intervals and watchdog events. Compare timing residuals and loss
rates with display activity; report battery lifetime only from actual discharge.

Then consider lower brightness, partial panel updates and a verified light-sleep
policy. Deep sleep is an explicit receiver-off mode and must never masquerade as
continuous LoRa fallback. GNSS shadow PPS remains a later opt-in revision with
verified PPS routing and a power budget; it cannot silently discipline BLE.
