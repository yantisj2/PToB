"""Guard readiness is independent from supervisor liveness. Jonathan Yantis. MIT."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import server


class GuardStatusTests(unittest.TestCase):
    def setUp(self):
        clock=patch.object(server.time,'CLOCK_BOOTTIME',7,create=True)
        clock.start()
        self.addCleanup(clock.stop)

    def test_running_transfer_without_ready_field_and_explicit_serving_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            (base/'transfer.json').write_text(json.dumps(dict(observed_boottime_s=99.5,running=True,tai_offset_s=37)))
            (base/'server.json').write_text(json.dumps(dict(observed_boottime_s=99.8,running=False,ready=False,reason='source unavailable')))
            with patch.object(server,'CLOCK_STATE',base),patch.object(server.time,'clock_gettime',return_value=100):
                data=server.clock_chain_status()
            self.assertTrue(data['transfer']['fresh'])
            self.assertTrue(data['transfer']['running'])
            self.assertIsNone(data['transfer']['ready'])
            self.assertTrue(data['server']['fresh'])
            self.assertFalse(data['server']['ready'])
            self.assertEqual(data['server']['reason'],'source unavailable')

    def test_cached_future_and_nonfinite_observations_never_become_current(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            for stamp in (97.9,101,True,None,float('nan'),10**400):
                (base/'server.json').write_text(json.dumps(dict(observed_boottime_s=stamp,running=True,ready=True)))
                with patch.object(server,'CLOCK_STATE',base),patch.object(server.time,'clock_gettime',return_value=100):
                    data=server.clock_chain_status()
                self.assertFalse(data['server']['fresh'])
                self.assertFalse(data['transfer']['fresh'])
                json.dumps(data,allow_nan=False)

    def test_missing_or_malformed_guard_does_not_hide_the_rest_of_status(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            for text in ('[1,2]','{incomplete','null'):
                (base/'server.json').write_text(text)
                with patch.object(server,'CLOCK_STATE',base):data=server.clock_chain_status()
                self.assertFalse(data['server']['fresh'])
                self.assertEqual(data['server']['reason'],'Guard observation unavailable')


if __name__=='__main__':unittest.main()
