# User-space chrony integration

Copyright (c) 2026 Jonathan Yantis. MIT documentation; chrony retains its GPL terms.

Build with `sh clients/macos/chrony/build.sh`, then build the client as described
in [the macOS guide](../../../docs/macos/build.md). Normal live CLI launch starts
an owned foreground chronyd 4.9 with `-x -U -u CURRENT_USER`. It feeds a SOCK
refclock named `BLE` and observes a separate corrected UTC user clock through a
private command socket. The daemon does not discipline the macOS system clock.
No privileged helper, LaunchAgent or system synchronization setting is installed.

Runtime sockets are local, owner-only and use short paths under `/private/tmp`.
`port 0` and `cmdport 0` disable network NTP/command service. There is no supplied
network-sharing profile. Closing the client stops/reaps only its owned daemon.
Source/root/boot/path changes, genuine authority loss, expiry, transport changes
and wall steps recreate that private daemon so old refclock filters cannot cross
source scopes. Short missing-SOF-map observations can preserve calibration only
within an already granted original authority deadline; they supply no time and
cannot renew permission. Queued feeds are cancelled while output is withheld.

The default fine feed is explicitly an **unqualified UTC estimate**: causal
SOF/Mach map + exact source map + retained integer published offset. It never
constructs a verified UTC anchor. Initial UTC and host/PHY bias, PPS association,
leap readiness and absolute UTC remain unknown. Offset mutation or overlapping
coordinate discontinuity quarantines the estimate. `--coarse-chrony` explicitly
uses the USB request midpoint; it cannot silently enter the fine filter.

The refclock is stratum 0; chronyd reports stratum 1 only after actual selection.
This hierarchy does not qualify source accuracy. No forced selection, fabricated
PPS, `trust`, `local stratum 1` or network fallback is configured. The configured
50 ms precision and 200 ms delay are provisional engineering allowances, not
measured source bounds. Chrony's RMS is its estimator statistic. A direct
chrony-versus-fine comparison shares the input source and is not independent UTC
metrology. Preserve startup transients, withheld observations and populations.

Read-only attachment to an existing **local** instance:

```sh
clients/macos/ptob --chrony-only \
  --chronyc /absolute/path/to/chronyc \
  --chrony-command-socket /absolute/path/to/command.sock --duration 60
```

This invokes `chronyc tracking`, opens no USB and does not reset that instance.
Without `--chrony-only`, USB diagnostics remain active. Explicit `--chrony-sock`
feeding authorizes `reset sources` at source/epoch/wall/gap fences. Chrony 4.9
reset does not clear the pre-poll filter, so external feeding withholds
source-bound delivered UTC; use owned mode for isolated source-bound output.
Read-only attachment remains available to monitor another application's daemon.

`chrony/run.py --state-dir NEW_DIRECTORY` is an optional foreground wrapper for
separate evidence folders; it starts a private daemon and live client and needs
live-run authorization. It is not part of the offline tests. `--continuous`
keeps healthy foreground service running while capture is bounded to the first
600 seconds. Native diagnostics retain an 8 KiB tail; the optional wrapper caps
its own message log at 8 MiB. No background service is installed automatically.
