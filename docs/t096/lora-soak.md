# Autonomous passive LoRa reception soak

Copyright (c) 2026 Jonathan Yantis. MIT.

The read-only harness runs without an active agent. Prepare and review the slow
firmware first, then separately install and verify the exact devices before
starting it. It does not flash, reset, clear faults, select profiles, enable RF,
reconnect after a device reset or discipline a host clock. It tests scheduling,
reception and relative timing in the existing lab antenna setup. It is not a
range test or independent UTC/output calibration.

The intended lab roles are TC-01 primary GM, TC-02 backup GM and TC-03/TC-04
LoRa clients. Bind the verified factory identities privately at build time;
use GM defaults for the first two and RC defaults for the clients. All normal
GM/RC slow images request RX at boot. Backup TX requires a qualified explicit
GM7 handoff; a persistent TX fault must be resolved through its separate guarded
recovery process. Production admission preserves all earlier fast airtime debt,
so a newly booted transmitter with more than72s debt can remain silent for a
full hour. The harness must not interpret that interval as passed reception.

Create a private configuration under ignored `artifacts/`. Each selected unit
has the same exact-device fields used by `console_capture.py`:

```json
{"devices":{"CLOCK":{"usb_serial":"verified serial","silicon_id":"16 lowercase hex digits","source":"64 lowercase hex digits","commit":"40 lowercase hex digits"}}}
```

Use the source/commit from the installed, readback-verified candidate receipt.
The example placeholders intentionally fail validation. The collector freshly
resolves USB serials, checks both callout and TTY descriptor ownership, takes a
unit lock and exclusive descriptor, then checks runtime unit/factory/source/
commit/boot identity. CRC, response sequence and command identity remain checked
throughout. Disconnect, reset or identity change ends the run for review.

```sh
python3 firmware/t096/tools/lora_soak.py \
  --config artifacts/local/lora-soak-devices.json \
  --unit TC-01 --unit TC-02 --unit TC-03 --unit TC-04 \
  --receiver TC-03 --receiver TC-04 \
  --max-rms-ns 10000 --duration 604800 \
  --out artifacts/lora-soak-01 --detach
```

The10us example RMS threshold is an operator choice, not an established
acceptance requirement. Set the agreed threshold explicitly. Duration means
total capture time including stabilization: seven days by default, maximum
fourteen. Initial stabilization has a separate two-hour timeout
(`--stabilize-timeout`), allowing debt aging without an unbounded wait for the
gate. `--detach` returns a PID and launcher log;
the process survives the chat ending. Keep the Mac powered and awake; sleep
or USB loss is retained as a gap/failure. Send `kill -TERM PID` for a clean stop.
Stopping early returns STOPPED, never a passing completed soak.

Every two seconds, `lora-diag` records original accepted event UTC/ticks,
source/capture context, META serial, pair counts, native fit RMS, expiries and
TX/RX errors. Detailed LoRa, clock, precision, network and status snapshots run
every30s. Continuous serial draining retains firmware messages between polls.
No fast periodic event-journal polling is added. The normal display refresh,
BLE source deadlines and controller priorities remain independent.

Each required receiver must observe at least600 real continuous seconds and
three advancing META boundaries in one authenticated source/capture context,
advancing accepted UTC events, no missed/error counters or host gaps, and native
training spanning at least180s within the configured RMS limit. Startup
acquisition failures remain in the journal and restart that gate. A serial jump
cannot substitute for elapsed time or a missed boundary. After both receivers
pass, the harness marks SOAK and continues autonomously. Later failures remain
counted even if service recovers; a recovered display cannot erase them.

`live.json` is atomically refreshed, `samples.csv` retains sampled timing
metrics, and `failures.jsonl` records discontinuities. Per-device raw/JSONL
files rotate at16MiB without deleting older evidence. `summary.json` records
final capture status, all receiver gates and post-gate failures. The default
total capture budget is2GiB with1GiB free-space reserve; exhaustion stops the
run and releases descriptors instead of deleting history. Run with larger
explicit budgets if the intended duration and observed data rate require it.

A PASS_RELATIVE_RECEPTION_SOAK requires completed clean captures, the initial
gate, current qualification and zero post-gate violations. Native RMS values
are overlapping relative link-fit residuals, not independent samples of UTC
error. Counter totals and missing intervals stay alongside them. Failover
changes context and restarts training/gating; preserve that result separately
from a steady-source soak. No absolute precision or field-range claim follows.

Offline checks operate no hardware:

```sh
python3 firmware/t096/tools/lora_soak_tests.py
python3 firmware/t096/tests/test_console_capture.py
python3 firmware/t096/tools/lora_slow_tests.py \
  --deps artifacts/public-review/t096-deps --out artifacts/lora-slow-host-01
```
