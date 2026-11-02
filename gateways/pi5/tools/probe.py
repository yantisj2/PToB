#!/usr/bin/env python3
"""Read-only Pi 5 BLE/PTP capability report. Does not scan, patch or steer clocks.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import argparse
import datetime
import fcntl
import hashlib
import json
from pathlib import Path
import platform
import shutil
import struct
import subprocess


def read(path):
    try:
        return Path(path).read_text().strip("\x00\n ")
    except (OSError, UnicodeError):
        return None


def command(argv, timeout=15):
    if not shutil.which(argv[0]):
        return {"available": False}
    try:
        run = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return {"available": True, "returncode": run.returncode,
                "stdout": run.stdout[-32768:], "stderr": run.stderr[-4096:]}
    except subprocess.TimeoutExpired:
        return {"available": True, "timeout": True}
    except OSError as error:
        return {"available": True, "error": str(error)}


def ptp_caps(device):
    # Linux UAPI: _IOR('=', 1, struct ptp_clock_caps), twenty 32-bit integers.
    try:
        with open(device, "rb", buffering=0) as f:
            data = fcntl.ioctl(f.fileno(), 0x80503D01, bytes(80))
        fields = struct.unpack("=20i", data)
        return dict(zip(("max_adj", "n_alarm", "n_ext_ts", "n_per_out", "pps",
                         "n_pins", "cross_timestamping", "adjust_phase", "max_phase_adj"), fields))
    except OSError as error:
        return {"unavailable": str(error)}


def collect():
    model = read("/proc/device-tree/model")
    report = {
        "schema": 1, "target": "raspberry-pi-5", "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "model": model, "serial": read("/proc/device-tree/serial-number"),
        "architecture": platform.machine(), "kernel": platform.release(),
        "boot_id": read("/proc/sys/kernel/random/boot_id"),
        "image_id": read("/opt/ptob/pi5/IMAGE_ID"),
        "source_sha256": globals().get("PTOB_SOURCE_SHA256") or hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "os_release": read("/etc/os-release"),
        "precision_input": {"pps_device": read('/sys/class/pps/' + Path('/dev/pps-ptob').resolve().name + '/name'),
                            "capture": "GPIO IRQ when ptob-pps is present; not PHC EXTTS"},
        "ptp_serving": {"configuration_present": Path('/etc/ptob/pi5/ptp4l.conf').is_file(),
                        "accuracy_qualified": False},
        "commands": {}, "ptp_clocks": {}, "bluetooth": {}, "firmware": {},
    }
    commands = {
        "ethernet_driver": ["ethtool", "-i", "eth0"],
        "ethernet_timestamping": ["ethtool", "-T", "eth0"],
        "bluetooth_info": ["btmgmt", "info"],
        "bluetooth_version": ["hciconfig", "hci0", "-a"],
        "packages": ["dpkg-query", "-W", "-f=${binary:Package} ${Version}\n", "bluez", "linuxptp",
                     "ethtool", "python3", "cloud-init", "firmware-brcm80211", "raspi-firmware", "linux-image-*"],
        "cloud_init": ["cloud-init", "status", "--format", "json"],
        "time_services": ["systemctl", "is-active", "systemd-timesyncd", "chrony", "ptp4l", "phc2sys", "timemaster",
                          "ptob-phc2sys", "ptob-ptp4l", "ptob-ptp-wifi"],
        "ptp_service_policy": ["systemctl", "is-enabled", "ptp4l", "phc2sys", "timemaster", "ptp4l@", "phc2sys@"],
        "addresses": ["ip", "-json", "address", "show"],
        "controller_boot": ["journalctl", "-b", "-k", "--no-pager", "-n", "60", "--grep=Bluetooth.*(BCM|firmware|features|chip id)"],
    }
    for name, argv in commands.items():
        report["commands"][name] = command(argv)
    for path in sorted(Path("/sys/class/ptp").glob("ptp*")):
        report["ptp_clocks"][path.name] = {"name": read(path / "clock_name"),
                                           "caps": ptp_caps("/dev/" + path.name)}
    for path in sorted(Path("/sys/class/bluetooth").glob("hci*")):
        report["bluetooth"][path.name] = {"address": read(path / "address"),
                                           "device": str((path / "device").resolve())}
    for path in sorted(Path("/lib/firmware/brcm").glob("BCM4345*.hcd")):
        try:
            report["firmware"][path.name] = {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                             "resolved_file": str(path.resolve())}
        except OSError as error:
            report["firmware"][path.name] = {"unavailable": str(error)}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expect-serial")
    args = parser.parse_args()
    # Reject a different device before querying its controllers or clocks.
    if args.expect_serial and read("/proc/device-tree/serial-number") != args.expect_serial:
        raise SystemExit("exact Pi serial mismatch")
    if platform.system() != "Linux":
        raise SystemExit("run this read-only probe on the Linux target")
    print(json.dumps(collect(), indent=2))


if __name__ == "__main__":
    main()
