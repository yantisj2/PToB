# Public T096 provisioning and storage — draft P1

Status: reviewable design and offline reference model; **not implemented on the
T096**. Copyright (c) 2026 Jonathan Yantis. MIT.

## Selected behavior

Distribute a generic unprovisioned application. A local wired tool configures a
user's device and group after physical confirmation. The image contains no group
PSK, lab roster or automatic demo credentials. Unprovisioned or quarantined boot
permits identification, status and recovery but grants no mesh or timing authority.

The initial public hardware target is the verified T096 HT-n5262G layout.
Read the immutable silicon ID before provisioning. Validate the board revision,
bootloader and memory layout independently; a valid ID or friendly name is not
proof of compatible hardware. Unknown hardware capabilities remain disabled.

## Flash ownership

All ranges below are start-inclusive, end-exclusive, from the selected source's
DTS, dependency lock, linker configuration and diagnostic definitions.

| Range | Owner | Public provisioning policy |
| --- | --- | --- |
| `0x00000–0x26000` | Resident MBR/SoftDevice region | Never write |
| `0x26000–0xDF000` | Application | App-only release payload |
| `0xDF000–0xE0000` | Credential page A | Provisioning owner only |
| `0xE0000–0xE1000` | Credential page B | Provisioning owner only |
| `0xE1000–0xE5000` | Diagnostic reservation | Preserve both banks |
| `0xE5000–0xED000` | Existing journal | Preserve |
| `0xED000–0x100000` | Recovery/boot region | Never write |

The existing DTS declares application space through `0xE5000`; `ROM_END_OFFSET`
reserves its last `0x6000` bytes. Before implementing P1, split these into explicit
application, credential and diagnostic partitions and remove the equivalent end
offset exactly once. Otherwise the application may lose 24 KiB twice or overlap
storage. Require linker/ELF/HEX/UF2 checks against the same bounds. Do not move
any protected boundary. Confirm flash page/write geometry from the pinned driver
and on the supported hardware before permitting writes. The reference model
assumes 4 KiB erase pages and aligned four-byte programming.

Generic release UF2 files must contain no credential-page blocks. App update and
recovery must preserve credentials unless the user explicitly invokes erase.
Bootloader update semantics and erase behavior require separate verification;
source-level partition declarations alone are not proof of preservation.

## Record P1

Two pages, one record per page. All integers are little-endian. The first 128
bytes contain the following fields; bytes 128–4091 remain erased (`FF`). The
four-byte commit word at offset 4092 is programmed to zero last. CRC32 is the
IEEE CRC used by Python `zlib.crc32`, over bytes 0–123, stored at offset 124.
It detects accidental corruption, not malicious modification or authenticity.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 8 | ASCII `PTOBPRV1` |
| 8 | 2 | Schema = 1 |
| 10 | 2 | Record length = 128 |
| 12 | 8 | Generation, nonzero; no wrap |
| 20 | 8 | Local immutable device ID |
| 28 | 8 | Group ID |
| 36 | 4 | Key epoch |
| 40 | 1 | Kind: 1 provisioned, 2 erased tombstone |
| 41 | 1 | Requested startup role: 0 unassigned, 1 GM, 2 relay |
| 42 | 1 | Name byte length |
| 43 | 1 | Flags = 0 |
| 44 | 24 | UTF-8 display name, zero-padded |
| 68 | 32 | Group PSK |
| 100 | 24 | Reserved zero |
| 124 | 4 | CRC32 |
| 4092 | 4 | Commit word |

Provisioned records require a nonzero group/epoch/PSK, role 1 or 2, and a valid
1–24-byte name. Tombstones retain generation and device ID; group, epoch, role,
name and key are zero. Roles express intent only; hardware capability and live
source/authority gates still decide whether service is allowed. No GNSS quality,
source deadline, route permission or boot nonce is persisted in this record.

## Transaction and power loss

1. Enter bounded local maintenance, withdraw time permission and drain actual
   radio/USB timing owners using existing completion/deadline rules. Refuse writes
   if owner retirement is uncertain. Serialize against diagnostic flash writes.
