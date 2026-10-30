# macOS · USB time client and chrony bridge

Receive time from a compatible **T096 RC** or dedicated **DC GNSS/USB** image,
inspect the input stream and feed an experimental user clock through an owned,
unprivileged chronyd. The macOS system clock stays unchanged.

Timing comes from **hardware-captured USB frame boundaries**, matched to the
Mac's USB frame reference and monotonic clock. USB packets carry captured
records; variable delivery latency affects freshness rather than defining phase.

[Build and set up](../../docs/macos/build.md) ·
[Chrony integration](chrony/README.md) · [USB contract](usb/README.md) ·
[Development history](../../CHANGELOG.md)

## Choose a source

| Source | Timing path | What you need |
| --- | --- | --- |
| T096 **RC** | GM → BLE → T096 RC → USB → macOS | Compatible enrolled RC with fresh accepted BLE timing. |
| T096 **DC** | GNSS → T096 DC → USB → macOS | Separate [DC image](../../docs/t096/direct-connect.md), GNSS fix/PPS and compatible USB maps; no BLE or group enrollment. |

The normal T096 GM backend does not publish a USB source map. Direct Mac BLE
and StickS3 CDC timing input are not supplied. Connecting a diagnostic serial
port is not sufficient; the source must implement the
[T096 USB timing contract](usb/README.md).

## Capabilities and specifications

| Area | Current configuration |
| --- | --- |
| Host | Apple Silicon macOS is the checked build host; Swift/Foundation client with IOKit/IOUSBHost helper. Other hosts remain unvalidated. |
| Input | Hardware USB frame capture plus immutable device source map and host Mach clock association. Source/root/boot/path and counter changes fence old training. |
| User clock | Owned foreground **chronyd -x**, unprivileged, fed through a private SOCK refclock. Quarter-second polling, four-sample filter and 64-sample estimator limit. |
| Diagnostics | Live terminal clock, source status, corrected-source RX USB RMS, chrony tracking, bounded JSONL capture and offline precision replay. |
| Continuity | Compatible retained estimates appear as **HOLDOVER / LOCAL ONLY** with increasing age and unknown error bounds; they never become chrony measurements. |
| Isolation | Owner-only local sockets; no OS clock discipline, LAN time serving, LaunchAgent or system synchronization changes. Exit stops the owned daemon. |

## Build and check offline

Follow the [build guide](../../docs/macos/build.md) for Apple's command-line
tools, public dependencies, registry selection and the complete offline suite.
From the repository root:

```sh
sh clients/macos/chrony/build.sh
sh clients/macos/build.sh
clients/macos/ptob --self-test
```

The build compiles the client, USB helper and pinned chrony; it does not start
a service. The self-test neither opens USB nor launches chronyd. Use the same
[display registry](../../registry/README.md) as the firmware when building for
a deployment. Names never replace device identity or timing permission.

## Connect and use

Arrange access to a compatible source, then launch:

```sh
clients/macos/ptob
```

Normal launch opens USB and starts the owned local chronyd. `--no-chrony`
selects USB-only operation. Use `--device FULL_USB_SERIAL` to select an exact
discovered device; a display alias is not a USB identity. The client does not
flash firmware or change relay roles.

| Key | View or action |
| --- | --- |
| `c` | Chrony statistics |
| `d` | Source and receiver diagnostics |
| `h` | Help |
| `q` | Close the client and stop its owned daemon |

For a bounded USB baseline before chrony, use a fresh capture path and your
source's exact serial:

```sh
clients/macos/ptob --no-chrony --no-ntp --device FULL_USB_SERIAL \
  --rate 32 --duration 120 --jsonl clients/macos/runs/NEW-baseline.jsonl
clients/macos/ptob --precision-replay clients/macos/runs/NEW-baseline.jsonl
```

Replay and self-tests are offline. The baseline command is a live hardware
operation. [ChronyControl setup](../../docs/macos/chronycontrol.md) provides
optional GUI monitoring of a separately installed application.

## Read the timing statistics

**RX USB RMS** measures causal corrected-source prediction errors against
hardware USB SOF / Apple Mach frame boundaries, before chrony filtering.
A preceding **64–1024** valid-observation affine fit predicts each sample before
that sample enters training. Residual bias and transients remain; the separately
reported UTC offset is excluded.

Exact immutable-map agreement and resolved direct-port frame identity are
required. Rejected/unavailable samples, gaps above **500 ms** and source-scope
changes restart training. The readout needs four predictions and a source
observation younger than **500 ms**. Warm-up, rejection, qualification and missing
observations stay explicit in capture/replay.

RX USB RMS, the device's SOF span RMS, retrospective source jitter and chrony's
estimator RMS are different metrics. Chrony-versus-fine-source comparisons
share their input; they are not independent UTC measurements. Host/PHY bias and
UTC association remain unknown.

## Continuity, evidence and limits

Same-source quality expiry pauses chrony input while preserving the owned
process. Source/counter changes fence old training. Automatic rediscovery uses
fresh helper instances after stale transport errors, with one reader per silicon
identity. The **500 ms** display hold affects presentation only; withheld UTC
is withheld immediately. See the [continuous-estimate contract](../../docs/t096/continuous-estimate.md).

The [USB timing research draft](../../papers/usb-macos-time-transfer-preprint.pdf)
and [selected comparison evidence](../../evidence/usb/2026-10-17/README.md) retain
historical measurements, exact populations, rejections and withheld output.
Their shared-bus observations do not establish a controlled accuracy comparison.
Clean-bus validation, independent UTC/endpoint accuracy and continuous precision
availability remain open. Self-tests and successful builds do not qualify them.

Read [chrony isolation and interpretation](chrony/README.md) before attaching
another instance. [Source notices](NOTICE.md), [import provenance](imports/2026-10-16.json)
and the [project changelog](../../CHANGELOG.md) preserve dependency terms and
source history. Raw captures and generated outputs remain local and ignored.

Copyright (c) 2026 Jonathan Yantis. Original client code and documentation use
[MIT](../../LICENSE); chrony and other third-party terms are preserved.
