# V14 GD2: occasional GM reference diagnostics

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.

Edition 2026-10-23. **Experimental implementation; bounded field observation recorded.** GD2 is a separately
versioned diagnostics extension to the current T096 G1/RC7/G5 implementation.
The [frozen V14-RC1 specification](V14_CLOCK_SPEC_RC1.md), its six layouts and
historical papers keep their existing identities. GD2 does not revise precision,
JOIN, election, qualification or failover semantics.

GD2 gives clients a descriptive snapshot of their serving root's GNSS input and
actual election quality tuple. It grants no timing, admission, takeover,
traceability or calibrated precision authority. The
[contract and synthetic vectors](../protocol/gm_diagnostics_v2/README.md)
define its bytes. MUST and SHOULD describe this experimental extension.

## Wire record

The complete manufacturer AD is 90 bytes, including its length/type. Multibyte
integers are unsigned little-endian. The sender is the root; no second identity
or boot is encoded. Unknown versions, reserved bits, lengths or source kinds
MUST be rejected. Experimental company identifier 0xffff has no production
assignment implied.

| Byte offset | Bytes | Field | Meaning |
| --- | ---: | --- | --- |
| 0 | 1 | `ad_length` | 89; includes every byte after this length |
| 1 | 1 | `ad_type` | Manufacturer-specific data: 255 |
| 2–3 | 2 | `company` | Experimental 0xffff |
| 4–5 | 2 | `marker` | ASCII GD |
| 6 | 1 | `version` | 2 |
| 7 | 1 | `reserved` | Zero |
| 8–15 | 8 | `group_id` | Provisioned nonzero group identity |
| 16–19 | 4 | `key_epoch` | Provisioned nonzero group key epoch |
| 20–27 | 8 | `root` | Nonzero serving root identity; also the sender |
| 28–35 | 8 | `root_boot` | Nonzero root timing/profile incarnation; the GS boot scope |
| 36–39 | 4 | `mesh_epoch` | Nonzero inherited mesh source epoch |
| 40–43 | 4 | `sequence` | Nonzero GD publication counter; never wraps |
| 44–51 | 8 | `sampled_us` | Nonzero original root monotonic snapshot time, microseconds |
| 52–55 | 4 | `valid_us` | Remaining diagnostic cache lifetime: 1..30000000 microseconds |
| 56 | 1 | `known` | Known mask: FIX/PPS/QUAL/SV/PTP/HDOP in bits 0..5 |
| 57 | 1 | `values` | Boolean FIX/PPS/QUAL values in bits 0..2 |
| 58 | 1 | `sats` | Satellites used: 0..99 when known; otherwise 255 |
| 59 | 1 | `source_kind` | 1 = local GNSS; GD-specific enumeration |
| 60–61 | 2 | `rmc_age_ms` | Active RMC age at snapshot; sentinel rules below |
| 62–63 | 2 | `pps_age_ms` | PPS edge age at snapshot; sentinel rules below |
| 64–65 | 2 | `gga_age_ms` | GGA satellite-count and HDOP observation age at snapshot |
| 66 | 1 | `priority1` | Actual election priority1 |
| 67 | 1 | `clock_class` | Actual election clock class |
| 68 | 1 | `accuracy` | Actual election accuracy code; not a measured nanosecond bound |
| 69 | 1 | `priority2` | Actual election priority2 |
| 70–71 | 2 | `variance` | Actual election variance code; not measured PPS or fit variance |
| 72–73 | 2 | `hdop_centi` | Reported dimensionless horizontal DOP in centi-units; unknown = 65535 |
| 74–89 | 16 | `tag` | First 16 bytes of the record HMAC-SHA256 |

HMAC input is the eight ASCII bytes `R2GNDG02`, then the 13-byte group context
`1:u8 || group_id:le64 || key_epoch:le32`, then bytes 0–73 of this complete AD.
The tag is the first 16 bytes of HMAC-SHA256 under the existing 32-byte group PSK.
Encoded group fields MUST match enrollment. This uses the existing symmetric
group trust boundary; every PSK holder can create authenticated statements.

## Values and unknowns

`known` bits 0..5 mean FIX, PPS, SOURCE_QUALIFIED, SATELLITES, PTP_DATASET
and HDOP. Bits 6..7 are zero. `values` bits 0..2 carry FIX, PPS and SOURCE_QUALIFIED; bits 3..7
are zero. A set value requires its known bit. Clear known means unavailable;
known plus a clear value means an evaluated false observation.

FIX copies the current active-RMC fix result; PPS copies recent-edge presence.
True FIX requires RMC age strictly below 2500 ms; true PPS requires PPS age strictly
below 1500 ms. SOURCE_QUALIFIED copies actual local source admission, including its
existing 60-second qualification. True qualification requires known true FIX and
PPS; diagnostics cannot cause qualification.

SATELLITES is satellites **used**, from checksum-valid GGA. A known count is 0..99
and requires GGA age strictly below 4000 ms; zero is a real reported count. Otherwise
its known bit is clear and `sats` MUST be 255.

