# P1 storage design model

This standard-library Python model supports review of the
[provisioning design](../../../docs/t096/provisioning-design.md). It does not
access hardware, generate real credentials or build/flash firmware. All test
keys and device IDs are synthetic. Original code: Jonathan Yantis, MIT.

Run from the repository root:

```sh
python3 -m unittest discover -s firmware/t096/provisioning -p 'test_*.py' -v
```

The tests exercise first enrollment, rotation and erase across every modeled
payload-write boundary, commit-byte boundary and cleanup-word boundary. They
also flip every individual payload/commit bit in a newer committed record while
an older record is present, and test schema, identity and generation rejection.

Snapshots model allowed outcomes: old configuration before commit (after cleanup),
new configuration after commit (after cleanup), or no service. Malformed committed
records quarantine even if this reduces availability. Partial erase is modeled
as sequential word erasure for fault probing; actual flash physics, driver error
semantics, timing, wear, concurrency and power-loss behavior require separate
implementation and hardware tests. No cryptographic tamper resistance is claimed.
