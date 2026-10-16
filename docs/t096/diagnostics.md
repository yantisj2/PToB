# T096 sync diagnostics

Copyright (c) 2026 Jonathan Yantis. MIT.

Version 0.15.56 adds a production RAM event journal for both GM and RC. Earlier
versions expose `network`, `precision`, `rc-parent`, `clock`, `join`, health and
watchdog snapshots, but do not retain a complete sequence of sync transitions.
Fixture `printk` traces are not the USB console in ordinary builds.

## Events and interpretation

The journal holds the newest 32 events (1,280 bytes of records). It survives
logical owner restarts within one MCU boot. It does not survive a power loss or
hardware reset. Callbacks copy one fixed record; main formats and exports it.
There is no flash logging, allocation, RF operation or recovery action in this
observer. A slow reader can lose history: `overwritten`, per-cursor `missed` and
sequence exhaustion are explicit. Sequence numbers never wrap within a boot.

The read-only, source/silicon/boot-bound command is:

```text
r2 REQUEST8 SILICON16 SOURCE64 BOOT16 events CURSOR8
```

Use cursor `00000000` to read the oldest retained event, then use its `sequence`
for the next request. The ordinary CRC-framed reply includes `found`, `last`,
`overwritten`, `exhausted`, `missed`, `sequence`, 64-bit uptime `ms`, `kind`,
signed `result`, `peer`, `detail`, `a` and `b`. A cursor belongs to one exact
source and MCU boot; start a new segment after either changes. CRC checks
transport integrity, not authenticity or timing accuracy.

| Kind | Meaning | detail / a / b |
| --- | --- | --- |
| 1 / 2 | State transition / 30-second unchanged-state heartbeat | flags / accepted / rejected |
| 3 | Root or selected scope changed | source epoch / root boot low / root boot high |
| 4 | Parent selected | relay flag / signed RSSI encoded as uint32 / reserved |
| 5 | Periodic-sync create result | SID / supervision timeout in 10ms units / reserved |
| 6 | Periodic sync established | SID / acquisition age ms / reserved |
| 7 | Periodic sync terminated; result is HCI reason | was established / cancel count / termination count |
| 8 | Sync cancel result, including deadline failure | was established / acquisition age ms / reserved |
| 9 / 10 | RC create / connected result | transport phase / attempt age ms / attempt count |
| 11 / 12 | RC operation error / local close request | phase or close cause / attempt age ms / attempt count |
| 13 | RC disconnected; result is HCI reason | phase / attempt age ms / server flag |
| 14 | GM admission result | source flags / causal proof round us / coarse uncertainty us |
| 15 | First owner fault | CRC32 of source reason string / reserved / reserved |
| 16 / 17 / 18 | GM connected / disconnected / MTU error | slot and flags / incarnation / reserved |
| 19 / 20 | Held map expired / accepted discipline resumed | count / discipline gap us / reserved |

State flag bits, starting at zero: active, RC profile, established PA sync,
creating PA sync, map valid, locked, authorized, serving GM, follower, closing,
profile change pending, phase acquisition. A locked state is a software
indication. GM health uses source eligibility, publication and capture progress;
the RC lock bit is not a GM health criterion.

RC transport phases: 0 idle, 1 connecting/waiting for callback, 2 MTU, 3 capability
or GM marker, 4 write, 5 reply, 6 GM request, 7 GM proof, 8 close. An ATT callback
error packs its callback kind in the upper 16 bits of `detail`; the lower bits
retain the phase. Close causes: 1 completed/aborted exchange, 2 quiet window,
3 acquisition yield, 4 demand rejection, 5 shutdown drain, 6 original attempt
deadline. A local close request does not prove it caused a later disconnect.
GM admission result codes: 1 codec, 2 exchange, 3 non-root, 4 coarse-time gate,
5 accepted. These diagnostic codes grant no authority.

Main also emits `#R2_TIMING {JSON}` notes when DTR is asserted and the console
queue is idle, at most ten notes per second. Backpressure retains the streaming
cursor until a note is queued; USB reset can still drop a queued note. Use the
framed cursor query to recover retained events. A transmitted note is not proof
of host receipt. Busy sync-create errors are rate limited to one per second.
Continuous collection is necessary: the ring is a recent-history buffer, not a
long-term recorder.

## Console capture

