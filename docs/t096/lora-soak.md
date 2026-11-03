# Autonomous passive LoRa reception soak

Copyright (c) 2026 Jonathan Yantis. MIT.

This read-only harness measures scheduling, reception and relative timing after
separately reviewed installation. It neither enables RF nor changes firmware,
roles, faults or clocks. The [wire3 design](lora-protocol.md) defines matched
slow 60/300 s receivers, one designated GM-profile emitter, source bounds,
RAM-only accounting and diagnostic fields. Other GMs can supply BLE time without
becoming LoRa emitters. Bind verified factories privately; no cross-boot airtime,
range or FCC compliance follows from this test.

Create an ignored private configuration using `console_capture.py` exact-device
fields from installed, readback-verified receipts. These placeholders fail validation:

```json
{"devices":{"CLOCK":{"usb_serial":"verified serial","silicon_id":"16 lowercase hex digits","source":"64 lowercase hex digits","commit":"40 lowercase hex digits"}}}
```

The collector resolves USB serials, checks callout/TTY ownership, takes exclusive
unit/descriptor locks and verifies unit/factory/source/commit/boot. CRC, sequence
and command identity remain checked throughout. Workers serialize ownership
inspection within one 8 s deadline; unexpected ownership, disconnect, reset or
identity change ends capture. No competing console reader is permitted.

```sh
python3 firmware/t096/tools/lora_soak.py \
  --config artifacts/local/lora-soak-devices.json \
  --unit SOURCE --unit CLIENT --receiver CLIENT \
  --max-rms-ns 10000 --duration 604800 \
  --out artifacts/lora-soak-01 --detach
```

Include the designated TX: receiver-only captures cannot exclude manual UTC
jobs between polls. The 10 µs RMS threshold is an operator example, not an accuracy
specification. Duration includes stabilization: default seven days, maximum 14;
`--stabilize-timeout` separately defaults to two hours. `--detach` returns a PID
and log. Keep the Mac awake; sleep/USB loss remains a gap. `kill -TERM PID`
closes capture as STOPPED, never a completed PASS.

Compact `lora-diag` polling runs every 2 s; detailed LoRa, clock, precision, network
and status snapshots run every 30 s. Continuous draining retains unsolicited
messages. First counter transition and first terminal fault each trigger one
bounded 12 s diagnostic burst through the same owned console. Neither clears
state. Display ACQ/HOLD, visibility/progression, host gaps and drawn-TZ retention
are reported separately; sampled availability cannot establish continuous uptime.

Each required receiver needs 600 real continuous seconds, three advancing
ordinary META boundaries in one source/capture context, advancing accepted UTC,
native training spanning 180 s within the chosen RMS limit, and no missed/late/error
advances or host gaps. Slow cadence is independently bounded at 62.51 s;
120 s hold and 420 s META lease cannot excuse a missed pair. Startup failures and
TX errors restart continuity. A serial jump cannot substitute for elapsed time
or a missed boundary. Every required receiver must pass before SOAK begins.
Later violations remain counted even after recovery.

A fresh matching TX boot/emitter is required. `manual_jobs` admission invalidates
the window and `manual_active` blocks qualification; receiver progress must pass
the latest `manual_serial`/`manual_stream` with an ordinary META before a new
600 s/three-META gate. [Manual UTC jobs](lora-utc-diagnostic.md), simulated aging
and advancing display-only ACQ/HOLD never qualify fresh service. Real failover
changes context and restarts fitting/gating; retain it separately from steady
source results.

`live.json` refreshes atomically; `samples.csv` retains metrics and
`failures.jsonl` discontinuities. Raw/JSONL files rotate at 16 MiB without deletion.
`summary.json` retains final capture status, receiver gates and violations.
Default disk allowance is 2 GiB with 1 GiB free reserve; exhaustion stops capture
and releases descriptors. Preserve failures, missing intervals and denominators.

PASS_RELATIVE_RECEPTION_SOAK requires the full requested duration, clean completed
captures, current qualification and zero post-gate violations. Native RMS is
an overlapping relative fit residual, not independent UTC/output error. Early
stop remains STOPPED even after an initial gate passes. No range or absolute
accuracy result follows. Dated outcomes belong in the
[changelog](../../firmware/t096/CHANGELOG.md).

Offline checks operate no hardware:

```sh
python3 firmware/t096/tools/lora_soak_tests.py
python3 firmware/t096/tests/test_console_capture.py
python3 firmware/t096/tools/lora_ram_tests.py \
  --deps artifacts/deps --out artifacts/lora-ram-host-01
python3 firmware/t096/tools/lora_display_holdover_tests.py \
  --deps artifacts/deps --out artifacts/lora-display-holdover-01
```
