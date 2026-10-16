#!/usr/bin/env python3
"""Foreground, user-owned chronyd -x plus the USB bridge. No system install.

An existing chrony instance is attached directly using ptob's command socket
options; this runner always owns a new private daemon/configuration.
"""
from __future__ import annotations

import argparse
import json
import os
import pwd
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import threading
import time

HERE = Path(__file__).resolve().parents[1]


def configuration(runtime: Path, detailed: bool = True) -> str:
    # Chrony's config tokeniser does not accept arbitrary whitespace in paths.
    if not runtime.is_absolute() or any(c.isspace() for c in str(runtime)):
        raise ValueError("runtime must be an absolute path without whitespace")
    if len(os.fsencode(runtime / "command.sock")) >= 104:
        raise ValueError("Darwin Unix socket path is too long")
    return f"""# Experimental BLE-derived UTC via a selected USB relay; no accuracy qualification.
# -x is mandatory: keep the virtual NTP clock, never adjust CLOCK_REALTIME.
# refclock stratum0 yields chronyd stratum1 only after actual selection.
# 50ms precision / 200ms delay are provisional engineering allowances,
# not measured bounds for currently unknown source UTC error.
refclock SOCK {runtime}/BLE.sock refid BLE stratum 0 poll 0 filter 4 precision 0.05 delay 0.2
minsamples 4
maxsamples 64
maxclockerror 10
maxdistance 1
maxupdateskew 1000
port 0
cmdport 0
bindcmdaddress {runtime}/command.sock
pidfile {runtime}/chronyd.pid
driftfile {runtime}/chrony.drift
logdir {runtime}
{('log tracking refclocks statistics' if detailed else '# Detailed chrony logs OFF for indefinite service')}
"""


def stop(child: subprocess.Popen | None) -> None:
    if child is None or child.poll() is not None:
        return
    child.terminate()
    try:
        child.wait(timeout=4)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait(timeout=2)


def run(args: argparse.Namespace) -> int:
    state = args.state_dir.expanduser().resolve()
    state.mkdir(mode=0o700, parents=False, exist_ok=False)
    runtime = Path(tempfile.mkdtemp(prefix="ptob-ch-", dir="/private/tmp"))
    # /private/tmp can pass down its group; chrony requires the command-socket
    # directory to match the selected user's primary group, even with -U.
    os.chown(runtime, -1, pwd.getpwuid(os.getuid()).pw_gid)
    daemon = client = None
    interrupted = False
    errors: list[str] = []
    started = time.time()

    def interrupt(_number, _frame):
        nonlocal interrupted
        interrupted = True

    for number in (signal.SIGINT, signal.SIGTERM):
        signal.signal(number, interrupt)
    config = configuration(runtime, detailed=not args.continuous)
    (state / "chrony.conf").write_text(config)
    (runtime / "chrony.conf").write_text(config)
    metadata = {"schema": 1, "runtime": str(runtime), "clock_control": "DISABLED_-x",
                "source": "BLE-derived UTC through an identity-bound USB relay", "precision_qualified": False,
                "started_wall_s": started, "continuous": args.continuous}
    (state / "session.json").write_text(json.dumps(metadata, indent=2) + "\n")
    log_reader = None
    daemon_log = None
    log_drops = [0]
    try:
        daemon_log = (state / "chronyd.log").open("xb")
        command = [str(args.chronyd), "-x", "-U", "-u", pwd.getpwuid(os.getuid()).pw_name,
                   "-d", "-4", "-f", str(runtime / "chrony.conf")]
        metadata["chronyd_command"] = command
        daemon = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        def drain_log():
            kept = 0
            while data := daemon.stdout.read(4096):
                keep = min(len(data), max(0, 8 * 1024 * 1024 - kept))
                daemon_log.write(data[:keep])
                kept += keep
                log_drops[0] += len(data) - keep
            daemon_log.flush()
        log_reader = threading.Thread(target=drain_log, daemon=True)
        log_reader.start()
        limit = time.monotonic() + 5
        while not (runtime / "BLE.sock").exists() or not (runtime / "command.sock").exists():
            if interrupted or daemon.poll() is not None or time.monotonic() >= limit:
                raise RuntimeError("chronyd did not create its private sockets; inspect chronyd.log")
            time.sleep(0.05)
        command = [str(args.client), "--chrony-sock", str(runtime / "BLE.sock"),
                   "--chrony-command-socket", str(runtime / "command.sock"), "--chronyc", str(args.chronyc),
                   "--jsonl", str(state / "capture.jsonl"), "--no-ntp"]
        command += ["--continuous"] if args.continuous else ["--duration", str(args.duration)]
        if args.no_interactive:
            command.append("--no-interactive")
        metadata["client_command"] = command
        print(f"Private chrony instance: {runtime}\nEvidence: {state}", flush=True)
        client = subprocess.Popen(command)
        while not interrupted and client.poll() is None:
            if daemon.poll() is not None:
                raise RuntimeError("owned chronyd exited during the run")
            time.sleep(0.1)
        if not interrupted and client.returncode:
            raise RuntimeError(f"USB client exited {client.returncode}")
        stop(client)
        stop(daemon)
        log_reader.join(timeout=3)
        return 0
    except (OSError, RuntimeError) as error:
        errors.append(str(error))
        print(str(error), file=__import__("sys").stderr)
        return 1
    finally:
        # Signal only these two child handles; never pkill, daemon-wide commands,
        # firmware resets, port seizure or changes to macOS timed.
        stop(client)
        stop(daemon)
        if log_reader:
            log_reader.join(timeout=3)
        if daemon_log:
            daemon_log.close()
        for path in runtime.iterdir():
            if path.is_file() and not path.is_socket():
                shutil.copy2(path, state / path.name)
        metadata.update(ended_wall_s=time.time(), interrupted=interrupted, errors=errors,
                        client_exit=client.returncode if client else None,
                        chronyd_exit=daemon.returncode if daemon else None, chronyd_diagnostic_bytes_dropped=log_drops[0])
        (state / "session.json").write_text(json.dumps(metadata, indent=2) + "\n")
        shutil.rmtree(runtime)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-dir", type=Path, required=True, help="new evidence directory; parent must exist")
    parser.add_argument("--chronyd", type=Path, default=HERE / ".build/chrony/bin/chronyd")
    parser.add_argument("--chronyc", type=Path, default=HERE / ".build/chrony/bin/chronyc")
    parser.add_argument("--client", type=Path, default=HERE / "ptob")
    lifetime = parser.add_mutually_exclusive_group()
    lifetime.add_argument("--duration", type=float, default=600)
    lifetime.add_argument("--continuous", action="store_true")
    parser.add_argument("--no-interactive", action="store_true")
    args = parser.parse_args()
    if not 0 < args.duration <= 1800:
        parser.error("duration must be 0..1800s; use --continuous for normal indefinite service")
    for name in ("chronyd", "chronyc", "client"):
        binary = getattr(args, name).expanduser().resolve()
        if not binary.is_file() or not os.access(binary, os.X_OK):
            parser.error(f"missing executable {binary}; run chrony/build.sh and ../build.sh")
        setattr(args, name, binary)
    try:
        return run(args)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
