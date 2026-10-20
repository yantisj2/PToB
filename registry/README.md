# Shared device display registry

Copyright (c) 2026 Jonathan Yantis. MIT.

[example.json](example.json) is the shared schema-1 example for T096, W12, SS3
and macOS, plus the offline N30 target. Every ID is synthetic. Keep a real deployment registry in an ignored
local file, then give the same file to each target's tooling. T096, W12 and SS3 consume it through their `build.py --registry` argument. The
macOS client uses `sh clients/macos/build.sh --registry FILE` from the root. No live discovery or credentials are stored here.

The `devices` object maps each nonzero 16-digit lowercase device ID to `alias`
and `target`. Optional `uuid` is a separately assigned canonical 128-bit UUID;
it does not replace or derive the wire ID. Aliases may repeat. IDs and supplied
UUIDs must be unique. The validator rejects duplicate JSON keys, unknown fields,
unsupported targets, more than 128 entries and aliases outside 1–24 portable
ASCII display characters. This narrower display subset is not a wire-name change.

T096, W12 and SS3 compile the entire chosen map into read-only firmware; macOS
generates read-only Swift aliases. W12 and SS3 use
registry aliases, then a bounded hexadecimal fallback; unknown identity stays
unknown. For T096, authenticated,
non-default advertised names take precedence, then the local registry alias,
then the last four ID digits. An unknown identity remains unknown. Registry
entries confer no membership, role, election preference or source validity.
Never select a flashing destination by alias alone.

Run `python3 registry/test_registry.py` for schema and actual T096 UI lookup tests.
The build receipt binds the input map, generator and generated header. Firmware enrollment
also binds the map hash into T096’s watchdog identity and W12/SS3 runtime build
identities; macOS records its map in the local build receipt. Updating a map currently
requires rebuilding; mutable P1 provisioning is still a separate design.

## N30 arrival onboarding

The offline N30 target reserves N3-1 and N3-2 without inventing device IDs.
After each factory MAC is verified, the [N30 onboarding tool](../firmware/n30/tools/onboard.py)
atomically merges its `n30` row into the ignored shared map and emits the
matching private device input. [Commands and arrival gates](../docs/n30/build.md).
This file operation neither changes installed maps nor accesses hardware.
