# Name map → device ID / UUID

Copyright (c) 2026 Jonathan Yantis. MIT.

The selected T096 implementation uses a stable **64-bit device ID**, displayed as
16 lowercase hexadecimal digits. This is not a standard 128-bit UUID. GATT service
and characteristic UUIDs identify protocol endpoints, not individual devices.

The current board adapter obtains eight bytes from Zephyr `hwinfo_get_device_id`,
prints those bytes in returned order, then parses that hexadecimal string as the
numeric identity. Protocol integers serialize little-endian. Thus synthetic
hwinfo bytes `01 23 45 67 89 ab cd ef` become display ID `0123456789abcdef`, numeric
`0x0123456789abcdef`, and wire bytes `ef cd ab 89 67 45 23 01`. Do not cast the
hwinfo byte buffer directly to a native integer. Validate this mapping against
the pinned target driver before public provisioning. A boot nonce, BLE address,
USB port and display alias are distinct from this stable ID.

Names are display metadata. The inspected G1 implementation permits 1–24 UTF-8
bytes, duplicate names and renames, while rejecting malformed/control/directional
text. Names do not authenticate a device, break election ties or establish source
quality. The historical recent-name cache has eight entries; eviction does not
revoke membership. The public implementation must verify these limits anew.

The [shared registry](../../registry/README.md) defines one schema for all targets.
Its [example](../../registry/example.json) uses synthetic IDs. Keep the actual
map in an ignored local file and supply `--registry` to each target builder.
T096, W12 and SS3 compile all entries into read-only display metadata. T096 prefers
authenticated non-default names, then registry aliases and an ID suffix. W12 and SS3
use registry aliases and then a bounded hexadecimal fallback. Updating this
map currently requires a rebuild.

Store entries by ID, not name. Duplicate aliases are allowed; duplicate IDs and
optional UUIDs are rejected. A reverse name lookup cannot authorize a write.
An optional UUID is separately assigned metadata, not a reinterpretation of the
64-bit protocol ID. The map changes labels only: no role, capability, membership,
source quality or enrollment is granted by an entry. The macOS build generates Swift aliases from the same validator and records
the map in its local build receipt.

The selected W12 and SS3 RC adapters derive its 64-bit ID from `0000` followed by the
complete 48-bit factory MAC, in hexadecimal display order. Its local device input
must match that mapping and the actual factory probe. This is a separate board
adapter from T096 hwinfo; an alias or optional UUID cannot replace either rule.

Board capabilities and startup roles are separate configuration. Do not infer
battery presence, GNSS capability or default GM role from an alias. Public USB
identity should use a stable device-ID-based serial independent of names, with
host discovery updated and tested accordingly. Existing laboratory naming is not
a public naming requirement. If a later client requires 128-bit device UUIDs,
specify a separate versioned mapping; do not silently reinterpret these IDs.
