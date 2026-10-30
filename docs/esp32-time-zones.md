# W12 and N30 coarse time zones

Copyright (c) 2026 Jonathan Yantis. MIT.

W12 0.2.20 and N30 0.2.36 select the same [coarse TZ wire](../protocol/time-zone-v1/README.md)
as T096 0.15.206: GM7 revision 4, direct JOIN revision 5, and RC capability/control
revision 8. Earlier coarse revisions are rejected by this profile. G1/GR3/RC7
precision payloads are unchanged. Other derived targets retain their selected
profile; this port does not update SS3, CoreSE, macOS or T096.

Only a freshly authenticated selected parent supplies the current atomic pair.
Direct ANN updates require the admitted root/boot/epoch/train, increasing GM
sequence and the original coarse UTC replay envelope. Initial JOIN metadata
cannot replace a later ordered ANN. Within an admitted relay path, a higher
existing admission challenge permits a changed pair; an equal challenge renews
only an identical pair. The producer increments that challenge when its advertised
pair changes. Older or same-token conflicting packets cannot restore a past offset.
Explicit unknown withdraws local time, and later precision presence cannot
resurrect it. Parent/path replacement clears the old context.

Matching parent coarse advertisements or authenticated periodic presence renew
a lease of three nominal periods: 750 ms direct GM, 300 ms RC. Clock lock, a
fitted map and precision corrections do not control this display lease. Queued
receipt time sets its deadline; polling and invalid/foreign packets cannot
extend it. The GM owns location and seasonal-offset calculation; receivers
consume its new snapshot without a local database or schedule.

The front digital and analog clocks use that offset, with `CDT` for ID 2/-300,
`CST` for ID 2/-360, and `LOC` for other valid regional offsets. Unknown/expired
metadata shows UTC. N30's moving clock and minimal saver use the same label;
its retained pixel cache cannot keep local time past TZ expiry. W12's calendar
conversion uses the same signed offset. UTC models, PPS phase, diagnostics and
radio timestamps are preserved. LoRa and functional RC9 carry no TZ and display
UTC. The current GM may select regional offsets automatically; unsupported or expired
metadata still falls back to UTC.

RC publishers forward the current pair through authenticated coarse capability
and admission records. Precision packets receive no TZ fields. A valid unknown
future ID forwards unchanged; malformed TZ normalizes to unknown while otherwise
valid authenticated time remains usable. New metadata cannot grant timing authority.

Run offline checks with the pinned SDK and one LLVM Clang toolchain:

```sh
python3 firmware/n30/tools/gm_successor_tests.py --idf "$IDF_PATH" --tz --w12 \
  --out artifacts/w12-tz-owner
python3 firmware/n30/tools/gm_successor_tests.py --idf "$IDF_PATH" --tz \
  --out artifacts/n30-tz-owner
python3 firmware/n30/tools/time_zone_tests.py --objects artifacts/w12-tz-owner \
  --out artifacts/esp32-tz-renderers
python3 firmware/w12/tools/rc7_permission_e2e.py --sdk "$IDF_PATH" --tz \
  --out artifacts/esp32-tz-publisher
```

These execute actual codecs and receiver owners, compare the frozen public
vectors byte for byte, mutate every wire byte, check signed offsets and expiry,
and compare production-renderer pixels for local digital/analog time and saver
fallback. The ordinary W12 host and N30 suites remain applicable. Synthetic
previews and passing tests do not qualify RF delivery, endurance or UTC accuracy.

Private native build receipts record `time_zone_contract` and exact unchanged
per-unit configuration. `rc9 diag` exposes TZ ID, offset, deadline, learned-record
and conflict counts inside its existing alternate CRC frame; normal status rows
and byte limits are unchanged. Hardware installation and bounded observations
need exact-device identity, ownership, recovery, original application drain,
app-only bounds, actual application readback and protected-prefix/tail comparison.