2. Verify the selected physical ID, original session/boot, expected generation
   and schema. Generation exhaustion requires explicit recovery, never wrap.
3. Erase the inactive page and verify erased contents. Write/verify the payload
   in aligned units. Validate the complete record before committing.
4. Program the final commit word and reread/validate the complete page.
5. Erase and verify the previous page before allowing service or reporting
   success. Clear transient key buffers and reboot into a fresh authority scope.
   Confirm non-secret status after reboot; no credential readback API exists.

At boot, choose the highest valid generation only when there is no committed
malformed/unknown record and no generation tie. If the other page is non-erased,
withhold service until cleanup is complete. An incomplete uncommitted candidate
may be discarded; before commit, the previous generation may resume after cleanup.
After a valid new commit, the old generation must never be chosen instead.
A partially programmed commit word, invalid committed CRC, wrong device ID,
unknown committed schema or equal generations quarantines the store.

An interrupted erase can leave a malformed committed record. P1 deliberately
quarantines this case rather than guessing whether it was an old or new record.
Recovery requires local physical confirmation and explicit reset/reprovisioning.
The model tests this fail-closed tradeoff; it does not promise uninterrupted
availability across arbitrary flash faults. Drivers must impose finite timeouts;
a timeout never means a successful write or permission to keep serving.

For factory erase, commit a higher-generation tombstone, then erase the older
page and verify it. Keep the tombstone so the prior PSK cannot become active by
fallback. Report erase complete only after cleanup/readback. Interrupted erase
may leave key bytes in flash until recovery finishes; do not promise forensic
secure erasure. Reset must not restore compiled-in credentials.

## Local management interface

Proposed operations are `identify`, `status`, `begin`, `write-config`, `commit`,
`erase` and `abort`; these are design names, not existing console commands.
Use a bounded length-delimited protocol, maximum 256-byte payload, strict schema,
no command echo and no diagnostic capture of credential-bearing frames. Bind
mutations to device ID, boot/session challenge and expected generation. One
session, 60-second absolute deadline, no extension by retries; a request must
start within a locally confirmed maintenance window. A dedicated on-device
confirmation screen must show the operation and ID; confirm with a fresh button
press after the prompt, avoiding existing power/profile shortcuts. Cable presence
alone is not authorization. On disconnect/timeout, clear buffers and leave service
withdrawn until boot/status reconciliation determines a valid store.

Keys enter the host tool through protected local files or non-echoing input,
never command-line arguments or environment variables. No secret in stdout,
logs, crash reports, support bundles or public manifests. Local files use owner-only
permissions with safe creation; never overwrite enrollment silently. The host
can generate a 32-byte PSK with OS randomness or accept an existing group's key.
Do not derive PSKs from names, IDs or passwords. No over-air provisioning in P1.

A compromised host, firmware or physical debugger can expose this plaintext key.
No secure element, encrypted storage, secure boot or anti-rollback is established
by this design. Do not add encryption whose decryption key is stored beside it
and describe that as isolation. Review firmware/bootloader rollback separately:
older firmware must not be installed unless its credential format and layout are
explicitly compatible. The old laboratory image is not a supported public downgrade.

## Update and test gates

Firmware release manifests must state board/layout, provisioning schema and
supported upgrade/downgrade paths. Preserve current P1 storage on compatible
updates. Unknown future schema must quarantine, never erase automatically.
A future schema migration requires a separate transaction/version review.

Run the offline model tests from the root:

```sh
python3 -m unittest discover -s firmware/t096/provisioning -p 'test_*.py' -v
```

They cover torn record/commit writes, interrupted cleanup, rotation, tombstones,
corruption, identity mismatch, unknown schema and name validation. They model
storage decisions only. Remaining gates: actual C implementation and host parity,
controller drain/flash arbitration, pinned-driver geometry, RAM/stack budgets,
clean standalone builds, update-package exclusion checks, and separately authorized
power-loss/install/recovery tests on hardware outside the lab roster.
