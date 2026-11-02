# Pi timing bridge: next experiments

Copyright (c) 2026 Jonathan Yantis. MIT.

Start from the [Pi target](../../gateways/pi5/README.md) and its
[setup guide](setup.md). This is the continuation plan; no USB/SOF or reverse
PTP performance result is claimed. The complete measured M-HAT/GPIO baseline,
including all tails, remains in the [selected results](../../evidence/pi5/2026-11-02/README.md).

## Next session: USB-connected T096 RC and SOF

The next input to evaluate is an exact verified **T096 in RC mode**, connected
to the Pi over USB. Its accepted BLE source can feed the existing USB source
stream. The current M-HAT CDC stream supplies diagnostics and is a different
contract. No T096 image change or physical connection is performed by this plan.
Read the [T096 guide](../../firmware/t096/README.md),
[USB observation contract](../../clients/macos/usb/README.md) and
[macOS client](../../clients/macos/README.md) first.

1. Inventory the exact T096, installed RC/source-stream image, USB topology and
   recovery identity. Preserve source root/boot/epoch, frame/counter epochs,
   map generation and original admission/expiry. Keep credentials private.
2. Prototype a **shadow-only Linux USB/SOF association**. Capture device SOF
   counters and frame identities; establish which host event each denotes.
   Keep submission/completion bounds and ambiguity explicit. USB read arrival
   is not the original event timestamp, and the macOS host-clock API is not
   assumed to exist on Linux.
3. Train causally: predict an observation using only the preceding fit before
   adding that observation. Withdraw on source/counter reset, ambiguous frame,
   expired map, disconnect or suspend/resume. Log invalid and warmup samples.
4. Compare a prescribed same-cohort window with the GPIO baseline using all
   pulses/observations, bias, RMS, centered SD, P99, maximum, tail counts and
   availability. Record observation rate, observer CPU load, host controller,
   hubs, clock ownership and thermal/power conditions. Do not pool unlike
   causal residuals, GPIO timestamps and chrony estimates into one RMS.
5. Introduce chrony input only after validating the shadow mapping, naming its
   source explicitly and defining an intentional switch from GPIO. Chrony
   remains the sole system-clock writer. Use an independent physical reference
   before making an output or UTC accuracy claim.

Existing code to study is in `clients/macos/` and the T096 USB implementation;
there is no validated Linux SOF/chrony receiver adapter in this Pi target yet.
The [paper's USB analysis](../../papers/pi5-mhat-gateway/main.pdf) describes
both the expected benefit of bypassing GPIO IRQ capture and the unknown host
association/bias terms. The next session should measure those terms rather than
inherit the historical macOS residual as a Pi result.

## Ethernet output and reverse direction

First connect a suitable independent Ethernet peer to the existing forward
server. Record actual native PHC capabilities, UTC/TAI interpretation, L2/E2E
domain 24 configuration, timestamp modes, path/cable asymmetry and reference
uncertainty. Verify remote NTP/PTP delivery, then timing under idle/load and
planned source/link loss and recovery. A healthy wrapper or small user-space
PHC midpoint is insufficient for an accuracy claim.

For proposed external-PTP → PToB operation, an external PTP receiver would own
the PHC and a separately designed bridge would associate it with an nRF timer.
The present M-HAT image is RC-only. GM ingress/publication and a valid bridge
still require implementation. The native Pi PHC exposes no EXTTS/PEROUT pins;
a verified timing NIC with physical I/O or a measured USB/SOF bridge would be
needed. Preserve source provenance, timescales, admission and expiry, prevent
feedback to the originating domain, and drain clock owners before mode changes.

## Open quality boundary

The GPIO baseline passed its finite continuity criteria, while microsecond
tails remain. Their initiating cause is unresolved. New instrumentation and
filter/servo trials must be separate populations with the original configuration
as control. Preserve real spikes rather than removing them from reported data.
Use independent edge capture and bounded IRQ/clock-slew observation to distinguish
physical output error from host capture and feedback. Filtered estimates can
improve a servo's noise rejection without establishing physical accuracy.
