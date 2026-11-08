# ESP32-REF changes

## Example 02 — Acquisition recovery

- Keep JOIN replay scope while waiting for fresh GR3 pairs. Signed packet
  expiry still controls delivered UTC, and unmatched reports cannot extend
  the acquisition timeout.
- Keep the OLED on one waiting or recovery view while radio stages retry.
- Refresh the causal UTC bound in the background while GR3 tracking continues.
- Bound each JOIN attempt and reacquire if refresh fails for 75 seconds.

## Example 02 — GM-sourced UTC

- Get startup UTC and freshness from the grandmaster over authenticated
  JOIN5. Use passive GR3 for clock tracking.
- Remove USB time input and update the W12 startup screen, copy recipe and
  shadow validation instructions. Keep bench measurements separate from the
  receiver.

## Example 02 — 7 November 2026

- Add a passive direct-GR3 receiver task, coarse UTC freshness window and
  opt-in POSIX discipline for existing ESP32-S3 applications.
- Add Basic, Refined and Fine descriptor profiles, a W12 Clock and Info
  display, and a separate bench validation plan.
- Add host tests for authenticated framing, exact packet pairs and expiry,
  and reproducible screenshots from the W12 renderer.

## 0.2.1 — 7 November 2026

- Add a GPIO-free 80 MHz MCPWM clock option for the same BLE SoftPLL.
- Exercise both clock rates and preserve timing windows in milliseconds.
- Report the selected local counter rate over USB. The BLE timestamp endpoints
  and wire protocol are unchanged.
- Clarify the W12 clock and Info screens, and add renderer screenshots to the
  lesson. Keep source loss, phase backlog, and shutdown actions distinct.

## 0.2.0 — 6 November 2026

- Replace the planned wired lesson with a wireless relative-time SoftPLL.
- Add a shared C controller, separate authenticated example protocol, fresh
  session admission, exact event joins and causal prediction accounting.
- Add the ESP32-S3 controller descriptor/programmed-TX adapter and independent
  native builds with the shared firmware retention lifecycle.
- Add a W12 operating display, button debounce, bounded shutdown coordinator,
  safe OLED/bus/auxiliary-radio levels and release-guarded deep sleep.
- Add a two-follower loss/drift simulation, protocol/lifecycle/SDK-faked power
  checks, rendered previews and an offline exact-device review preparer.
- Hardware installation, measured RF/output precision and sleep current remain
  pending. The review preparer reads exact-device ownership and recovery records
  locally.

## 0.1.0 — 6 November 2026

- Introduce an independent firmware-examples collection and learning sequence.
- Add host-only clock fundamentals with deterministic counter, rate, phase,
  delayed-observation and expiry exercises.
- Add newly written C11 timing components, offline checks and source receipts.
- Hardware adapters, native images, BLE and automatic PLL training remain planned.

Initial host verification passed strict Clang C11 compilation, fatal
UndefinedBehaviorSanitizer checks and a standalone copy built from a path with
spaces. AddressSanitizer validation remains open: the instrumented lesson
stalled before output on the review host, including outside the sandbox; a
trivial `int main(void) { return 0; }` ASan control also timed out. Failed-run
receipts and diagnostic logs are retained locally. No hardware was accessed.

Copyright (c) 2026 Jonathan Yantis. MIT.
