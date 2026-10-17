#!/usr/bin/env python3
"""Offline observer isolation checks. No USB access or chronyd process.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import chronycontrol as observer


class ObserverTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ptob-observer-test-", dir="/private/tmp")
        self.root = Path(self.temporary.name)
        self.state = self.root / "state"
        self.fake = self.root / "chronyc"
        self.fake.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
        self.fake.chmod(0o700)
        observer.prepare(argparse.Namespace(state_dir=self.state, device="TC-00-0000000000000001",
            duration=7200, chronyd=Path("/usr/bin/true"), chronyc=self.fake, client=Path("/usr/bin/true")))

    def tearDown(self):
        self.temporary.cleanup()

    def query(self, *args):
        return subprocess.run([str(self.state / "chronyc"), *args], text=True, capture_output=True)

    def test_read_only_queries_force_private_socket(self):
        for command in ("tracking", "sources -v", "sourcestats", "activity"):
            with self.subTest(command=command):
                result = self.query("-h", "127.0.0.1", command)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(str(self.state / "active-command.sock"), result.stdout)
                self.assertNotIn("127.0.0.1", result.stdout)

    def test_mutations_and_command_separators_are_rejected(self):
        for command in ("makestep", "shutdown", "reset sources", "burst 4/4", "tracking\nshutdown",
                        "tracking;shutdown", "sources -x", "-m tracking shutdown"):
            with self.subTest(command=command):
                self.assertNotEqual(self.query(command).returncode, 0)

    def test_long_evidence_path_uses_short_runtime_socket(self):
        state = self.root / ("long-evidence-" + "x" * 90)
        observer.prepare(argparse.Namespace(state_dir=state, device="TC-00-0000000000000001",
            duration=7200, chronyd=Path("/usr/bin/true"), chronyc=self.fake, client=Path("/usr/bin/true")))
        runtime = self.root / "runtime"
        runtime.mkdir()
        target = runtime / "command.sock"
        (state / "active-command.sock").symlink_to(target)
        self.assertGreater(len(str(state / "active-command.sock").encode()), 104)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
            server.bind(str(target))
            result = subprocess.run([str(state / "chronyc"), "tracking"], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["-h", str(target), "tracking"])

    def test_version_query_and_no_implicit_run(self):
        self.assertEqual(self.query("-v").stdout.strip(), "-v")
        self.assertEqual(self.query("--version").stdout.strip(), "-v")
        self.assertFalse((self.state / "capture.jsonl").exists())
        self.assertEqual(json.loads((self.state / "session.json").read_text())["status"], "PREPARED")
        self.assertEqual(self.state.stat().st_mode & 0o777, 0o700)

    def test_daemon_requires_foreground_non_clock_control_mode(self):
        result = subprocess.run([str(self.state / "chronyd"), "-U"], capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    def test_daemon_epochs_keep_logs_outside_runtime_and_parent_private(self):
        for _ in range(2):
            with tempfile.TemporaryDirectory(prefix="ptob-ch-", dir="/private/tmp") as runtime:
                config = Path(runtime) / "chrony.conf"
                config.write_text("port 0\ncmdport 0\n")
                with patch.object(observer.os, "execv", side_effect=RuntimeError("exec intercepted")):
                    with self.assertRaisesRegex(RuntimeError, "exec intercepted"):
                        observer.daemon(self.state, ["-x", "-U", "-d", "-f", str(config)])
                self.assertIn(f"logdir {self.state}/logs\n", config.read_text())
                self.assertEqual((self.state / "active-command.sock").readlink(), Path(runtime) / "command.sock")
                (self.state / "logs").chmod(0o755)  # chronyd can set its log directory's mode
                self.assertEqual(self.state.stat().st_mode & 0o777, 0o700)
        self.assertEqual(len((self.state / "daemon-epochs.jsonl").read_text().splitlines()), 2)

    def test_privileged_helper_query_drops_to_state_owner(self):
        with patch.object(observer.os, "geteuid", return_value=0), \
             patch.object(observer.os, "setgroups") as groups, \
             patch.object(observer.os, "setgid") as group, \
             patch.object(observer.os, "setuid") as user, \
             patch.object(observer.os, "execv", side_effect=RuntimeError("exec intercepted")):
            with self.assertRaisesRegex(RuntimeError, "exec intercepted"):
                observer.query(self.state, ["tracking"])
            groups.assert_called_once_with([])
            group.assert_called_once_with(observer.pwd.getpwuid(self.state.stat().st_uid).pw_gid)
            user.assert_called_once_with(self.state.stat().st_uid)

    def test_missing_samples_remain_in_denominator(self):
        rows = [{"type": "usb_status", "tick_ns": "1000000000", "chrony_user_utc_ns": None},
                {"type": "usb_status", "tick_ns": "2000000000", "chrony_user_utc_ns": "123"}]
        (self.state / "capture.jsonl").write_text("".join(json.dumps(r)+"\n" for r in rows))
        observer.summarise(self.state, {"status": "COMPLETE", "duration_s": 1})
        summary = json.loads((self.state / "summary.json").read_text())
        self.assertEqual(summary["counts"]["status_with_chrony_utc"], 1)
        self.assertEqual(summary["counts"]["status_without_chrony_utc"], 1)
        self.assertFalse(summary["accuracy_qualified"])


if __name__ == "__main__":
    unittest.main()
