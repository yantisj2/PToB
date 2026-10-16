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

Proposed user-local map format:

```json
{
  "schema": 1,
  "devices": {
    "0123456789abcdef": {"alias": "Roof clock"},
    "1023456789abcdef": {"alias": "Roof clock"}
  }
}
```

These IDs are synthetic. Store the map by ID, not by name. A reverse lookup for
“Roof clock” returns both candidates and cannot authorize a write. Every mutating
operation must resolve and confirm one immutable ID. A host-local alias takes
precedence for host display only; show the ID alongside it. Changing that alias
does not rename the device or change its role. Proposed device rename uses the
same physically confirmed P1 configuration transaction and preserves group/key.
Duplicate discovered IDs must be treated as ambiguous and refused.

Board capabilities and startup roles are separate configuration. Do not infer
battery presence, GNSS capability or default GM role from an alias. Public USB
identity should use a stable device-ID-based serial independent of names, with
host discovery updated and tested accordingly. Existing laboratory naming is not
a public naming requirement. If a later client requires 128-bit device UUIDs,
specify a separate versioned mapping; do not silently reinterpret these IDs.
