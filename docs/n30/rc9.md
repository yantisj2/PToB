# N30 RC9 receiver and LoRa fallback relay

Copyright (c) 2026 Jonathan Yantis. MIT.

The [RC9 wire contract](../../firmware/w12/main/rc9/README.md) carries functional
RC8 UTC snapshots over one BLE hop. Transport and output uncertainty are
unknown. N30 receives RC9 on every RC profile. A BLE100 build with
`--estimated-relay --lora-rx` can publish TIME v2 from its own current
authenticated passive LoRa input while qualified normal BLE is absent.
The publisher uses the original source deadline and withdraws time and
admissions on loss, withdrawal or expiry. Received RC9 is a terminal source.

The selected source order is settled normal BLE, fresh direct LoRa RC8, then
fresh RC9. RC9 never trains the normal BLE clock or inherits its fine-lock,
relative RMS or relay permissions. The clock and screensaver label it `RC9`;
the Precision page identifies RC9 UTC with unknown precision. Original RC8
boots may be absent and stay unknown. Upstream link RMS is exposed only as
an RC8 diagnostic, not RC9 delivery accuracy.

A fresh authenticated RC9 snapshot interrupts an armed no-time interval until
its original deadline. Learned boot information or fresh healthy LoRa reception
can leave the destructive absence timer disarmed, as described in the
[target guide](../../firmware/n30/README.md). Polling, metadata and lease renewal cannot extend the
source hold. Withdrawal, expiry, fault, OFF and maintenance remove time.
The screensaver retains its existing500ms total-loss exit and15-minute minimal
transition.

Ticket-bound `rc9-parent <exact-factory-id>` selects a direct publisher;
`rc9-parent auto` restores automatic selection. These commands use the existing
source-change/drain owner. `rc9 diag` returns a nonce/CRC-bound ten-row diagnostic
frame with source IDs, boots, the local deadline, upstream assurance and
authority, and independent RC9 counters. Server builds also report the child
lease and service flags. Use a runtime manifest with `n30_ui_revision=5` and
the build receipt's `n30_rc9_server` value. For this diagnostic response set
`n30_frame_kind=RC9`.

```sh
python3 firmware/n30/tools/tests.py --out artifacts/n30-rc9-ui-tests
python3 firmware/w12/tools/rc9_tests.py --idf artifacts/public-review/esp-idf \
  --out artifacts/n30-rc9-core-tests
```

Offline checks and native image identity are separate from exact-device
installation and live RC9 reception. No new precision qualification is implied.
