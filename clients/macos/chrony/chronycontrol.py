#!/usr/bin/env python3
"""Optional ChronyControl observer for PToB's owned fine-feed daemon.

Copyright (c) 2026 Jonathan Yantis. MIT.
Installs no app, helper, service, network listener or system clock control.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import pwd
import shlex
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parents[1]
READ_COMMANDS = {"tracking", "sources", "sourcestats", "activity", "ntpdata",
                 "selectdata", "authdata", "serverstats", "clients", "smoothing"}


def save(path: Path, value: dict) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def load(state: Path) -> dict:
    if state.stat().st_uid != os.getuid() or state.stat().st_mode & 0o077:
        raise ValueError("state directory must belong to this user and be mode 700")
    return json.loads((state / "session.json").read_text())


def daemon(state: Path, arguments: list[str]) -> None:
    session = load(state)
    if not {"-x", "-U", "-d", "-f"}.issubset(arguments):
        raise ValueError("adapter requires PToB's foreground chronyd -x configuration")
    config = Path(arguments[arguments.index("-f") + 1])
    runtime = config.parent
    if not str(runtime).startswith("/private/tmp/ptob-ch-") or runtime.stat().st_uid != os.getuid():
        raise ValueError("expected an owned PToB private runtime")
    original = config.read_text()
    if not {"port 0", "cmdport 0"}.issubset(original.splitlines()):
        raise ValueError("network ports must remain disabled")
    # Keep native source fencing/recreation. Logs append across daemon epochs;
    # the event log preserves their separate identities and interruption times.
    logs = state / "logs"
    logs.mkdir(mode=0o700, exist_ok=True)
    config.write_text(original + f"\nlogdir {logs}\nlog tracking refclocks statistics\n")
    (state / "last-chrony.conf").write_text(config.read_text())
    pointer = state / "active-command.sock"
    pointer.unlink(missing_ok=True)
    pointer.symlink_to(runtime / "command.sock")
    with (state / "daemon-epochs.jsonl").open("a") as output:
        output.write(json.dumps({"wall_s": time.time(), "pid": os.getpid(),
                                 "runtime": str(runtime), "system_clock_control": False}) + "\n")
    os.execv(session["chronyd"], [session["chronyd"], *arguments])


def query(state: Path, arguments: list[str]) -> None:
    # An enabled ChronyControl helper can invoke its chronyc path as root.
    # Return to the evidence owner's identity before reading state or querying.
    owner = state.stat()
    if os.geteuid() == 0 and owner.st_uid != 0:
        account = pwd.getpwuid(owner.st_uid)
        os.setgroups([])
        os.setgid(account.pw_gid)
        os.setuid(owner.st_uid)
    session = load(state)
    if any(c in "\n\r;" for token in arguments for c in token):
        raise ValueError("one read-only monitoring command is required")
    if arguments in (["-v"], ["--version"]):
        os.execv(session["chronyc"], [session["chronyc"], "-v"])
    # ChronyControl may supply a host or a quoted command. Always force our
    # private socket and reject every mutating or unrecognised command.
    flags, commands = [], []
    index = 0
    while index < len(arguments):
        token = arguments[index]
        if token in {"-h", "-p"}:
            index += 2
            continue
        if token in {"-n", "-N", "-c", "-4", "-6"}:
            flags.append(token)
        elif token == "-m":
            pass
        else:
            commands.extend(shlex.split(token))
        index += 1
    if (not commands or commands[0] not in READ_COMMANDS or
            any(c in "\n\r;" for token in commands for c in token) or
            any(token.startswith("-") and token not in {"-v", "-a"} for token in commands[1:]) or
            any(token in READ_COMMANDS for token in commands[1:])):
        raise ValueError("ChronyControl adapter accepts one read-only monitoring command")
    # Evidence directory names can exceed macOS's Unix socket path limit.
    # The owned runtime already has a short path; resolve the observer pointer.
    socket = (state / "active-command.sock").resolve()
    os.execv(session["chronyc"], [session["chronyc"], "-h", str(socket),
                                *flags, *commands])


def prepare(args: argparse.Namespace) -> Path:
    state = args.state_dir.expanduser().resolve()
    if any(c.isspace() for c in str(state)):
        raise ValueError("chrony log directory must not contain whitespace")
    state.mkdir(mode=0o700, parents=False, exist_ok=False)
    binaries = {name: str(path.expanduser().resolve()) for name, path in
                (("chronyd", args.chronyd), ("chronyc", args.chronyc), ("client", args.client))}
    for binary in binaries.values():
        if not os.access(binary, os.X_OK):
            raise ValueError(f"missing executable: {binary}; build the macOS client and chrony first")
    session = {"schema": 1, **binaries, "device": args.device, "duration_s": args.duration,
               "system_clock_control": False, "precision_qualified": False,
               "status": "PREPARED", "prepared_wall_s": time.time()}
    save(state / "session.json", session)
    (state / "logs").mkdir(mode=0o700)
    for name, mode in (("chronyd", "_daemon"), ("chronyc", "_query")):
        script = "#!/bin/sh\nexec " + shlex.join([sys.executable, str(Path(__file__).resolve()),
                                                mode, str(state)]) + ' "$@"\n'
        (state / name).write_text(script)
        (state / name).chmod(0o700)
    # ChronyControl reads logdir from this file. It does not run this config.
    (state / "monitor.conf").write_text(f"# Observer metadata only; use the owned PToB daemon.\nport 0\ncmdport 0\nlogdir {state}/logs\n")
    return state


def summarise(state: Path, session: dict) -> None:
    counts, reasons, rows = Counter(), Counter(), 0
    first = last = None
    with (state / "capture.jsonl").open() as capture:
        for line in capture:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                counts["malformed_rows"] += 1
                continue
            rows += 1
            kind = row.get("type", "unknown")
            counts[kind] += 1
            if "tick_ns" in row:
                tick = int(row["tick_ns"])
                first = tick if first is None else min(first, tick)
                last = tick if last is None else max(last, tick)
            if kind == "source_authority_fence":
                reasons[row.get("reason", "unknown")] += 1
            if kind == "usb_status":
                counts["status_with_chrony_utc" if row.get("chrony_user_utc_ns") is not None
                       else "status_without_chrony_utc"] += 1
    save(state / "summary.json", {"schema": 1, "status": session["status"],
         "requested_duration_s": session["duration_s"], "capture_span_s": (last-first)/1e9 if first is not None else 0,
         "rows": rows, "counts": dict(counts), "source_fences": dict(reasons),
         "accuracy_qualified": False, "interpretation": "Chrony estimator stability and availability; no independent UTC accuracy measurement."})


def run(state: Path) -> int:
    session = load(state)
    if session["status"] != "PREPARED":
        raise ValueError("session has already run; prepare a new directory")
    command = [session["client"], "--duration", str(session["duration_s"]), "--device", session["device"],
               "--chronyd", str(state / "chronyd"), "--chronyc", session["chronyc"],
               "--jsonl", str(state / "capture.jsonl"), "--no-ntp", "--no-interactive"]
    child = None
    interrupted = False

    def stop(_number, _frame):
        nonlocal interrupted
        interrupted = True
        if child is not None and child.poll() is None:
            child.terminate()

    for number in (signal.SIGTERM, signal.SIGINT):
        signal.signal(number, stop)
    session.update(status="RUNNING", started_wall_s=time.time(), pid=os.getpid(), command=command)
    save(state / "session.json", session)
    try:
        with (state / "client.log").open("x") as output:
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT)
            session["client_pid"] = child.pid
            save(state / "session.json", session)
            result = child.wait(timeout=session["duration_s"] + 60)
        session.update(status="INTERRUPTED" if interrupted else "COMPLETE" if result == 0 else "FAILED", client_exit=result)
    except Exception as error:
        if child is not None and child.poll() is None:
            child.terminate()
            child.wait(timeout=15)
        session.update(status="FAILED", error=str(error))
    finally:
        session["ended_wall_s"] = time.time()
        save(state / "session.json", session)
        (state / "active-command.sock").unlink(missing_ok=True)
        if (state / "capture.jsonl").exists():
            summarise(state, session)
    return 0 if session["status"] == "COMPLETE" else 1


def main() -> int:
    os.umask(0o077)
    if len(sys.argv) > 2 and sys.argv[1] in {"_daemon", "_query", "run"}:
        mode, state = sys.argv[1:3]
        if mode == "run":
            return run(Path(state))
        (daemon if mode == "_daemon" else query)(Path(state), sys.argv[3:])
        return 1
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-dir", type=Path, required=True, help="new private directory; parent must exist")
    parser.add_argument("--device", required=True, help="exact compatible T096 USB serial")
    parser.add_argument("--duration", type=float, default=7200)
    parser.add_argument("--chronyd", type=Path, default=HERE / ".build/chrony/bin/chronyd")
    parser.add_argument("--chronyc", type=Path, default=HERE / ".build/chrony/bin/chronyc")
    parser.add_argument("--client", type=Path, default=HERE / "ptob")
    args = parser.parse_args()
    if not 0 < args.duration <= 7200:
        parser.error("duration must be >0 and <=7200 seconds")
    state = prepare(args)
    print(f"Prepared {state}\nChronyControl chronyc: {state}/chronyc\nConfiguration: {state}/monitor.conf")
    print("Start the explicitly authorised live run:\n" + shlex.join([sys.executable, str(Path(__file__).resolve()), "run", str(state)]))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
