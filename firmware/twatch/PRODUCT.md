# T-Watch Ultra

<!-- impeccable:product-schema 1 -->

## Platform
Embedded ESP32-S3; native 410 × 502 touchscreen, not a web or mobile OS app.

## Users
Jonathan wears and demonstrates a precision mesh watch. Time must be legible at a glance; diagnostic detail must remain accessible.

## Product Purpose
A daily watch with distinctive Digital and mechanical Instrument faces, authenticated BLE timing and continuous passive LoRa fallback.

## Stack
Existing W12/N30 C++ timing backend; new native rendering, touch, settings and board support. No Arduino radio replacement.

## Capabilities and Constraints
Boot into the last selected face. Digital is Casio-inspired; Instrument uses an olive mechanical dial with directional bevels, recessed wells and outlined luminous pilot hands inside a squarish case. Its calendar bridge places separate source and RX apertures beside a concentric PLL phase well, keeping RC9 distinct from BLE; Sync RMS spans linear 0ns–10us with an OVER flag. Show source, internal Sync RMS, PLL lock and sampled P99 sub-1us hold duration with honest unavailable/stale states. Launcher opens settings and face selection. Release-resolved horizontal swipes also switch and persist either face; the waking gesture is consumed, and MODE taps remain available. Face selection, brightness and 90/120/180-second display timeout persist separately from credentials. Display-only local hold can continue time while withdrawing source and precision; it never changes timing authority. GNSS remains off in revision one. No inherited hardware precision qualification. Runtime mesh selection is discovered and freshly authenticated after context changes.

## Open decisions
The installed SX1262 module is confirmed. User confirmed earlier physical Digital rendering and approved its corner/chrome refinement plus the olive mechanical Instrument direction. The replacement calendar bridge is preview-only and has not been installed in this task. Its physical appearance, measured power and timing qualification remain open. No new global design authority is established for other repository targets.
