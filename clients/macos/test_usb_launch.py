#!/usr/bin/env python3
"""Exercise real shell PATH/symlink launches with an isolated synthetic helper.

No USB discovery or hardware access: the test copies the client into a private
fixture and supplies only a synthetic helper beside that copy.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
checks = 0


def check(ok, label):
    global checks
    assert ok, label
    checks += 1


def main():
    # Retain the reproduction and all outcomes, including failures.
    root = Path(tempfile.mkdtemp(prefix="usb-launch-", dir=HERE / ".build"))
    app = root / "app with spaces"
    helper = app / "usb/reference-probe"
    helper.parent.mkdir(parents=True)
    shutil.copy2(HERE / ".build/ptob-receiver", app / "ptob-receiver")
    helper.write_text("#!/bin/sh\necho '{\"event\":\"manifest\",\"topology\":[],\"serial\":\"TC-04-0000000000000004\",\"silicon\":\"0000000000000004\",\"launch_test_helper\":true}'\n")
    helper.chmod(0o700)
    (root / "bin").mkdir()
    (root / "bin/ptob-launch-test").symlink_to("../second-link")
    (root / "second-link").symlink_to(app / "ptob-receiver")
    cwd = root / "unrelated working directory"
    (cwd / "usb").mkdir(parents=True)
    # argv[0]-based discovery would run this decoy from CWD instead.
    decoy = cwd / "usb/reference-probe"
    decoy.write_text("#!/bin/sh\necho '{\"event\":\"manifest\",\"wrong_cwd_helper\":true}'\n")
    decoy.chmod(0o700)
    environment = dict(os.environ, PATH=str(root / "bin") + ":" + os.environ["PATH"])
    for name, duration in (("duration", True), ("default-duration", False)):
        capture = root / f"{name}.jsonl"
        command = 'ptob-launch-test --no-interactive --no-ntp --jsonl "$1"'
        if duration:
            command += " --duration 3"
        task = subprocess.Popen(["/bin/zsh", "-f", "-c", command, "launch-test", str(capture)],
                                cwd=cwd, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if not duration:
            # Wait for helper admission rather than assuming a copied ad-hoc
            # executable and its shell helper start within 400 milliseconds.
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if capture.exists() and 'launch_test_helper' in capture.read_text():
                    break
                time.sleep(.025)
            task.terminate()
        stdout, stderr = task.communicate(timeout=6)
        (root / f"{name}.stdout").write_bytes(stdout)
        (root / f"{name}.stderr").write_bytes(stderr)
        rows = [json.loads(s) for s in capture.read_text().splitlines()]
        check(task.returncode == 0, f"PATH launch {name} closes successfully; {root}")
        raw = [r["helper"] for r in rows if r["type"] == "usb_raw"]
        check(any(r.get("launch_test_helper") for r in raw), f"PATH launch {name} starts adjacent helper; {root}")
        check(not any(r.get("wrong_cwd_helper") for r in raw), f"PATH launch {name} ignores CWD helper; {root}")
        check(not any(r["type"] == "usb_observer_error" for r in rows), f"PATH launch {name} has no missing-helper error; {root}")
        check(rows[-1]["type"] == "usb_end", f"PATH launch {name} retains closed capture; {root}")
        check(rows[0]["chrony_input_mode"] == "EXPERIMENTAL_FINE_UTC_ESTIMATE", f"PATH launch {name} defaults to fine input; {root}")
        check(not any(r["type"] == "chrony_refclock_sample" and r.get("input_method") == "COARSE_USB_REQUEST_MIDPOINT" for r in rows), f"PATH launch {name} cannot silently fall back to coarse input; {root}")

    helper.write_text("#!/usr/bin/python3\nimport json,time\n"
        "print(json.dumps({'event':'manifest','serial':'TC-04-0000000000000004','silicon':'0000000000000004'}),flush=True)\n"
        "print(json.dumps({'event':'sample','precision_source':None,'precision_source_error_code':-536870186,'precision_source_error_domain':'IOUSBHostErrorDomain','precision_source_error':'Control request timed out'}),flush=True)\n"
        "time.sleep(2)\n")
    capture = root / "control-timeout.jsonl"
    task = subprocess.run([str(app / "ptob-receiver"), "--no-chrony", "--duration", "1.5", "--no-ntp", "--no-interactive", "--jsonl", str(capture)],
                          cwd=cwd, capture_output=True, text=True, timeout=7)
    rows = [json.loads(s) for s in capture.read_text().splitlines()]
    check(task.returncode == 0 and "USB control timeout" in task.stdout, "control timeout is visible on the main face without opening details")
    failure = next(r for r in rows if r["type"] == "usb_transport_error")
    check(failure["error_code"] == -536870186 and failure["source_permission"].startswith("NO_RESPONSE"), "transport error code survives without inferring source loss")
    check(all(r.get("chrony_user_utc_ns") is None and r.get("shadow_ns") is None for r in rows if r["type"] == "usb_status"), "timeout diagnostics cannot fabricate UTC")

    helper.unlink()
    capture = root / "missing-helper.jsonl"
    task = subprocess.run([str(app / "ptob-receiver"), "--duration", "0.2", "--no-ntp", "--no-interactive", "--jsonl", str(capture)],
                          cwd=cwd, capture_output=True, text=True, timeout=6)
    rows = [json.loads(s) for s in capture.read_text().splitlines()]
    check("USB ERROR  USB helper missing; rebuild the CLI. Retrying." in task.stdout, "missing helper is visible without details")
    failure = next(r for r in rows if r["type"] == "usb_observer_error")
    check(failure["helper_path"] == str(helper) and failure["diagnostic"], "actual helper path and OS diagnostic retained")
    print(f"PASS {checks} shell PATH/symlink/helper checks; synthetic helper only; {root}")


if __name__ == "__main__":
    main()
