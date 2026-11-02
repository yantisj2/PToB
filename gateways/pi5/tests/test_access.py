"""Offline identity and credential-boundary checks.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('pi5_remote', Path(__file__).parents[1] / 'tools/remote.py')
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)


class AccessTests(unittest.TestCase):
    def test_remote_guard_accepts_nul_terminated_identity_before_execution(self):
        expected = {'serial': '0123456789abcdef', 'image_id': 'ptob-pi5-abcd',
                    'source': 'print("operation-reached")'}
        records = {'/proc/device-tree/model': 'Raspberry Pi 5 Model B Rev 1.1\x00',
                   '/proc/device-tree/serial-number': expected['serial'] + '\x00',
                   '/opt/ptob/pi5/IMAGE_ID': expected['image_id'] + '\n'}
        for mismatch in (False, True):
            payload = dict(expected)
            if mismatch:
                payload['serial'] = 'fedcba9876543210'
            with patch.object(sys, 'stdin', io.StringIO(json.dumps(payload))), \
                 patch('pathlib.Path.read_text', lambda p: records[str(p)]), \
                 patch('platform.machine', return_value='aarch64'), \
                 patch('os.umask'), patch.object(sys, 'stdout', io.StringIO()) as output:
                if mismatch:
                    with self.assertRaisesRegex(SystemExit, 'factory serial mismatch'):
                        exec(remote.GUARD, {})
                    self.assertEqual(output.getvalue(), '')
                else:
                    exec(remote.GUARD, {})
                    self.assertEqual(output.getvalue(), 'operation-reached\n')

    def test_credential_paths_cannot_live_inside_repository(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp).resolve()
            key, known = folder / 'key', folder / 'known'
            for path in (key, known):
                path.write_text('synthetic fixture')
                path.chmod(0o600)
            profile = {'host': 'ptob@192.0.2.1', 'serial': '0123456789abcdef',
                       'image_id': 'ptob-pi5-abcd', 'host_key_alias': 'synthetic-device',
                       'identity_file': str(key), 'known_hosts': str(known)}
            remote.validate_profile(profile)
            with patch.object(remote, 'ROOT', folder):
                with self.assertRaisesRegex(ValueError, 'outside the repository'):
                    remote.validate_profile(profile)
            key.chmod(0o644)
            with self.assertRaisesRegex(ValueError, 'owner-only'):
                remote.validate_profile(profile)

    def test_shell_or_option_host_is_refused_before_ssh(self):
        for host in ('-oProxyCommand=bad', 'user@host;bad', 'user@$(bad)', 'user@host\nbad'):
            with self.assertRaisesRegex(ValueError, 'user@host'):
                remote.validate_profile({'host': host})


if __name__ == '__main__':
    unittest.main()
