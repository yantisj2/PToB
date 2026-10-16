# USB observation boundary

Copyright (c) 2026 Jonathan Yantis. MIT.

The Objective-C helper uses IOKit/IOUSBHost for compatible vendor interfaces:
VID `2fe3`, PID `0100`, class/subclass/protocol `ff/54/01`. Every source, captured
SOF and marker binds to the selected serial's full silicon suffix. The helper
does not open a CDC console. Duplicate silicon identities are refused; selection
retains a healthy identity, rotates after 15 seconds without permitted UTC and
five seconds backoff, and rescans on disconnect. `--device FULL_SERIAL` pins an
exact identity across port changes. A selected SS3 CDC interface is insufficient.

UTS1 source records, UFS1 frame records and UFA1 markers are decoded strictly.
Identity, boot, source, path, USB/capture epoch, map and original expiry fences
remain independent of transport connectivity. Captured UTC is at the device
counter tick, not host arrival time. The coarse comparison preserves the entire
wall-time request bracket and signed offset interval. Fine SOF/Mach fitting does
not establish PPS-to-UTC association or absolute timestamp bias.

## Missing-SOF-map observation

Version 0.21 accepts an explicit compatible-device classification that no
permitted map covers the captured SOF while an atomic current-counter projection
still proves current source permission. It remains an invalid time observation.
Matching provenance may preserve the owned chrony filter only within the
previously granted original authority deadline. It cannot install a map, supply
UTC or extend a deadline. Actual withdrawal, expiry, changed/unknown provenance
and malformed classifications retain fencing; older ambiguous reason1 replies
retain their prior behavior. No natural-event physical coverage is inferred
from synthetic decoder tests.

The helper's `--self-test` parses fixtures without IOKit access. Live precision
requests use bounded vendor-IN EP0 control transfers, currently an experimental
transport. Control timeout diagnostics remain separate from source-permission
gaps. No automatic device reset, firmware write or source-role change occurs.
