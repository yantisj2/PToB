# Changelog

Copyright (c) 2026 Jonathan Yantis. MIT.

## 16 October 2026 — timing-chain stability and continuity

This update integrates T096 **0.15.69**, W12 **0.1.7**, and the macOS client
continuity changes. It is an experimental source update. The
[engineering results](evidence/stability/2026-10-16/README.md) retain the failed
and incomplete populations alongside the bounded successes.

- T096: improve cold admission, connection cancellation and established-link
  diagnostics; reserve native controller RX headroom under mesh load. Request
  100 ms RC discovery, gate every RC advertisement on usable upstream precision,
  and reduce failed steady control attempts without slowing the 16 Hz precision
  train. Legacy 300 s renewal and 360 s admission lease remain.
- T096: retain a separately labelled local clock estimate through source loss;
  preserve original authentication, source expiry and downstream authority gates.
  Move USB projection and serialization outside the interrupt-masking map lock,
  with a generation check that refuses raced observations. Add bounded native
  receive-outcome, transition, watchdog and retained-fault diagnostics.
- macOS: display HOLDOVER / LOCAL ONLY with observation age and unknown error;
  preserve the owned chrony process across compatible quality loss while pausing
  measurements. Source/counter changes still fence training. Reuse USB buffers,
  coordinate readers with a per-silicon kernel lock, and rediscover after stale
  transport errors. Add opt-in two-hour capture and ChronyControl observation.
  The README includes a user-supplied ChronyControl tracking screenshot with
  unresolved spikes preserved and attribution to Bryan Christianson.
- W12: align RC control/admission behavior, retain an OLED-only HOLD estimate,
  and add bounded receive-loss diagnostics. Isolate large status snapshots and
  enforce a static main-stack budget after rejecting a stack-overflowing
  candidate. See the [W12 changelog](firmware/w12/CHANGELOG.md).
- Preserve earlier W12 GNSS indicator/factory-binding repairs and the SS3 relay
  display fix in the integrated history. W12 remains RC-only with no USB timing
  relay; longer negotiated leases and quality-bearing BLE holdover are deferred.

The controller RX refusal was localized and passed bounded recovery checks.
TC-02's brief v69 USB check had no request errors. TC-04 still failed with
persistent USB/CDC nonresponse; v68 also retained controller assertions and MCU
restarts. The new USB lifecycle investigation is a proposal, not an applied fix.
These results do not establish continuous delivery, qualified HOT failover,
metrology readiness or independent UTC accuracy.
