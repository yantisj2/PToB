#!/usr/bin/env python3
"""Terminal page/replay checks using a synthetic chronyc, without USB access."""
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import shutil
import struct
import subprocess
import tempfile
import termios
import time

HERE = Path(__file__).resolve().parent
BINARY = HERE / ".build/ptob-receiver"


def main():
    root = Path(tempfile.mkdtemp(prefix="chrony-ui-", dir=HERE / ".build"))
    checks = 0

    def check(ok, label):
        nonlocal checks
        assert ok, f"{label}; evidence: {root}"
        checks += 1

    tracking = {"reference_name": "BLE", "refid": "424C4500", "stratum": 1,
                "ntp_minus_system_s": .025, "last_offset_s": -.001, "rms_offset_s": .002,
                "frequency_ppm_fast": 10., "residual_frequency_ppm": -.1, "skew_ppm": .2,
                "root_delay_s": .2, "root_dispersion_s": .05, "update_interval_s": 1., "leap": "Normal"}
    chrony = {"enabled": True, "fresh": True, "tracking": tracking, "last_tracking": tracking,
              "user_utc_ns": "1790000000025000000", "query_age_ns": "250000000", "reference_age_s": .5,
              "query_span_ns": "2000000", "checks": 42, "failures": 0}
    status = {"type": "usb_status", "tick_ns": "2000000000", "host_wall_ns": "1790000000000000000",
              "wall_sample_before_ns": "2000000000", "wall_sample_after_ns": "2000000000", "shadow_ns": None,
              "chrony_user_utc_ns": "1790000000025000000", "chrony": chrony, "transport": "CHRONY",
              "chrony_refclock": {"enabled": True, "ready": True, "sent": 40, "rejected": 3, "failures": 0, "resets": 2},
              "chrony_comparison": {"rms_ns": 26000000., "valid_status_samples": 30, "unavailable_status_samples": 12}}
    for state in ("tracking", "withheld", "failed", "disconnected"):
        row = json.loads(json.dumps(status))
        if state != "tracking":
            row["chrony"].update(fresh=False, user_utc_ns=None, tracking=None)
            row["chrony_user_utc_ns"] = None
        if state == "failed":
            row["chrony"].update(error="Synthetic chronyc unavailable", failures=1)
        if state == "disconnected":
            row["chrony"] = {"enabled": False}
        capture = root / f"{state}.jsonl"
        capture.write_text(json.dumps({"type": "usb_start", "tick_ns": "1000000000"}) + "\n" + json.dumps(row) + "\n")
        for width, height in ((80, 24), (120, 32), (40, 16)):
            page = subprocess.run([str(BINARY), "--instrument-preview", str(capture), "--page", "chrony",
                                   "--columns", str(width), "--rows", str(height)], capture_output=True, text=True, timeout=5)
            check(page.returncode == 0, f"{state} preview renders at {width}x{height}")
            plain = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", page.stdout)
            (root / f"{state}-{width}x{height}.txt").write_text(plain)
            check("CHRONY / STATISTICS" in plain and "c Chrony" in plain, "page and navigation visible")
            if state == "tracking":
                check("TRACKING" in plain and "BLE / S1" in plain, "current selected source retained")
            elif state == "withheld":
                check("USER UTC  WITHHELD" in plain and ("LAST SUCCESSFUL SNAPSHOT" in plain or "(last)" in plain), "stale readings identified")
            elif state == "failed":
                check("CHECK FAILED" in plain and "WITHHELD" in plain, "query failure cannot show live UTC")
            else:
                check("NOT CONNECTED" in plain and "not connected" in plain, "disabled page offers setup")

    # Exercise the actual terminal keyboard route, not just renderer flags.
    client = root / "ptob-receiver"
    shutil.copy2(BINARY, client)
    fake = root / "synthetic-chronyc"
    fake.write_text("#!/usr/bin/python3\nimport time\nprint(f'424C4500,BLE,1,{time.time()+.025:.9f},.025,-.001,.002,10,-.1,.2,.2,.05,1,Normal')\n")
    fake.chmod(0o700)
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    capture = root / "interactive.jsonl"
    task = subprocess.Popen([str(client), "--chrony-only", "--chronyc", str(fake), "--chrony-command-socket", "/tmp/ptob-synthetic-unused.sock",
                             "--duration", "20", "--no-ntp", "--jsonl", str(capture)], stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)
    output = bytearray()

    def wait_for(needle, cursor):
        limit = time.monotonic() + 5
        while time.monotonic() < limit:
            if needle.encode() in output[cursor:]:
                return
            if select.select([master], [], [], .1)[0]:
                try:
                    data = os.read(master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not data:
                    break
                output.extend(data)
        raise AssertionError(f"terminal did not show {needle!r}; evidence: {root}")

    def drain_until_exit(timeout=5):
        # A real terminal keeps consuming repaint/restore output. A PTY test
        # must do the same while waiting, or it can block the display drain.
        limit = time.monotonic() + timeout
        while task.poll() is None and time.monotonic() < limit:
            if select.select([master], [], [], .05)[0]:
                try:
                    output.extend(os.read(master, 65536))
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    time.sleep(.01)
        task.wait(timeout=.5)

    try:
        wait_for("CHRONY  TRACKING", 0)
        wait_for("NTP−SYS", 0)
        check(b"CHRONY EST RMS" in output and b"NTP\xe2\x88\x92SYS" in output, "live main face includes essentials")
        for key, title in ((b"c", "CHRONY / STATISTICS"), (b"d", "SOURCE & TRANSPORT DIAGNOSTICS"),
                           (b"h", "INSTRUMENT CONTROLS"), (b"1", "CHRONY UTC / USER CLOCK"),
                           (b"c", "CHRONY / STATISTICS"), (b"c", "CHRONY UTC / USER CLOCK")):
            cursor = len(output)
            os.write(master, key)
            wait_for(title, cursor)
            checks += 1
        os.write(master, b"q")
        drain_until_exit()
        check(task.returncode == 0, "q closes from the page route")
    finally:
        if task.poll() is None:
            task.terminate()
            try:
                drain_until_exit()
            except subprocess.TimeoutExpired:
                task.kill()
                task.wait(timeout=2)
        (root / "terminal.ansi").write_bytes(output)
        os.close(master)
    rows = [json.loads(s) for s in capture.read_text().splitlines()]
    check([r["to"] for r in rows if r["type"] == "instrument_page"] == ["chrony", "details", "help", "main", "chrony", "main"], "keyboard route matches exclusive page state")
    check(not any(r["type"] in ("usb_raw", "usb_observer_error", "usb_observer_end") for r in rows), "chrony-only UI test never opens USB")
    check(rows[-1]["type"] == "usb_end", "UI capture closes after readers stop")
    print(f"PASS {checks} chrony page/replay/terminal checks; synthetic chronyc only; {root}")


if __name__ == "__main__":
    main()
