# Optional ChronyControl monitoring

Copyright (c) 2026 Jonathan Yantis. MIT.

[ChronyControl, by Bryan Christianson](https://www.whatroute.net/chronycontrol.html), is an optional,
separately installed macOS application. It is not bundled with PToB. Version
1.7.0 can display PToB's private user-level chronyd and tracking logs using the
adapter below. Follow the [build guide](build.md) first.

Install the signed app from its official download into `~/Applications`, or use
`brew install --cask chronycontrol` on a normally configured Homebrew host.
Opening the app can prompt for its privileged helper. The log chart is available
after Continue; live command queries can require that helper on some hosts.
When an enabled helper invokes the adapter as root, the adapter drops to the
trial folder's owner before reading its state or querying the socket. Do not use
Configure / Install chronyd or its daemon start, stop, restart or synchronization
actions for this workflow. Those manage the application's separate system
service and can change macOS time settings.

## Prepare and run

Choose an attached compatible T096 by its full USB serial, with appropriate
live-use authorization. Preparation only creates a new owner-only evidence
folder; it does not open USB or launch a daemon:

```sh
python3 clients/macos/chrony/chronycontrol.py \
  --state-dir clients/macos/runs/chronycontrol-trial \
  --device TC-00-0000000000000001 --duration 7200
```

Replace the synthetic serial with the exact device identity. Use the absolute
paths printed by preparation in ChronyControl Settings / General:

| Field | Value |
| --- | --- |
| chronyc | `STATE_DIRECTORY/chronyc` |
| Configuration | `STATE_DIRECTORY/monitor.conf` |
| chronyd | Absolute path to `clients/macos/.build/chrony/bin/chronyd` |
| Adjust System Clock | Off |
| Launch at Login | Off |

Run the printed `python3 …/chronycontrol.py run …` command in a terminal. It
opens only the pinned USB timing interface and starts PToB's owned unprivileged
`chronyd -x -U`; it does not open a CDC console or flash a device. `--duration`
is bounded to 7200 seconds. Keep the host awake for the trial; optional
`caffeinate -i python3 …/chronycontrol.py run …` inhibits idle sleep for that
command's lifetime. Interrupting the command stops its owned client and daemon.
There is no automatic launch service or future schedule.

The client retains its normal fine-input validity and source-expiry fences.
The adapter enables append-only chrony tracking/refclock/statistics logs, updates
a local socket pointer on each owned daemon start, and permits only read-only
ChronyControl queries. Its GUI adapter cannot execute `makestep`, `reset sources`,
`burst` or `shutdown`. Native PToB still uses its real chronyc internally for
its own source lifecycle. NTP and command UDP ports stay disabled.

Select **tracking**, **sources**, or **sourcestats** in the main window. Choose
**Live Tracking** to graph this run's `tracking.log`; check the window title
matches the intended evidence directory. The graph reads the file configured
by `logdir` in `monitor.conf`. Existing graph windows can retain the previous
file: close and reopen Live Tracking after selecting a new trial.

## Evidence and interpretation

The folder retains `capture.jsonl`, `client.log`, `logs/tracking.log`, `logs/refclocks.log`,
`logs/statistics.log`, `daemon-epochs.jsonl`, the last actual daemon config, session
metadata and a final `summary.json`. Empty logs may be absent until the source
has supplied suitable samples. Preparation refuses to overwrite an existing
folder. Runtime sockets are removed on exit; the chart can still display the
retained tracking log after completion. These directories remain ignored; review
and curate evidence separately before publication.

`COMPLETE` means the capture process exited normally; it is not a stability or
accuracy pass. The summary preserves valid and missing 1 Hz status populations,
authority fences and daemon restarts. Point samples do not establish continuous
availability. Missing input and unsynchronised stratum-0 zero offsets are not
zero-error measurements. Daemon epochs, startup transients and source changes
must remain distinguishable even if ChronyControl draws connecting lines.

Last offset, offset standard deviation and frequency describe chrony's estimate
against the experimental USB feed. They do not measure independent UTC accuracy.
The 50 ms precision and 200 ms delay remain provisional allowances; initial UTC
and host/PHY bias remain unknown. No network fallback or system clock discipline
is enabled. For a separate offline report after the run:

```sh
clients/macos/ptob --report clients/macos/runs/chronycontrol-trial/capture.jsonl \
  --report-out clients/macos/runs/chronycontrol-trial/report
python3 clients/macos/chrony/test_chronycontrol.py
```

The report and adapter tests do not operate hardware or start chronyd. To remove
the optional integration, close ChronyControl and keep or remove the local trial
folder; the app can be removed independently from `~/Applications`. No PToB
system service, system configuration or installed chrony binaries need removal.
