#!/usr/bin/env python3
"""Offline destructive-operation regressions on disposable synthetic builds.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from unittest import mock

import firmware_artifacts as storage


class RetentionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.repo = Path(self.temp.name).resolve()
        subprocess.run(['git', 'init', '-q', str(self.repo)], check=True)
        (self.repo / 'artifacts/local').mkdir(parents=True)
        self.serial = 0

    def tearDown(self):
        self.temp.cleanup()

    def build(self, device='0000000000000001', profile='RC', status='BUILD_COMPLETE_PHYSICAL_UNKNOWN', version='1.0.0'):
        self.serial += 1
        root = self.repo / 'artifacts' / ('build-' + str(self.serial))
        for folder in ('target', 'source', 'local', 'pycache'):
            (root / folder).mkdir(parents=True, exist_ok=True)
        for name, contents in {'target/app.bin': b'image', 'target/app.elf': b'ELF',
                               'target/code.obj': b'object', 'target/stack.su': b'stack',
                               'target/generated.c': b'generated', 'target/header.h': b'header',
                               'source/main.c': b'source', 'pycache/cache.pyc': b'cache',
                               'target/CMakeError.log': b'compiler failure details',
                               'build.log': b'failure and success logs'}.items():
            (root / name).write_bytes(contents)
        storage.atomic_json(root / 'local/device.json', {'device_id': device})
        receipt = {'artifact_class': 'PTOB_W12_RC', 'version': version, 'profile': profile,
                   'status': status, 'source_dirty': status == 'DEVELOPMENT_NOT_FLASHABLE',
                   'sources': {'main.c': storage.digest(root / 'source/main.c')},
                   'artifacts': {n: storage.digest(root / n) for n in ('target/app.bin', 'target/app.elf', 'target/stack.su')},
                   'compiled_sources': {'generated/target/generated.c': storage.digest(root / 'target/generated.c')},
                   'application': 'target/app.bin', 'application_sha256': storage.digest(root / 'target/app.bin'),
                   'application_bytes': 5}
        storage.atomic_json(root / 'receipt.json', receipt)
        stamp = time.time() - 10000 + self.serial
        os.utime(root / 'receipt.json', (stamp, stamp))
        return root

    def apply(self, plan):
        with contextlib.redirect_stdout(io.StringIO()), storage.locked(self.repo):
            return storage.apply_plan(self.repo, plan)

    def pin(self, value):
        storage.atomic_json(self.repo / 'artifacts/local/device-current.json', value)

    def test_compaction_preserves_receipt_and_all_inspector_inputs(self):
        root = self.build()
        before = storage.verify_bundle(root)
        result = self.apply(storage.make_plan(self.repo, 'compact'))
        self.assertEqual(result['completed'], 1)
        self.assertFalse((root / 'target/code.obj').exists())
        self.assertFalse((root / 'pycache/cache.pyc').exists())
        self.assertFalse((root / 'pycache').exists())
        self.assertTrue((root / 'target/generated.c').exists())
        self.assertTrue((root / 'target/header.h').exists())
        self.assertEqual(storage.verify_bundle(root), before)
        self.assertEqual(storage.make_plan(self.repo)['actions'], [])

    def test_latest_previous_and_historical_logs(self):
        old, previous, latest = [self.build() for _ in range(3)]
        receipt_hash = storage.digest(old / 'receipt.json')
        self.apply(storage.make_plan(self.repo))
        self.assertFalse((old / 'target/app.bin').exists())
        self.assertEqual(storage.read(old / storage.RETIRED)['state'], 'RETIRED')
        for p in ('build.log', 'source/main.c', 'local/device.json', 'target/stack.su', 'target/CMakeError.log'):
            self.assertTrue((old / p).exists())
        self.assertEqual(storage.digest(old / 'receipt.json'), receipt_hash)
        for root in (previous, latest):
            storage.verify_bundle(root)
        with self.assertRaises(ValueError):
            storage.verify_bundle(old)

    def test_device_profile_and_development_separation(self):
        roots = [self.build(), self.build(device='0000000000000002'),
                 self.build(profile='GM'), self.build(status='DEVELOPMENT_NOT_FLASHABLE')]
        plan = storage.make_plan(self.repo)
        self.assertEqual(len(plan['latest']), 4)
        self.assertTrue(all(x['action'] == 'compact' for x in plan['actions']))

    def test_failed_build_does_not_displace_good_images(self):
        good = [self.build() for _ in range(2)]
        failed = self.build(status='FAILED')
        self.apply(storage.make_plan(self.repo))
        for p in good:
            storage.verify_bundle(p)
        self.assertTrue((failed / 'build.log').exists())
        self.assertFalse((failed / 'target/app.bin').exists())

    def test_current_and_transitive_recovery_pins(self):
        old, recovery, _, _ = [self.build() for _ in range(4)]
        manifest = self.repo / 'artifacts/install/manifest.json'
        storage.atomic_json(manifest, {'recovery': str(recovery / 'target/app.bin')})
        self.pin({'image': str(old / 'target/app.bin'), 'manifest': str(manifest)})
        plan = storage.make_plan(self.repo)
        self.assertEqual(set(plan['pins']), {str(old), str(recovery)})
        self.apply(plan)
        storage.verify_bundle(old)
        storage.verify_bundle(recovery)

    def test_symlink_reference_protects_real_build(self):
        old = self.build()
        self.build(); self.build()
        alias = self.repo / 'artifacts/alias'
        alias.symlink_to(old, target_is_directory=True)
        self.pin({'image': str(alias / 'target/app.bin')})
        plan = storage.make_plan(self.repo)
        self.assertIn(str(old), plan['pins'])
        self.assertIn('symlink:' + str(alias), plan['pin_inputs'])

    def test_new_pin_invalidates_plan_before_any_deletion(self):
        old = self.build(); self.build(); self.build()
        plan = storage.make_plan(self.repo)
        self.pin({'image': str(old / 'target/app.bin')})
        with self.assertRaisesRegex(ValueError, 'Stale plan'):
            self.apply(plan)
        self.assertTrue((old / 'target/code.obj').exists())

    def test_changed_file_invalidates_plan(self):
        root = self.build()
        plan = storage.make_plan(self.repo)
        (root / 'target/code.obj').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Stale plan'):
            self.apply(plan)
        self.assertTrue((root / 'pycache/cache.pyc').exists())

    def test_new_build_invalidates_plan(self):
        root = self.build()
        plan = storage.make_plan(self.repo)
        self.build()
        with self.assertRaisesRegex(ValueError, 'New builds'):
            self.apply(plan)
        self.assertTrue((root / 'target/code.obj').exists())

    def test_corrupt_replacement_does_not_delete_predecessors(self):
        old, _, latest = [self.build() for _ in range(3)]
        (latest / 'target/app.bin').write_bytes(b'corrupt')
        result = self.apply(storage.make_plan(self.repo))
        self.assertIn(str(latest), result['invalid_latest'])
        self.assertTrue((old / 'target/app.bin').exists())

    def test_symlink_in_build_and_tracked_files_are_excluded(self):
        a, b = self.build(), self.build()
        outside = self.repo / 'outside'
        outside.write_bytes(b'untouched')
        (a / 'target/link.obj').symlink_to(outside)
        subprocess.run(['git', '-C', str(self.repo), 'add', str(b / 'target/code.obj')], check=True)
        plan = storage.make_plan(self.repo)
        self.assertEqual(len(plan['actions']), 1)
        self.assertEqual(len(plan['skipped']), 1)
        self.apply(plan)
        self.assertTrue((a / 'target/link.obj').is_symlink())
        self.assertEqual(outside.read_bytes(), b'untouched')

    def test_receipt_path_traversal_rejected(self):
        root = self.build()
        r = storage.read(root / 'receipt.json')
        r['artifacts']['../outside'] = '0' * 64
        storage.atomic_json(root / 'receipt.json', r)
        plan = storage.make_plan(self.repo, min_age=0)
        self.assertEqual(plan['actions'], [])
        self.assertIn('Unsafe receipt path', plan['skipped'][0]['reason'])

    def test_lock_excludes_second_builder(self):
        with storage.locked(self.repo):
            with self.assertRaises(RuntimeError):
                with storage.locked(self.repo):
                    pass

    def test_recent_and_incomplete_are_not_deleted(self):
        root = self.build()
        os.utime(root / 'receipt.json', None)
        self.build(status='INCOMPLETE')
        plan = storage.make_plan(self.repo)
        self.assertEqual(plan['actions'], [])
        self.assertEqual(len(plan['skipped']), 2)

    def test_interrupted_retirement_can_resume(self):
        old = self.build(); self.build(); self.build()
        storage.atomic_json(old / storage.RETIRED, {'state': 'REMOVING'})
        (old / 'target/app.bin').unlink()
        self.apply(storage.make_plan(self.repo))
        self.assertEqual(storage.read(old / storage.RETIRED)['state'], 'RETIRED')

    def test_version_order_does_not_promote_a_copied_old_build(self):
        new = self.build(version='2.0.0')
        previous = self.build(version='1.9.0')
        old = self.build(version='1.0.0')
        plan = storage.make_plan(self.repo)
        self.assertEqual(next(iter(plan['latest'].values())), [str(new), str(previous)])
        self.assertEqual(next(x['action'] for x in plan['actions'] if x['path'] == str(old)), 'retire')

    def test_builder_failure_does_not_run_retention(self):
        def fail():
            raise RuntimeError('native build failure')
        with mock.patch.object(storage, 'make_plan') as plan, mock.patch('sys.argv', ['build.py', '--out', str(self.repo / 'artifacts/new')]):
            with self.assertRaisesRegex(RuntimeError, 'native build failure'):
                storage.run_builder(fail, self.repo)
            plan.assert_not_called()

    def test_source_checkout_reference_does_not_pin_all_builds(self):
        old = self.build(); self.build(); self.build()
        self.pin({'source': str(self.repo / 'artifacts')})
        self.assertEqual(storage.make_plan(self.repo)['pins'], {})

    def test_legacy_t096_identity_requires_bound_header(self):
        root = self.build()
        (root / 'local/device.json').unlink()
        p = root / 'target/ptob_device.h'
        p.write_text('#define PTOB_DEVICE_ID "0000000000000001"\n')
        r = storage.read(root / 'receipt.json')
        r['artifact_class'] = 'PTOB_T096_RC7'
        r['artifacts']['target/ptob_device.h'] = storage.digest(p)
        self.assertIn('0000000000000001', storage.grouping(root, r))
        p.write_text('#define PTOB_DEVICE_ID "0000000000000002"\n')
        with self.assertRaises(ValueError):
            storage.grouping(root, r)

    def test_successful_builder_automatically_compacts_and_rotates(self):
        old = self.build(); self.build()
        output = self.repo / 'artifacts/build-3'
        with mock.patch('sys.argv', ['build.py', '--out=' + str(output)]), contextlib.redirect_stdout(io.StringIO()):
            storage.run_builder(self.build, self.repo)
        self.assertFalse((old / 'target/app.bin').exists())
        self.assertFalse((output / 'target/code.obj').exists())
        storage.verify_bundle(output)

    def test_failed_builder_releases_scratch_without_rotating_good_images(self):
        good = self.build()
        output = self.repo / 'artifacts/build-2'
        def fail():
            self.build(status='FAILED')
            raise RuntimeError('compile failed')
        with mock.patch('sys.argv', ['build.py', '--out', str(output)]), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, 'compile failed'):
                storage.run_builder(fail, self.repo)
        storage.verify_bundle(good)
        self.assertTrue((output / 'build.log').exists())
        self.assertFalse((output / 'target/code.obj').exists())

    def test_nested_source_checkout_build_is_discovered(self):
        root = self.build()
        checkout = self.repo / 'artifacts/experiment/source'
        checkout.mkdir(parents=True)
        subprocess.run(['git', 'init', '-q', str(checkout)], check=True)
        (checkout / 'artifacts').mkdir()
        nested = checkout / 'artifacts/build'
        root.rename(nested)
        self.assertEqual(storage.discover(self.repo), [nested])
        subprocess.run(['git', '-C', str(checkout), 'add', 'artifacts/build/target/code.obj'], check=True)
        self.assertEqual(storage.make_plan(self.repo)['actions'], [])

    def test_low_disk_space_stops_before_native_build(self):
        usage = storage.shutil.disk_usage(self.repo)
        low = type(usage)(usage.total, usage.used, 1024)
        with mock.patch('sys.argv', ['build.py', '--out', str(self.repo / 'artifacts/new')]), \
                mock.patch.object(storage.shutil, 'disk_usage', return_value=low), \
                mock.patch.object(storage, 'cleanup_after_build') as cleanup:
            main = mock.Mock()
            with self.assertRaisesRegex(RuntimeError, 'Less than 2 GiB'):
                storage.run_builder(main, self.repo)
            main.assert_not_called()
            cleanup.assert_not_called()

    def test_backend_evidence_and_implementation_changes_do_not_create_groups(self):
        root = self.build()
        r = storage.read(root / 'receipt.json')
        r['clock_backend'] = {'mode': 'FINE', 'counter_hz': 80000000, 'calibration_evidence_sha256': 'first', 'fine_slew_threshold_ns': 1000}
        first = storage.grouping(root, r)
        r['clock_backend'].update(calibration_evidence_sha256='second', fine_slew_threshold_ns=2000)
        self.assertEqual(first, storage.grouping(root, r))
        r['clock_backend']['mode'] = 'LEGACY'
        self.assertNotEqual(first, storage.grouping(root, r))

    def test_older_branch_build_still_compacts_without_replacing_newer_images(self):
        self.build(version='3.0.0'); self.build(version='2.0.0')
        output = self.repo / 'artifacts/build-3'
        with mock.patch('sys.argv', ['build.py', '--out', str(output)]), contextlib.redirect_stdout(io.StringIO()):
            storage.run_builder(lambda: self.build(version='1.0.0'), self.repo)
        self.assertFalse((output / 'target/code.obj').exists())
        storage.verify_bundle(output)
        latest = storage.read(self.repo / 'artifacts/.firmware-retention/latest.json')['latest']
        self.assertNotIn(str(output), next(iter(latest.values())))

    def test_modified_device_input_cannot_change_retention_group(self):
        root = self.build()
        r = storage.read(root / 'receipt.json')
        r['device_config_sha256'] = storage.digest(root / 'local/device.json')
        storage.atomic_json(root / 'local/device.json', {'device_id': '0000000000000002'})
        with self.assertRaisesRegex(ValueError, 'Device input differs'):
            storage.grouping(root, r)

    def test_nested_checkout_shares_outer_lock_and_retention_owner(self):
        nested = self.repo / 'artifacts/experiment/source'
        nested.mkdir(parents=True)
        subprocess.run(['git', 'init', '-q', str(nested)], check=True)
        self.assertEqual(storage.storage_root(nested), self.repo)
        with storage.locked(self.repo):
            with self.assertRaises(RuntimeError):
                with storage.locked(nested):
                    pass

    def test_output_root_parent_traversal_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Parent traversal'):
            storage.safe_path(self.repo / 'artifacts', self.repo / 'artifacts/../victim')


if __name__ == '__main__':
    unittest.main()
