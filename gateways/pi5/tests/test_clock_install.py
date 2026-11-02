"""Refuse competing clock ownership before stopping or overwriting services. MIT."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('timing_setup', Path(__file__).parents[1]/'tools/timing_setup.py')
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


class ClockInstallTests(unittest.TestCase):
    def test_external_clock_owner_refused_before_any_service_or_file_change(self):
        for name in ('phc2sys', 'ptp4l', 'chronyd', 'ts2phc', 'timemaster', 'ntpd', 'systemd-timesyn'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                proc = Path(temp)
                (proc/'comm').write_text(name+'\n')
                (proc/'cgroup').write_text('0::/user.slice/external.service\n')
                with patch.object(timing.os, 'geteuid', return_value=0), \
                     patch.object(timing.Path, 'glob', return_value=[proc]), \
                     patch.object(timing, 'run') as operation:
                    with self.assertRaisesRegex(ValueError, 'competing clock owner'):
                        timing.clock_install({})
                    operation.assert_not_called()


if __name__ == '__main__':
    unittest.main()
