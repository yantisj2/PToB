# N30 RC9 direct client

Copyright (c) 2026 Jonathan Yantis. MIT.

Version0.2.25 adds a direct client for the distinct W12 RC9 service. The
[wire1 contract](../../firmware/w12/main/rc9/README.md) carries functional RC8
UTC snapshots over one BLE hop, with UNKNOWN transport and output uncertainty.
The N30 does not publish or relay RC9. T096 receivers remain unchanged.

The selected source order is settled normal BLE, fresh direct LoRa RC8, then
fresh RC9. RC9 never trains the normal BLE clock or inherits its fine-lock,
relative RMS or relay permissions. The clock and screensaver label it `RC9`;
the Precision page identifies RC9 UTC with unknown precision. Original RC8
boots may be absent and stay unknown. Upstream link RMS is exposed only as
an RC8 diagnostic, not RC9 delivery accuracy.

A fresh authenticated RC9 snapshot resets the no-time watchdog until its
original deadline. Polling, metadata and lease renewal cannot extend the
source hold. Withdrawal, expiry, fault, OFF and maintenance remove time.
The screensaver retains its existing500ms total-loss exit and15-minute minimal
transition.

Ticket-bound `rc9-parent <exact-factory-id>` selects a direct publisher;
`rc9-parent auto` restores automatic selection. These commands use the existing
source-change/drain owner. `rc9 diag` returns a nonce/CRC-bound ten-row diagnostic
frame with source IDs, boots, original local expiry, upstream assurance/authority
and independent RC9 counters. Use a runtime manifest with `n30_ui_revision=5`;
for this diagnostic response set `n30_frame_kind=RC9`.

```sh
python3 firmware/n30/tools/tests.py --out artifacts/n30-rc9-ui-tests
python3 firmware/w12/tools/rc9_tests.py --idf artifacts/public-review/esp-idf \
  --out artifacts/n30-rc9-core-tests
```

Offline checks and native image identity are separate from exact-device
installation and live RC9 reception. No new precision qualification is implied.