HDOP is the horizontal dilution of precision reported by checksum-valid GGA, in
dimensionless centi-units: 123 displays as 1.23. Known HDOP requires GGA age strictly
below 4000 ms and `hdop_centi` in 0..65534. Zero is a valid reported numeric value,
not an unknown sentinel. Unknown HDOP MUST clear bit 5 and use 65535. The parser
accepts an unsigned integer or one/two decimal digits exactly; blank, signed,
malformed, overflow or higher-precision input is unknown without changing normal
GGA acceptance or source qualification. It does not round a higher-precision input.
HDOP is a receiver geometry report; it is not a measured time error, antenna test,
clock variance or precision bound. VDOP/PDOP and antenna health remain unavailable.

The three ages refer to `sampled_us`. Finite ages are whole milliseconds rounded
down. 65534 means at least 65534 ms; 65535 means unobserved/unknown. These sentinels
cannot accompany true FIX/PPS or known SATELLITES/HDOP because of their freshness
limits. A receiver may add its own local receipt age for descriptive display;
it MUST NOT call that a precise current remote age or a transport-delay bound.

PTP_DATASET means the root reports its actual election tuple in order
`priority1, clock_class, accuracy, priority2, variance`. The current defaults are
`128,248,254,128,65535`; accuracy 254 (0xfe) and variance 65535 (0xffff) remain unknown, even with
GNSS fix and PPS. These are raw quality codes, not fit RMS, measured PPS variance
or an inferred error budget. If PTP_DATASET is unknown, this exact default tuple
is canonical padding and MUST NOT be displayed as measured quality. The current
CoreS3-SE display prints raw accuracy 254 as ACC UNKNOWN and raw variance 65535 as
VAR NONE; NONE means unreported, never measured zero. Other reported codes remain
visible as hexadecimal codes rather than invented units or uncertainty bounds.

`source_kind=1` identifies a locally sourced GNSS snapshot in the GD enumeration.
It is not an IEEE 1588 `timeSource` encoding. GD2 carries no PTP domain, TAI/UTC
offset, leap flags, UTC/frequency traceability or independent UTC accuracy.
Reporting the election tuple does not establish IEEE 1588 interoperability or
conformance.

## Publication, expiry and scope

The serving GM uses its existing one-second nonprecision extended-advertising
update. At first opportunity, then no earlier than 2 seconds after a successful
GD publication, one GD-only window replaces GS. Healthy updates alternate GD and
GS. This is the source 0.15.168 cadence retained from 0.15.167; 0.15.166 used a
10-second GD1 interval.
No new advertiser, extra update call, controller reserve or precision record is added.
GS 116 bytes plus GD2 90 bytes would exceed the unchanged 191-byte controller ceiling.
Scheduling can defer a window; 2 seconds is not a delivery guarantee. The omitted
GS update can briefly expire a client's live-GS indication because GS validity is
at most 2 seconds and can be shorter. Cached role identities/event timers are
separate and do not grant authority.

The original diagnostic snapshot budget is at most 30000000 microseconds. Before
publication the producer subtracts elapsed time since `sampled_us` from that
budget, while preserving the original sample timestamp. A future, expired,
invalid or mismatched GD snapshot falls back to GS. A failed optional update does
not withdraw healthy precision timing. Each successful GD update advances an
independent nonzero 32-bit sequence; exhaustion suppresses GD rather than wrapping.

Receivers MUST authenticate the complete record and bind the canonical advertiser
to `root`. The root identity, timing/profile incarnation and mesh epoch MUST match the current
timing owner's selected source. Mesh epoch persistence across GM replacement does
not permit reuse of another root's incarnation or cached diagnostics. A scope mismatch
cannot update the selected reference display or its deadline.

`root_boot` is the same timing/profile incarnation carried by GS and exposed by
the GM's `gm00.incarnation`. A runtime GM/RC role change can replace it within one
physical device boot. The USB/status physical boot is a separate identity;
diagnostic validation MUST NOT substitute `status.boot` for `root_boot`.

On first acceptance of a newer sequence, the local diagnostic deadline is the
original radio receipt time plus authenticated `valid_us`, with checked arithmetic.
Queue/presentation delay is consumed from that deadline. Repeated equal/older
sequences MUST NOT renew it. The sequence floor and nondecreasing sample-time
check are keyed by root/`root_boot` and survive mesh-epoch or source selection changes
within the observer boot; GS and GD counters are independent. At the deadline,
values are stale and MUST NOT remain marked current. For the unchanged selected
root/timing incarnation/mesh epoch, an observer MAY retain the last authenticated
payload until replaced within its boot and show it as CACHED or STALE with its
original receipt age. This descriptive cache has no TTL extension or timing
authority. If the timing selection becomes entirely empty (root, timing/profile
incarnation and mesh epoch all zero), the observer MAY show the retained payload
as LAST GM / CACHED history, always noncurrent and with its original receipt age.
No GD report may be accepted as the selected reference while that selection is
empty. A different nonempty selection, including a changed root, incarnation or
mesh epoch and any partially empty tuple, MUST hide the old root's values from
the reference readout. Returning to the original selection does not renew its
receipt or deadline. Replays cannot reset receipt age, expiry or cache age.

