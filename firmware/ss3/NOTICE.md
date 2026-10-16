# SS3 source notices

Original migration tooling and documentation: Copyright (c) 2026 Jonathan Yantis,
MIT. Selected source files retain their inherited Apache-2.0 SPDX notices;
those notices are not replaced by the repository default. A complete
redistribution/licensing reconciliation remains a public-release gate, consistent
with the T096 and W12 imports.

`main/g1_hmac.c` retains Intel/TinyCrypt's BSD-3-Clause notice and the documented
fixed dummy-buffer initialization change. ESP-IDF, NimBLE and TinyCrypt are
obtained separately from pinned public upstream sources under their own terms.
No SDK, private history, enrolled firmware or raw laboratory captures are
redistributed. The closed legacy include graph supplies compatibility display
and GNSS types; it does not enable a GNSS source or other radios.

The import manifest records exact selected paths, original hashes, public hashes
and transformations. Original identities in presentation/test fixtures were
replaced by synthetic IDs or explicit local configuration. Hardware roles and
historical source documents remain separate from operating authorization.
