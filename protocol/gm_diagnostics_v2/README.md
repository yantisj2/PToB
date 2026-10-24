# V14 GD2 diagnostics extension

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.

**Experimental implementation; bounded field observation recorded.** GD2 is a 90-byte authenticated
GNSS/reference and raw election-quality AD, adding real GGA HDOP to GD1. It is a
descriptive snapshot of the selected serving root with no precision, admission or
GM authority. Read the [versioned specification](../../docs/V14_GM_DIAGNOSTICS_V2.md).

[contract.json](contract.json) defines every byte, sentinel and cadence.
[vectors.json](vectors.json) includes only public synthetic identities/key;
none is deployment enrollment or hardware evidence. Both GD2 and strict legacy
GD1 cases match the actual C/TinyCrypt codec and an independent Python HMAC.

```sh
python3 tools/check_gm_diagnostics_v2.py
```

The standard-library check operates no hardware. Production codec/parser checks:
`firmware/t096/tools/gnss_diagnostic_tests.py`. GD1 compatibility does not invent
HDOP from a legacy record. Frozen V14-RC1 and historical papers remain unchanged.

The contract records the 23 October 2026 T096 0.15.168/CoreS3-SE 0.1.10 field population:
61 authenticated GD2 reports over 209.8 seconds with zero expired diagnostic TTLs,
plus one empty timing selection shown only as LAST GM history. 20 reports matched
real source GGA events exactly for HDOP/SV; 41 are unmatched. Earlier expired-report
failures remain separate. Field-summary records are CC BY 4.0, credit Jonathan
Yantis; contract/code and ordinary documentation are MIT. See the specification
for counts, original deadlines, cache semantics and remaining limits.
