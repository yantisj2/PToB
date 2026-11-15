"""Guard readiness is independent from supervisor liveness. Jonathan Yantis. MIT."""
import json
import gzip
import io
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

    def test_current_status_never_accepts_future_invalid_or_old_timestamps(self):
        with patch.object(server.time,'clock_gettime',return_value=100):
            for stamp in (96.9,100.1,True,None,float('nan'),10**400):
                state=server.status_freshness(dict(observed_boottime_s=stamp,chain=dict(complete=True)))
                self.assertTrue(state['stale'])
                self.assertFalse(state['chain']['complete'])
            self.assertFalse(server.status_freshness(dict(observed_boottime_s=99.5,chain={}))['stale'])

    def test_cached_history_age_advances_on_each_response(self):
        server.HISTORY_CACHE.clear()
        try:
            with patch.object(server,'read_history',return_value=dict(newest_mono_ms=99000)) as read, \
                 patch.object(server.time,'monotonic',return_value=1), \
                 patch.object(server.time,'clock_gettime',side_effect=[100,103]):
                self.assertEqual(server.history(300)['sample_age_s'],1)
                self.assertEqual(server.history(300)['sample_age_s'],4)
                read.assert_called_once()
        finally:server.HISTORY_CACHE.clear()

    def test_gzip_negotiation_honors_quality_and_explicit_refusal(self):
        for header in ('gzip','br, gzip','GZip; q=0.5','*;q=1'):
            self.assertTrue(server.accepts_gzip(header),header)
        for header in ('','br','gzip;q=0','gzip;q=0, *;q=1','gzip;q=invalid','gzip;q=nan'):
            self.assertFalse(server.accepts_gzip(header),header)

    def test_http_compression_preserves_observations_and_content_length(self):
        class Response(server.Handler):
            def __init__(self,encoding):
                self.path='/api/status';self.headers={'Accept-Encoding':encoding}
                self.wfile=io.BytesIO();self.sent={}
            def send_response(self,code):self.code=code
            def send_header(self,k,v):self.sent[k]=v
            def end_headers(self):pass
        with tempfile.TemporaryDirectory() as directory:
            state=Path(directory)/'state.json'
            state.write_text(json.dumps(dict(observed_boottime_s=100,chain=dict(complete=True),synthetic='abcdef'*1000)))
            with patch.object(server,'STATE',state),patch.object(server,'clock_chain_status',return_value={}), \
                 patch.object(server.time,'clock_gettime',return_value=100):
                compressed=Response('gzip');compressed.do_GET()
                plain=Response('gzip;q=0');plain.do_GET()
            self.assertEqual(compressed.code,200)
            self.assertEqual(compressed.sent['Content-Encoding'],'gzip')
            self.assertEqual(compressed.sent['Vary'],'Accept-Encoding')
            self.assertEqual(int(compressed.sent['Content-Length']),len(compressed.wfile.getvalue()))
            self.assertNotIn('Content-Encoding',plain.sent)
            self.assertEqual(gzip.decompress(compressed.wfile.getvalue()),plain.wfile.getvalue())
            self.assertLess(len(compressed.wfile.getvalue()),len(plain.wfile.getvalue())//5)


if __name__=='__main__':unittest.main()
