# PToB — Precision Time over BLE

**[Explore PToB at ptob.dev](https://ptob.dev)** — the companion project website.

PToB is a precision time protocol proposal for Bluetooth Low Energy. The goal is
useful, traceable time at the receiving device: associate a physical radio event
with its timestamp, account for correction delay, and make the clock's quality
and availability explicit.

This repository holds the draft papers, experimental evidence, and work toward a
T096 firmware example. It is intended for BLE implementers, timing specialists,
and embedded engineers evaluating the protocol, its measurements, and its fit
for real devices. All papers remain drafts; PToB is not an adopted Bluetooth SIG
or IEEE standard.

## From v12 to R2

**Precision from passive reception drove v12.** That requirement pushed the
project toward periodic advertising as the timing transport and made periodic
advertising support a baseline requirement for participation in the PToB mesh.
The v12 model explored the scheduling, correction, and coordination rules that
informed the T096 R2 hardware implementation.

The architecture separates **mesh services** for capable devices from **client
services** for devices that cannot meet those requirements. A later GATT client
service is planned for phones and conventional operating systems. That service
is distinct from the GATT control traffic already exercised in R2 experiments;
it does not imply the same precision or mesh role for every client.

[Read the v12-to-R2 design background](docs/V12_TO_R2.md) for the service split,
implementation boundaries, and questions still open for review.

## Read the research

| Draft | Focus | Edition |
| --- | --- | --- |
| [V14-RC1 service and profile proposal](papers/ptob-v14-sig-proposal.pdf) | GM authority, boundary-clock regeneration, six wire layouts, and conformance targets | 12 October 2026 |
| [V14 boundary-clock research](papers/ptob-v14-preprint.pdf) | Causal timestamps, prospective one-relay PPS-shadow validation, and full availability accounting | 12 October 2026 |

[Sources, citations, and revision history](papers/README.md) accompany each paper.
The [October 2 proposal and calibration preprint, and October 3 oscillator study](papers/README.md#historical-drafts)
remain available with their original evidence boundaries.

V14 separates independently sourced grandmasters from dedicated relay clocks
that regenerate downstream events through their local disciplined maps. Read the
[research status and remaining conformance gaps](docs/V14_RESEARCH_STATUS.md)
and [frozen RC1 specification](docs/V14_CLOCK_SPEC_RC1.md). RC1 is a specification
candidate; later firmware changes do not inherit its research results.

## Inspect the evidence

| Collection | What you can review |
| --- | --- |
| [V14 October 12 research](evidence/v14/2026-10-12/README.md) | All 504 observed PPS edges, separate validity populations, renewal samples, and wire issues |
| [October 2 paper evidence](evidence/papers/2026-10-02/README.md) | Claim mappings, calibration, populations, and original protocol/firmware identities |
| [Oscillator/PLL evidence](evidence/papers/oscillator-pll-2026-10-03/README.md) | Hardware-derived samples, separate cohorts, and sampled lock states |
| [v12 numerical model](evidence/v12/model/README.md) | Frozen model, executable tests, synthetic results, and retained failures |
| [R2 controller simulation](evidence/v12/r2/simulation/README.md) | Control and precision-transport campaigns with explicit outcomes |
| [R2 hardware records](evidence/v12/r2/hardware/README.md) | T096 startup, UI, and service observations with unresolved issues |

The [evidence index](evidence/README.md) explains what can be reproduced from this
checkout. Private raw captures are not included. Relative timing error, software
lock, and simulated precision are distinct from calibrated UTC and independently
measured electrical-output accuracy.

The [T096 example](firmware/t096/README.md), developed from R2, is being prepared
for standalone builds. Firmware source and a deployable release are not yet
included here.

Reviews are welcome on controller support, physical timestamp association,
clock discipline, service boundaries, and measurement design. Identify the draft
edition or commit, the claim under discussion, and the evidence that would test it.

By **Jonathan Yantis**, with [acknowledged Codex assistance](CREDITS.md).
Code is [MIT](LICENSE); papers, figures, and original evidence datasets are
**CC BY 4.0**. See [licensing and attribution](LICENSING.md).