The standalone macOS collector needs Python 3, `ioreg` and `lsof`; it has no
private-checkout dependency. Create an ignored, owner-only configuration with
your actual USB serial, silicon, exact installed source and commit:

```json
{"devices":{"PToB0":{"usb_serial":"YOUR_USB_SERIAL","silicon_id":"0123456789abcdef","source":"REPLACE_WITH_64_LOWERCASE_HEX_DIGITS","commit":"REPLACE_WITH_40_LOWERCASE_HEX_DIGITS"}}}
```

```sh
python3 firmware/t096/tools/console_capture.py \
  --config artifacts/local/console-devices.json --unit PToB0 \
  --duration 3600 --out artifacts/console-run-01
```

Repeat `--unit` to capture a known cohort concurrently. This issues only status,
network, precision, parent-status, clock, join and event queries. It performs a
fresh USB identity/ownership scan before each query while draining serial on its
owning thread. It takes the unit lock and exclusive descriptor ownership, refuses
an existing client and ends its segment on disconnect, identity/boot mismatch or
three unanswered queries. It never stops another process. Original maintenance
and update guards remain necessary for flashing.

Raw bytes and timestamped JSONL rotate at 16 MiB by default; `--rotate-bytes`
changes that size. The collector retains host UTC and monotonic receive times,
CRC failures, timeouts, partial tails, event gaps and final closure status.
Capture artifacts stay under ignored `artifacts/`. Protect raw logs and real
identity configurations; do not add them to the repository. Earlier firmware is
sampled through its existing snapshots; it cannot supply the new event history.
The deployed command protocol requires increasing request IDs across console
owners; the collector uses the established millisecond seed and stops on
exhaustion. A future host-session handshake should remove this limitation.

Host receive times correlate observations; they do not measure RF timing,
independent output accuracy or UTC error. Query load can perturb scheduling.
Retain polling rate, missing intervals and diagnostic drops in any run analysis.

For an explicitly requested background run, `tools/console_background.py` uses
the same arguments and defaults to seven days, with hourly segments. It starts
no timing daemon and never resets hardware. A disconnect ends the current
segment; after proved descriptor closure and a 15-second backoff it starts a new
segment with a fresh USB identity/ownership scan. Gaps, errors and boot changes
remain in the segment journal. `run.json` records the owned PID, and SIGTERM
requests a graceful stop. Unknown closure stops the supervisor. A detached run
does not survive a host reboot; keep the host awake for continuous collection.

## Recovery changes and acceptance

Cold RC admission requests 15ms ACL events, keeping same-scope renewal at 50ms.
This gives the dependent JOIN exchanges more opportunities to meet the unchanged
100ms initial coarse-uncertainty gate. Each attempt randomizes a finite scan
interval (50–100ms) and window (10–20ms), preserving the split controller's
non-continuous scan path and original two-second attempt deadline. This reduces
repeated phase collisions; it does not guarantee reception.

An authenticated direct JOIN from a non-root ends that locator's acquisition
trial and defers it for 60 seconds. G1 discovery alone still grants no role or
time authority. Later promotion can be retried after that bounded cooldown.
Source expiry, authentication, replay fences, corrected-capture association,
500ms held-map limit, PA supervision and proven controller drains remain in force.

Durable-chain acceptance requires extended GM and RC capture together, including
late admission while other clients remain locked; source loss and return; root
promotion and reboot; relay path changes; hours of renewals; event-counter and
host-request limits; console saturation; and unsuccessful recovery attempts.
Separate availability, corrected delivery, fit quality and independent output
metrology. Report failure/recovery durations and denominators, not just final
lock state. Keep a working GM's periodic train running during individual client
recovery; a global GM restart is a last-stage recovery, not the normal cure.

Offline regressions:

```sh
python3 -m unittest discover -s firmware/t096/tests -p 'test_console_capture.py' -v
CC=clang python3 firmware/t096/tools/rc_selection_tests.py --out artifacts/selection-check-01
python3 firmware/t096/tools/test.py --deps artifacts/deps
```

Zephyr's [periodic-sync API](https://docs.zephyrproject.org/latest/doxygen/html/group__bt__gap.html)
does not impose an acquisition timeout. The application's bounded acquisition
and [supervision timeout](https://docs.zephyrproject.org/latest/doxygen/html/structbt__le__per__adv__sync__param.html)
are separate controls. A passing host test or build does not qualify long-term
hardware reliability.
