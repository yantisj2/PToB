# V14-RC1 wire annex

Frozen 12 October 2026 at historical specification checkpoint `7995249`.
Read the [specification](../../docs/V14_CLOCK_SPEC_RC1.md),
[proposal](../../papers/ptob-v14-sig-proposal.pdf), and
[research status](../../docs/V14_RESEARCH_STATUS.md) together. This is a portable
specification candidate, without current firmware or hardware qualification.

[contract.json](contract.json) defines six layouts: GR3, RC5/profile3,
RQ5/profile3, JOIN4, GC17, and GC17 HELLO. Precision AD records are 190 bytes;
the 223-byte HELLO uses connected control. The frozen contract is unchanged,
including its known GR3 origin-range notation ambiguity: `[14,60]` means start
14, length 60, or bytes `[14:74]`, in the checked reference/examples.

[vectors.json](vectors.json) contains synthetic identities and a public test
key: 14 valid and 19 rejected records. HMAC-SHA256 tags use the first 16 bytes
over an eight-byte domain, 13-byte group context, and authenticated body. Route
digests use the first eight bytes of their route HMAC. The test key carries no
deployment authority. Byte identity does not establish time accuracy.

Run from the repository root with Python 3.9+:

```sh
python3 tools/check_v14_document.py
```

The public checker verifies the proposal's 181 fields, six printed examples,
packet/origin/route tags, reserved padding, units, and bibliography resolution.
It opens no devices and needs no private reference checkout or crypto library.
The original 33-vector C/ASan/UBSan result is historical and not rerun here.
The [issue record](../../evidence/v14/2026-10-12/provenance/ptob-v14-sig-issues.json)
retains the selected decoder's duplicate/nonpreceding-correction acceptance gaps;
the target requirements remain rejection. Full replay/stateful conformance and
later RC profiles need separate verification.

Contract, synthetic vectors, and ordinary documentation: **MIT**, copyright
(c) 2026 Jonathan Yantis. The proposal and scientific evidence have their
separate CC BY 4.0 licenses.
