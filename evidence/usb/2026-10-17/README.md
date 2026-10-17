# USB timing comparison — 17 October 2026

Jonathan Yantis supplied these selected hardware observations for the
[USB/macOS draft](../../../papers/usb-macos-time-transfer-preprint.pdf) and
[client results](../../../clients/macos/README.md). Original observations and
derived CSV data use [CC BY 4.0](../../LICENSE.md); verification code uses MIT.

This is the same fixed, concurrent **180-second shared-bus window** recorded in
the [comparison summary](../../../clients/macos/results/2026-10-17-ble-vs-direct.json),
approximately 12:42:10–12:45:10 PM CDT. Selection used RC trial +60 through +240
seconds, independent of error magnitude. Every USB sample and native status
point in that window is retained. RC and DC use separate physical GNSS chains;
these are comparable methods, not a controlled ranking of transports.

## Attached observations

| File | Rows | Content |
| --- | ---: | --- |
| [rx.csv](rx.csv) | 11,520 | All 5,760 USB sample events per profile; acceptance, rejection, warm-up and causal prediction errors |
| [status.csv](status.csv) | 354 | All 177 native status points per profile; delivered/withheld output, admitted same-instant differences and sampled tracking RMS |
| [manifest.json](manifest.json) | — | Exact file/summary identities, derivation and exclusions |

`profile` is RC or DC. `index` starts at one for each profile and file;
`elapsed_ns` is relative to the common window start. Files contain no absolute
UTC labels, original counter values, USB serials, source/boot/group identifiers,
enrollment, socket paths or raw packets. Public input-prefix hashes bind their
private originals without including those originals.

In `rx.csv`, 0/1 fields count a valid timing reply, hardware acceptance/rejection
and RX warm-up/unqualified/rejected observations. `rx_error_ns` is blank when no
causal prediction was admitted; a numeric zero is a measured zero. Its sign is
observed corrected source phase minus the previous fit's prediction, against
hardware SOF/Mach boundaries. UTC translation is excluded. Current observations
never train their own predictions; errors retain mean and transients, with no
retrospective detrending. Rejections can restart warm-up.

In `status.csv`, `user_clock_delivered` records availability of the separately
sampled user clock. `chrony_minus_fine_ns` is the admitted delivered chrony time
minus its own fine input at a common host instant; blanks remain withheld.
These two columns can have different admission outcomes. `estimator_rms_ns`
samples the latest synchronized native chrony tracking readout at that status
point. It can be cached; its median is not the median of tracking events selected
by their query timestamps. `synchronized` records that readout's state.

## Derivation and verification

The offline export replays all preceding captured observations through the
unchanged production `USBFrameBridge` and immutable-map arithmetic before
selecting errors within the window. The export reproduces the existing summary
exactly. Status fields are selected directly from the same native status rows.
The manifest records production-code and private exporter hashes. No observation
is removed for its magnitude, and missing errors are never filled with zero.

From the repository root, using Python 3.9 or newer:

```sh
python3 tools/verify_usb_results.py
python3 tools/verify_research.py
```

The first command checks file identities and recomputes RX RMS/mean/sigma,
warm-up/rejection ledgers, sampled availability, direct RMS/mean/tails and the
tracking-readout statistics. It uses only these public files and Python's
standard library. The second checks the current paper edition and its separate
historical evidence. These commands do not reconstruct the hardware/SOF fit
from private captures or independently validate physical accuracy.

## Boundaries and retained failures

The bus was shared, **not clean**. An isolated-controller repeat is pending.
PPS-to-UTC association, host/PHY bias and independent UTC error remain unknown.
RX prediction, internal estimator RMS and delivered agreement are separate
measurements; point-sampled availability does not qualify continuous delivery.

The comparison is after acquisition. A separate first-minute RC attempt had
1,915 USB samples, zero valid UTC replies and one request error during delayed
BLE reacquisition. First sampled precision lock was at 96.907 seconds. That
failed population remains private and separate; its cause is not established.
It is not included in or represented as success by the settled comparison.
Both two-hour trials were still in progress at this evidence cutoff. The CSVs
do not establish complete-trial performance, durability or clean-bus acceptance.

Copyright (c) 2026 Jonathan Yantis.
