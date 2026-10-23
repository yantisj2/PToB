# RC9 functional snapshot service, wire 1

Copyright (c) 2026 Jonathan Yantis. MIT.

RC9 carries one RC8 LoRa receiver's functional UTC estimate over one direct BLE
hop. It has its own service UUID, marker and HMAC domains. It never supplies the
normal G1/RC7 clock fitter, relay publication, GM election or USB timing output.
It does not qualify RF/UTC precision. The upstream link RMS describes only the
RC8 link; it is not the uncertainty of RC9 delivery. Queue, radio, asymmetric
transport and clock-model errors remain **UNKNOWN**.

This is a new contract, independent of the frozen RC7 and RC8 contracts. Older
receivers do not consume it. T096 has no RC9 receiver implementation. The shared
ESP32 adapter enables clients with `PTOB_RC9=1`; W12 additionally owns the RC8
server. N30 is a direct client only.

## Framing and authentication

Each integer is unsigned little endian. Every message begins with byte0 equal
to total length minus1, bytes1–3 `ff ff ff`, bytes4–5 `R9`, byte6 wire version1,
byte7 type, bytes8–11 group key epoch. All reserved bytes must be zero. Tags are
16-byte truncated HMAC-SHA256 using the existing group envelope: eight-byte
domain, version1, group ID (8 bytes), key epoch (4 bytes), then preceding wire
bytes. Group membership uses symmetric trust, not individual hardware attestation.

The UUID bytes in NimBLE order are service
`09 00 bc 51 e2 d4 38 8c 94 4a c4 76 38 e5 f6 48`, characteristic
`0a 00 bd 51 e2 d4 38 8c 94 4a c4 76 38 e5 f6 48`. There is no RC7 fallback.

## Control and capability

Control is212 bytes, types1 capability,2 request,3 response. Its tag at196 uses
`RC9CTRL1`. The144-byte discovery capability uses the same fields below,
reserved89–127, tag at128, and the independent domain `RC9CAP01`.

| Offset | Bytes | Field |
| --- | --- | --- |
| 12 | 8 | Sender factory |
| 20 | 8 | Sender boot |
| 28 | 8 | Target factory |
| 36 | 8 | Target boot |
| 44 | 8 | Challenge |
| 52 | 8 | Request nonce |
| 60 | 8 | Next challenge |
| 68 | 8 | Response publisher monotonic microseconds |
| 76 | 4 | Request sequence |
| 80 | 4 | Granted/requested lease microseconds |
| 84 | 4 | First admissible publication |
| 88 | 1 | Current source ready, boolean |
| 89 | 107 | Reserved, control only |

Capability contains only sender, boot and nonzero next challenge. A request
contains exact sender/target factories and boots, challenge, nonce, increasing
sequence and lease. The response mirrors those fields and grants at most the
requested lease, supplies a strictly newer challenge, monotonic timestamp and
nonzero publication floor. A response must complete before the original2s
exchange deadline. The original client lease is at most360s, with renewal due
at300s. `ready=false` may retain an admitted lease but cannot create UTC.

## Periodic time snapshot

Time is190 bytes, type4, tag at174 using `RC9TIME1`. The snapshot is publisher
UTC at a software preparation point, with UNKNOWN transport delay. It is not an
actual TX capture or a late TX correction. A future precision transport requires
an explicitly different contract.

| Offset | Bytes | Field |
| --- | --- | --- |
| 12 | 8 | Publisher factory |
| 20 | 8 | Publisher boot |
| 28 | 4 | Monotonic publication |
| 32 | 1 | Time valid, boolean; zero explicitly withdraws time |
| 33 | 1 | Original RC8 assurance,0 unknown/1 continuity/2 anchored |
| 34 | 1 | Original RC8 envelope authority,0 unknown/1 current/2 expired |
| 35 | 1 | Reserved |
| 36 | 8 | Publisher snapshot monotonic microseconds |
| 44 | 8 | Original corrected RC8 RX event monotonic microseconds |
| 52 | 8 | Original RC8 functional expiry in publisher microseconds |
| 60 | 8 | Original RC8 corrected event UTC nanoseconds |
| 68 | 8 | Root factory |
| 76 | 8 | Root boot;0 remains unknown |
| 84 | 8 | RC8 emitter factory |
| 92 | 8 | RC8 emitter boot;0 remains unknown |
| 100 | 8 | RC8 stream;0 remains unknown |
| 108 | 4 | Original source epoch |
| 112 | 4 | RC8 receiver generation |
| 116 | 16 | Original RC8 context |
| 132 | 8 | Original source creation nanoseconds |
| 140 | 8 | Original source expiry nanoseconds |
| 148 | 4 | RC8 link RMS nanoseconds;UINT32_MAX unknown |
| 152 | 1 | RC8 link fit sample count,0–16 |
| 153 | 1 | Original kind,1 own GM/2 selected BLE |
| 154 | 2 | Reserved |
| 156 | 4 | RC9 output bound, always UINT32_MAX |
| 160 | 4 | Original corrected RC8 sequence |
| 164 | 10 | Reserved |

## Client lifetime

The challenge exchange bounds publisher monotonic time relative to the original
local request and response. The policy assumes at most2000ppm relative clock
rate error. This assumption is not a measured timing bound. Expiry uses the
earliest causal conversion and the original lease; UTC extrapolation uses the
midpoint of the coarse interval, with UNKNOWN error. Neither arrival time nor a
GATT round trip trains a fine clock.

The original RC8 expiry must be later than its snapshot and at most12s after
its original physical RX event. Repeated publications of that same physical
pair, polling, metadata and control renewal cannot extend the client's saved
expiry. A changed context/generation retains its own event and expiry. A newer
authenticated withdrawal revokes UTC immediately, retaining the original client
lease for source holding. Replays cannot withdraw or revive time. Recovery from
withdrawal requires a new accepted physical pair. OFF, fault and maintenance
still revoke time through the serialized controller owner.

The codec and client core can be checked without USB:

```sh
python3 firmware/w12/tools/rc9_tests.py --idf artifacts/public-review/esp-idf \
  --out artifacts/rc9-tests
```

These checks use the real group HMAC, independent Python layout/HMAC, sanitizers,
and synthetic monotonic/UTC coordinates. They establish neither live RF
interoperability nor independently measured precision.
