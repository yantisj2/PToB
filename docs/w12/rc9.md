# W12 RC9 functional time service

Copyright (c) 2026 Jonathan Yantis. MIT.

Version0.2.4 optionally receives and serves the separate [RC9 wire](../../firmware/w12/main/rc9/README.md).
A W12 selecting RC8 LoRa drains its normal RC7 publisher before advertising
RC9 on SID14/15 and service UUID09. It never labels LoRa time RC1/RC2, trains
normal G1 from RC9, recascades RC9, or becomes GM. The same sole controller owner
retains transient GATT admission, handle quarantine and original2s teardown.

An authenticated direct child requests at most360s and renews at300s. The
original lease keeps RC8 reception and RC9 TIME available through BLE recovery
and temporary RC8 gaps. Version0.2.14 gives qualified normal BLE the local
display immediately and stops new RC9 capability admissions and renewals.
Requests queued before recovery cannot extend a lease afterward. Existing
children retain their original lease while seeking normal BLE independently;
after the final lease expires, the actual publisher drains and returns to RC7.
Loss of BLE qualification permits fallback admissions again. Gaps publish
explicit withdrawal without restarting source expiry. Explicit KEEP OFF,
owner fault and maintenance override demand.
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

## AUTO client return to normal BLE

W12 0.2.11 and N30 0.2.28 separate the server's original RC8 demand lease
from the client's source selection. A valid automatic RC9 client passively
re-observes normal sources for 1.5 seconds every 30 seconds, using the existing
discovery scheduler. An unavailable RC9 snapshot already permits discovery.
A fresh normal G1 or RC7 locator may request admission under the existing
rate, cooldown and ownership bounds; a remembered locator cannot do so.

RC9 remains available through failed admission, NO_SOURCE and indirect root
hints, subject to its original sample and lease expiry. Only a successful
normal admission retires the RC9 client and drains its old periodic owner.
JOIN supplies coarse replay context, not a fine clock. New physically matched
normal observations must build the normal clock independently. Explicit
RC9 and normal-source pins retain their meaning. The server's existing lease
is neither shortened nor renewed by a client returning to BLE.

Earlier AUTO clients treated the six-minute RC9 lease as exclusive ownership
and stopped scanning while RC9 time remained valid. A newly healthy mesh
could remain hidden until a renewal or expiry. Native regressions cover
discovery during valid fallback, return during valid/expired time, NO_SOURCE,
indirect hints, explicit pins and actual owner teardown. These checks are
synthetic controller-boundary tests, not RF or precision qualification.
