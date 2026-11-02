# Pi 5 / M-HAT final fixed-window observation

Jonathan Yantis. Original observations and derived datasets: CC BY 4.0;
original code snapshots: MIT. This selected collection accompanies a paper
**approved for public draft review**. The [subsequent approval receipt](../../../papers/pi5-mhat-gateway/publication.json)
preserves the original prepared manifest and data and authorizes publication
to GitHub main with unrelated LoRa development excluded. It is a finite hardware observation, not a current acceptance
certificate or independent accuracy test.

The complete final window followed 120.041 seconds of continuous warmup and
lasted 600.026 seconds. It contains 600 new PPS assertions for 600 nominal
opportunities, 600 status snapshots, 599 new-to-new periods and 597 distinct
chrony reference updates. One initially cached assertion is excluded from the
new-edge and period populations. All phase magnitudes and tails remain.
The same full source cohort, current admission, fresh PPS, selected PTOB/Normal,
fresh running PHC transfer and fresh ready/running Ethernet PTP guard were
observed throughout. Configuration/service identities did not change.
There were no observed sequence skips, nominal missing slots, source transitions
or acquisition/export errors in this window. This does not establish endurance.

| Complete population | Result |
| --- | --- |
| GPIO assertion phase RMS / population SD | 872.002 / 871.999 ns |
| Signed mean / absolute P99 / absolute maximum | +2.063 / 4809 / 7017 ns |
| Strict absolute exceedances above 1 / 2 / 5 µs | 46 / 19 / 5 of 600 |
| Chrony tracking RMS estimate, median / range | 72 / 20–1670 ns over 600 snapshots |
| HAT internal fit RMS, median / range | 54 / 30–88 ns over 600 snapshots |
| PTOB regression residual SD, median / range | 371.5 / 98–2893 ns over 600 snapshots |

GPIO IRQ phase is relative to the same Pi system clock disciplined by chrony.
It includes IRQ capture and clock slew and is not independent electrical PPS,
upstream GM, UTC, PHC-output or network-client error. Internal estimates describe
different populations and cannot be substituted for the raw phase or subtracted
to isolate one noise source. The observed tails exceed the 1 µs objective.

`final-edges.csv` retains signed integer-nanosecond phase and new-to-new intervals;
its first interval is blank because the seed is excluded. `final-status.csv`
retains all one-Hz quality observations. `chrony_update` is a local ordinal of
reference-time changes; repeated values do not count as independent updates.
`status_bracket_eligible` means fresh same-cohort one-Hz observations surrounded
an edge within the capture's bracket criterion; it is not exact-event authentication.
Elapsed coordinates are observer polling times, not the PPS edge's host arrival
latency measurement. `cohort01` replaces the full source identity tuple.

The separate post-window `protocol-health.json` and `protocol-ptp.csv` retain four
valid Pi-loopback NTP/PTOB replies and 204 passive Wi-Fi PTP egress observations
(96 matched Sync/FollowUp pairs and 12 Announce messages over 12.006 seconds).
Packet sequences are relative aliases. No raw packet, network address or port
identity is included. These checks did not measure remote receipt or accuracy.
Ethernet was disconnected in all 600 snapshots. PHC midpoint observations and
read brackets are health measurements, not qualified hardware clock offsets.

[manifest.json](manifest.json) records explicit selection, redaction, licenses,
source/image/configuration identity, full denominators and private-input hashes.
Private raw captures, enrollment, identifiers and earlier development populations
are not included. Included CSV arithmetic can be reproduced without those inputs;
physical extraction and the source/service predicates cannot be independently
reconstructed from the selected files alone. Checksums establish identity only.
The original private populations are preserved separately and are not pooled here.

From the repository root, ordinary verification needs only Python 3.9 or newer:

```sh
python3 tools/verify_pi5_gateway.py
```

The exact selected MIT code snapshots in `code/` document acquisition, reduction
and protocol observation. They are historical experimental code, not a public
hardware authorization or a production configuration. They require a separately
enrolled exact target and private raw inputs; the ordinary verifier runs neither
them nor hardware. In particular, the recorded 37-second TAI check describes
this experiment's operating offset, not a permanent production constant.

The matching [seven-page draft](../../../papers/pi5-mhat-gateway/main.pdf),
[source and rebuild instructions](../../../papers/pi5-mhat-gateway/README.md)
and [edition manifest](../../../papers/pi5-mhat-gateway/edition.json) preserve
measured forward behavior and proposed, unmeasured Linux USB and reverse-PTP paths.
