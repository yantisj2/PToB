# W12 RC9 functional time service

Copyright (c) 2026 Jonathan Yantis. MIT.

Version0.2.4 optionally receives and serves the separate [RC9 wire](../../firmware/w12/main/rc9/README.md).
A W12 selecting RC8 LoRa drains its normal RC7 publisher before advertising
RC9 on SID14/15 and service UUID09. It never labels LoRa time RC1/RC2, trains
normal G1 from RC9, recascades RC9, or becomes GM. The same sole controller owner
retains transient GATT admission, handle quarantine and original2s teardown.

An authenticated direct child requests at most360s and renews at300s. The
original lease pins RC8 selection and AUTO reception through healthy BLE
recovery and temporary RC8 gaps. Gaps publish explicit withdrawal without
restarting source expiry. When the final lease expires, ordinary BLE recovery
policy resumes. Explicit KEEP OFF, owner fault and maintenance override demand.
A stale display snapshot withdraws time but cannot shorten or extend the original
client lease. Server/client restart requires a new boot-bound exchange.

Packets retain the actual RC8 edge, original12s hold and UTC envelope. Missing
companion boots/stream/quality stay unknown. Authentication establishes group
membership; pre-companion RC8 does not establish the complete emitter identity.
RC9 is functional UTC with unknown queue, radio delay and total precision.
Its upstream link RMS is not the BLE hop precision or an absolute UTC bound.

Build with `--rc9` plus the fine-clock, BLE100 estimated-relay owner and LR2021
options. These are separate services despite sharing the controller owner.
`rc9-parent <exact16hex|auto>` is a volatile, original2s rebind. `rc9 diag` is a
separate ten-row frame under the unchanged nonce, CRC, boot, source, build and
1.5s response deadline; normal status retains its nine-row layout. Its lease
and OLED second-boundary timestamps support bounded local diagnosis.

Offline checks:

```sh
python3 firmware/w12/tools/rc9_tests.py --idf artifacts/esp-idf --out artifacts/rc9-core
# Supply current G1 codec objects from the host suite:
IDF_PATH=artifacts/esp-idf python3 firmware/w12/tools/rc9_native_tests.py \
  --objects artifacts/g1-tests --out artifacts/rc9-native
python3 firmware/w12/tools/dual_display_tests.py --out artifacts/dual-display
```

These use real authentication codecs and the actual native controller adapter
with synthetic SDK callbacks. They are not RF interoperability or precision
qualification. Preserve failed hardware populations and exact-image records.

Version0.2.5 distinguishes an actual outgoing admission from a speculative
upstream retry. An unavailable BLE parent cannot starve RC9's connectable
capability advertisement. Actual admission, ACL ownership, quarantine and the
original drain still pause/refuse new clients. `service_flags` is the sum of
configured=1, periodic=2, SyncInfo discovery=4 and connectable capability=8;
normal RC7 status remains zero while profile9 owns the publisher.
