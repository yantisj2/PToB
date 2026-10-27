# W12 source notices

The shared W12/N30 GM successor consumer includes a selected MIT GM7 revision-3
codec snapshot from this repository's T096 source. Its exact selection and
hashes are recorded in [the snapshot provenance](main/gm7/README.md).

Original migration tooling and documentation: Copyright (c) 2026 Jonathan Yantis,
MIT. Selected original source files retain their inherited Apache-2.0 SPDX
notices, matching the T096 extraction policy; those notices are not replaced by
the repository default. The [full Apache-2.0 text](../../LICENSES/Apache-2.0.txt) is included. A future
firmware binary needs a separate linked-dependency redistribution review.

`main/g1_hmac.c` retains Intel/TinyCrypt's BSD-3-Clause notice and its documented
fixed dummy-buffer initialization change. ESP-IDF, NimBLE, TinyCrypt and other
SDK components are obtained separately through pinned public upstream sources
under their own terms. No SDK, private history or private enrolled image is
redistributed. See `imports/2026-10-16.json` for exact selected original bytes and
public transformations; imported historical documents are not operating authority.

Optional LR2021 builds link public RadioLib 7.8.1, Copyright (c) 2018 Jan Gromeš,
MIT. Its complete upstream license is retained beside the dependency sources
in each local image artifact. See the pinned release in dependencies.lock.json.
Selected original RC8 and N30 receiver/policy imports are recorded in
rc8-imports.json; original code is MIT, Copyright (c) 2026 Jonathan Yantis.