The current client fence has eight bounded root/incarnation contexts and is volatile
across client reboot; it does not establish durable replay protection, arbitrary
delay-attack resistance or original GNSS age at first unseen receipt. Diagnostic
receipt validity cannot extend any timing map, source permission, role grant,
admission lease or qualification result.

## GD1 compatibility

GD2 encoders emit exactly 90 bytes with version 2, 74 authenticated body bytes and
domain `R2GNDG02`. A compatible decoder MAY also accept the historical GD1 shape:
88 bytes, length byte 87, version 1, 72 authenticated body bytes, domain `R2GNDG01`, and
known bits 0..4 only. Its HDOP result MUST be unknown. A version/length/domain mix
MUST be rejected; bytes beyond a GD1 body cannot be guessed to be HDOP.
An observer supporting only GD1 will reject GD2 and show diagnostics unavailable;
no precision, JOIN, GS, election or timing wire layout changes for compatibility.
Both diagnostic versions share their existing independent GD sequence scope, so
changing wire version does not permit replay or reset the deadline.

## Validation boundary

T096 0.15.168 source `23cbec6017f268fa107dd865012838aff9004ea6` and CoreS3-SE 0.1.10
were observed in a bounded 210-second run on 23 October 2026. The observer's installed
build identity is recorded in the contract. Its passive listen window was 80 ms at
most every 800 ms under the unchanged healthy-clock gate.

The 177 receiver samples contained 61 distinct authenticated GD2 reports, spanning
209.809967 seconds of original-deadline continuity: 176 samples had matching current
selected scope, and one had an entirely empty timing selection and only LAST GM
history. The received publication sequences spanned 105–209: 61 distinct labels observed,
44 unobserved labels in that inclusive range. This is not an RF packet-loss rate;
each published AD repeats and the passive observer does not capture every packet.
No diagnostic TTL expired. The largest observed gap between accepted
reports was 12.049297 seconds; the minimum arrival margin before the prior original
deadline was 17.946095 seconds. Repeated sequences changed neither payload, receipt
nor deadline. This is a bounded receipt result, not guaranteed delivery or
uninterrupted timing selection. 1,279 raw console frames were independently checked
for CRC and exact source/build/session binding; received RF wire bytes are not
available, so this establishes production-decoder acceptance through its console.

The matching producer capture contained 10 four-device GM cohorts, 40 unit
observations and 113 extra serving-root clock queries: 401 CRC-framed responses in
all. Every cohort retained one inherited mesh epoch and the same serving ACTIVE
root. HOT was confirmed from both peers in 7/10 cohorts; it was acquiring in the
remaining 3. The serving root's successful GD HCI counter advanced 127→172. These
local facts do not establish guaranteed HOT availability or seamless handover.

Of 61 received reports, 20 matched the exact original GGA event in the 123 source
clock queries. Their reported HDOP and satellites-used values agreed exactly.
The join uses the interval defined by GD `sampled_us` minus its floored GGA age
and the source's exact `mono_us - gga_age_us`; the remaining 41 reports have no
same-event source observation and are retained as unmatched. HDOP is real GGA
geometry data, not a timing precision measurement.

The configured LoRa transmitter had 10 observations: 4 NO_SOURCE, 6 matched
source-bearing BLE contexts, including 1 before its first completed timing event,
and 5 completed timing observations. In that completed subset, sequence advanced
3→11 and TX_DONE 6→15 under the same root/incarnation/mesh epoch. No independently
received LoRa measurement was made.

Eight on-device live-model page renders and one completed LCD Reference frame
were captured with verified pixel CRCs. These are device-generated framebuffer
observations, not optical LCD or display-edge metrology.

Earlier results remain separate: the previous 0.1.8 receiver run was INCOMPLETE,
with 146/150 current diagnostic samples, one empty timing selection and three
actual expired-report samples. Earlier GD1 delivery gaps are not reclassified as
steady freshness. The first 0.1.10 preflight produced zero samples after a host
ownership-query timeout; the completed retry is the population above.

The field-observation summary in this section and `field_validation` in the
contract are CC BY 4.0, credit Jonathan Yantis. The contract, synthetic vectors,
checker and ordinary specification text use MIT. Private raw captures and real
enrollment are excluded; public offline tests verify wire semantics, not a repeat
of the private field extraction. No calibrated UTC, phase continuity, independently
measured RF endpoint, IEEE 1588 conformance or durable failover qualification follows.

Run the public offline wire check and the production-body host test from the
repository root:

```sh
python3 tools/check_gm_diagnostics_v2.py
python3 firmware/t096/tools/gnss_diagnostic_tests.py \
  --deps artifacts/deps --out artifacts/gd2-check-01
```

The first command uses only Python's standard library and opens no devices.
The second needs the documented T096 dependencies and checks actual C/TinyCrypt
bodies with sanitizer instrumentation; use a fresh output directory.
