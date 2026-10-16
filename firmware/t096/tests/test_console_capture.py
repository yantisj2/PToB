"""MIT. Copyright (c) 2026 Jonathan Yantis. Offline capture regressions."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location('capture', Path(__file__).resolve().parents[1]/'tools/console_capture.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def frame(sequence=1, payload=None):
    payload = json.dumps({'boot': '0000000000000001'} if payload is None else payload)
    return [f'R2 00000001 {sequence:08x} {len(payload)} {zlib.crc32(payload.encode()):08x}',
            payload, f'R2 END 00000001 {sequence:08x}']


class CaptureTest(unittest.TestCase):
    def test_crc_sequence_and_truncation(self):
        f = m.Frames()
        for line in frame():
            result = f.feed(line)
        self.assertEqual(result[0], 1)
        f.feed(frame()[0]);f.feed(frame()[1])
        with self.assertRaisesRegex(ValueError, 'regressed'):
            f.feed(frame()[2])
        f = m.Frames();f.feed(frame()[0]);f.feed('{}')
        with self.assertRaisesRegex(ValueError, 'CRC'):
            f.feed(frame()[2])
        f = m.Frames();f.feed(frame()[0])
        with self.assertRaisesRegex(ValueError, 'truncated'):
            f.feed(frame(2)[0])
        self.assertIsNone(f.feed(frame(2)[1]))
        self.assertEqual(f.feed(frame(2)[2])[0], 1)

    def test_noise_and_limits(self):
        f = m.Frames()
        self.assertIsNone(f.feed('#R2_TIMING {}'))
        with self.assertRaises(ValueError):
            f.feed('R2 00000001 00000001 9999 00000000')
        with self.assertRaises(ValueError):
            f.feed('x'*3073)
        for line in frame(payload=[]):
            if line.startswith('R2 END'):
                with self.assertRaises(ValueError):f.feed(line)
            else:f.feed(line)

    def test_rotation_retains_all_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory);writer = m.Rotating(p, 'raw', 8)
            for data in [b'12345', b'67890', b'abc']:
                writer.write(data)
            writer.close()
            self.assertEqual(b''.join(x.read_bytes() for x in sorted(p.iterdir())), b'1234567890abc')
            self.assertEqual(len(list(p.iterdir())), 2)

    def test_explicit_identities(self):
        c = dict(silicon_id='1'*16, source='2'*64, commit='3'*40, usb_serial='synthetic')
        m.validate_config({'CLOCK': c})
        for bad in [{'CLOCK': {**c, 'source': 'not-source'}}, {'../CLOCK': c}, {'A': c, 'B': c}]:
            with self.assertRaises(ValueError):m.validate_config(bad)


if __name__ == '__main__':
    unittest.main()
