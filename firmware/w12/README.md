# W12 native RC example

Copyright (c) 2026 Jonathan Yantis. MIT; preserved source notices apply.

**PToB owns W12 native G1/RC7 development.** Public successor version **0.1.0**
starts from the selected native application at `48b9ab7`. See the
[build guide](../../docs/w12/build.md), [cutover record](../../docs/w12/cutover.md),
[explicit import provenance](imports/2026-10-16.json) and [source notices](NOTICE.md).

The supported public profile is **RC**: BLE50 client-only or BLE100 receiver,
with an optional experimental estimated-TX relay. W12 GM service, GNSS variants,
LoRa, WiFi, PTS and donor output correction experiments are not supplied by this
target. Original signed source expiry, physical receive/correction matching,
controller ownership, bounded update drain and precision refusal remain in force.
The estimated relay has unknown TX uncertainty and no actual TX capture claim.

The [shared registry](../../registry/README.md) supplies display aliases. Device
factory binding, group enrollment and real maps are explicit ignored local
inputs. No enrolled binary is distributed and runtime mutable provisioning is
not implemented. Build/host-test success is separate from physical precision,
absolute UTC, RF duty and reliability qualification.

The selected source includes the application under `main/`, a closed historical
display/GNSS include boundary under `legacy/`, contracts and host checks, pinned
public SDK acquisition, and a no-hardware builder. The legacy code path supplies
compatibility types and display routines; its presence does not activate other
radios or GPS. A generic public installation/recovery tool remains pending.
