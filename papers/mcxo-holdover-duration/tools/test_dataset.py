"""Archive integrity regressions. No hardware, network or private inputs.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from dataset import Dataset


class ArchiveIntegrity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def make(self, names=('evidence/observations.jsonl',)):
        archive = self.root / 'observations.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            for name in names:
                z.writestr(name, b'{"value":1}\n')
        raw = archive.read_bytes()
        manifest = {'archive': {'path': archive.name, 'bytes': len(raw),
                               'sha256': hashlib.sha256(raw).hexdigest()},
                    'members': [{'path': name, 'bytes': 12,
                                 'sha256': hashlib.sha256(b'{"value":1}\n').hexdigest()}
                                for name in names]}
        (self.root / 'manifest.json').write_text(json.dumps(manifest))
        return archive, manifest

    def test_valid_archive_reads_without_extracting(self):
        self.make()
        self.assertEqual(Dataset(self.root).json('evidence/observations.jsonl'), {'value': 1})
        self.assertFalse((self.root / 'evidence').exists())

    def test_tampered_archive_is_refused(self):
        archive, _ = self.make()
        archive.write_bytes(archive.read_bytes()[:-1] + b'x')
        with self.assertRaisesRegex(AssertionError, 'Archive hash'):
            Dataset(self.root)

    def test_member_identity_is_checked(self):
        _, manifest = self.make()
        manifest['members'][0]['sha256'] = '0' * 64
        (self.root / 'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(AssertionError, 'Member hash'):
            Dataset(self.root)

    def test_unsafe_member_is_refused(self):
        self.make(('../escape',))
        with self.assertRaisesRegex(AssertionError, 'Unsafe archive member'):
            Dataset(self.root)


if __name__ == '__main__':
    unittest.main()
