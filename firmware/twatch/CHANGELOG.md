# T-Watch Ultra changelog

## 0.1.3 — development, 2026-11-15

- Replace Instrument reception indicators with a calendar-style source aperture,
  centered PLL phase well and matching LoRa receiver-active jewel.
- Align mirrored apertures, captions and phase geometry; remove the red source
  selector triangle.
- Remove the red MIN annotation from the hold dial; lower-bound-only evidence
  parks unavailable instead of implying an exact sampled hold duration.

## 0.1.2 — development, 2026-11-14

- Refine Instrument with directional bevels, recessed concentric subdials,
  centered scale lettering and luminous pilot-style hands.
- Combine the source selector, PLL phase disc and LoRa receiver-active jewel;
  distinguish RC9 from BLE-derived sources.
- Change Instrument RMS to a linear 0 ns–10 µs dial with an over-range flag.
- Add release-resolved horizontal face swipes with persisted selection;
  preserve tap controls and consume the complete first wake gesture.

## 0.1.1 — development, 2026-11-14

- Refine Digital with corner-safe insets, chamfered instrument framing and MODE.
- Add olive Instrument with analog time, RMS and P99 hold dials, reception
  shutters, moon-like PLL and battery reserve. Persist either selected face.
- Default/minimum display sleep is 90 seconds; migrate older short settings.
- Keep presentation time through missing snapshots with a separate bounded local
  clock. Withdraw precision indicators during this display-only extrapolation;
  source authentication, timing leases and receiver behavior are unchanged.
- Host behavior and native render checks cover unknown states, limits, face
  selection, timeout and presentation-only continuity. Hardware precision and
  endurance remain unqualified.

## 0.1.0 — development, 2026-11-14

- Establish a separate Ultra V1 SX1262 target using the W12/N30 timing backend.
- Add a native 410 × 502 digital face, launcher, face selector and persisted
  brightness/display-timeout settings.
- Separate display QSPI from passive LoRa SPI, use native USB diagnostics and
  keep GNSS off. Preserve runtime BLE discovery and authentication fences.
- Add factory/session/ticket-bound USB ROM entry after the original bounded
  maintenance drain and complete USB acknowledgment.
- Installation, physical power behavior and timing qualification remain separate
  commissioning gates; no inherited precision or battery-runtime claim.

- First-device commissioning: app-only installation and full application readback,
  protected-prefix/tail preservation, native display rendering, authenticated BLE
  reception and active SX1262 RX observed. Software USB ROM entry and return
  passed. Physical touch/aesthetic review, fallback timing, precision and battery
  endurance remain unqualified. Initial display command validation failure and
  USB reset-path failure were retained and corrected.
