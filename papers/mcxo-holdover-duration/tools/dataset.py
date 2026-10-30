"""Read the sealed, selected supplement without expanding it into the checkout.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

BASE = Path(__file__).resolve().parents[1]
COLLECTION = BASE.parents[1] / 'evidence/w12/2026-10-30-holdover'


class Dataset:
    def __init__(self, collection=COLLECTION):
        self.collection = Path(collection)
        self.manifest = json.loads((self.collection / 'manifest.json').read_text())
        name = self.manifest['archive']['path']
        assert PurePosixPath(name).name == name and name.endswith('.zip'), 'Unsafe archive path'
        archive = self.collection / name
        assert archive.resolve().parent == self.collection.resolve(), 'Archive outside collection'
        raw = archive.read_bytes()
        assert len(raw) == self.manifest['archive']['bytes'], 'Archive size mismatch'
        assert hashlib.sha256(raw).hexdigest() == self.manifest['archive']['sha256'], 'Archive hash mismatch'
        self.files = {row['path']: row for row in self.manifest['members']}
        assert len(self.files) == len(self.manifest['members']), 'Duplicate manifest member'
        self._data = {}
        with zipfile.ZipFile(archive) as bundle:
            names = bundle.namelist()
            assert len(names) == len(set(names)) and set(names) == set(self.files), 'Archive inventory mismatch'
            assert sum(i.file_size for i in bundle.infolist()) <= 32 * 1024 * 1024, 'Supplement size limit'
            for name in names:
                path = PurePosixPath(name)
                assert not path.is_absolute() and '..' not in path.parts, 'Unsafe archive member'
                row = self.files[name]
                assert bundle.getinfo(name).file_size == row['bytes'], ('Member size mismatch', name)
                data = bundle.read(name)
                assert hashlib.sha256(data).hexdigest() == row['sha256'], ('Member hash mismatch', name)
                self._data[name] = data

    def read_bytes(self, name):
        return self._data[name]

    def read_text(self, name):
        return self.read_bytes(name).decode('utf-8')

    def json(self, name):
        return json.loads(self.read_text(name))


_dataset = None


def dataset():
    global _dataset
    if _dataset is None:
        _dataset = Dataset()
    return _dataset
