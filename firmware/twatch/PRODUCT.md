# T-Watch Ultra

<!-- impeccable:product-schema 1 -->

## Platform
Embedded ESP32-S3; native 410 × 502 touchscreen, not a web or mobile OS app.

## Users
Jonathan wears and demonstrates a precision mesh watch. Time must be legible at a glance; diagnostic detail must remain accessible.

## Product Purpose
A daily watch with four distinctive faces: Digital, mechanical Instrument, Solar and Shadow, authenticated BLE timing and continuous passive LoRa fallback.

## Stack
Existing W12/N30 C++ timing backend; new native rendering, touch, settings and board support. No Arduino radio replacement.

## Capabilities and Constraints
Boot into the last selected face. Digital is Casio-inspired; Instrument uses an olive mechanical dial with directional bevels, recessed wells and outlined luminous pilot hands inside a squarish case. Its calendar bridge places separate source and RX apertures beside a concentric PLL phase well, keeping RC9 distinct from BLE; Sync RMS spans linear 0ns–10us with an OVER flag. Show source, internal Sync RMS, PLL lock and sampled P99 sub-1us hold duration with honest unavailable/stale states. Launcher opens settings and face selection. Release-resolved horizontal swipes also cycle and persist all four faces; the waking gesture is consumed, and MODE taps remain available. Face selection, brightness and 15/30/60/90-second display timeout persist separately from credentials. Display-only local hold can continue time while withdrawing source and precision; it never changes timing authority. GNSS main power is requested only while Shadow is awake and visible; leaving the face, opening an app, sleeping or entering maintenance turns it off while preserving backup power. No inherited hardware precision qualification. Runtime mesh selection is discovered and freshly authenticated after context changes.

Solar adds a fixed Santa Rosa Beach/Central daily sun path, calculated twilight and lunar phase, four compact circular subdials aligned to the case chamfers for RMS, P99, source identity/strength and approximate predicted tide progress. The clock medallion moves opposite the sun with date, Central time and lunar graphic inside its pale rim; eight sunrise/sunset and twilight crossings mark the inner orbit. The civil scale stays fixed at 12 top, 18 right, 00 bottom and 06 left; actual morning/evening event chords change the horizon and twilight height/tilt seasonally. Current Central civil time drives sun and clock position. Source strength is encoded in the ring, with only the identifier in text. Its ephemerides and PSRAM minute backdrop are cached; generation-aware tile damage preserves dynamic rendering. Unavailable tides dim the subdial and expired coverage says EXP. The user approved the solarpath.webp reference as a third face; it does not replace Digital or Instrument.

Shadow is an original Casio/Garmin-inspired GNSS comparison instrument, with graphite fields, pale segmented time, lime activity, actual satellite azimuth/elevation and C/N0 bars. Only satellites with known positions appear in its sky plot. Its physical PPS annunciator is independent of mesh time and its acquisition sweep. A phase-blind ten-second settling interval precedes signed MEAN, P99 ABS and smaller RMS/sample count; coarse histogram tails carry `~`, and missing evidence stays unavailable. Collection requires current selected fresh BLE/RC1/RC2 timing plus an independent GNSS fix/PPS reference. LoRa, RC9, HOLD and expired source evidence cannot display retained BLE statistics. Source/reference changes, PPS gaps and new visits restart collection. GNSS is a read-only observer with no mesh admission, servo, clock-authority or receiver-aid role, and never changes Solar's fixed location. The other three faces retain their established visual worlds.

## Open decisions
The installed SX1262 module is confirmed. User confirmed earlier physical Digital rendering and approved its corner/chrome refinement plus the olive mechanical Instrument direction. The replacement calendar bridge is preview-only and has not been installed in this task. Its physical appearance, measured power and timing qualification remain open. Solar previews are synthetic, with hardware appearance and performance unqualified. Shadow's native preview and finish review are complete; installed GNSS behavior, physical appearance, measured power and independent precision remain unqualified. No new global design authority is established for other repository targets.
