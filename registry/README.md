# Shared device display registry

Copyright (c) 2026 Jonathan Yantis. MIT.

[example.json](example.json) is the shared schema-1 example for T096, W12, SS3
and macOS. Every ID is synthetic. Keep a real deployment registry in an ignored
local file, then give the same file to each target's tooling. T096 and W12 consume it through their `build.py --registry` argument; SS3 and
macOS adapters remain part of their later migration. No live discovery or credentials are stored here.

The `devices` object maps each nonzero 16-digit lowercase device ID to `alias`
and `target`. Optional `uuid` is a separately assigned canonical 128-bit UUID;
it does not replace or derive the wire ID. Aliases may repeat. IDs and supplied
UUIDs must be unique. The validator rejects duplicate JSON keys, unknown fields,
unsupported targets, more than 128 entries and aliases outside 1–24 portable
ASCII display characters. This narrower display subset is not a wire-name change.

T096 and W12 compile the entire chosen map into read-only firmware. W12 uses
registry aliases, then a bounded hexadecimal fallback; unknown identity stays
unknown. For T096, authenticated,
non-default advertised names take precedence, then the local registry alias,
then the last four ID digits. An unknown identity remains unknown. Registry
entries confer no membership, role, election preference or source validity.
Never select a flashing destination by alias alone.

Run `python3 registry/test_registry.py` for schema and actual T096 UI lookup tests.
The build receipt binds the input map, generator and generated header. Enrollment
also binds the map hash into the watchdog build identity. Updating a map currently
requires rebuilding; mutable P1 provisioning is still a separate design.
