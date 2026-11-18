#!/usr/bin/env python3
"""Offline strict PPS envelope, decoder and calibration gate regressions.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import dual_radio_decode as decoder
import dual_profile

BOOT = '0000000000000001'
NONCE = '0000000000000002'
MANIFEST = dict(r2_profile='r2-g1', factory_mac='02:00:00:00:00:01', source_commit='synthetic-source',
                build_id='synthetic-build', console_frame_bytes=8704,
                fault_watchdog='SUSTAINED_FAULT_120S_V1', dual_radio_test_contracts=['TWATCH_DUAL_RADIO_PPS_V1'])


def response(payload=None, cursor=1, radio_row=None):
    payload = payload or dict(schema=1, contract='TWATCH_DUAL_RADIO_PPS_V1', pps=dict(number=1, seen_us=2000000))
    raw = ('\nW12_V12_BEGIN version=3 nonce=' + NONCE + ' ticket=0000000000000003 factory=02:00:00:00:00:01 session=' + BOOT +
           ' source=synthetic-source build=synthetic-build request_us=1000000 deadline_us=2500000 ticket_deadline_us=3000000\n' +
           'W12_R2_STATUS profile=r2-g1\nW12_V12_STATUS factory=02:00:00:00:00:01 source=synthetic-source build=synthetic-build session=' + BOOT + '\n' +
           (radio_row if radio_row else 'TWATCH_DUAL_PPS version=1 contract=TWATCH_DUAL_RADIO_PPS_V1 clock_contract=TWATCH_COMMON_CLOCK_V1 enabled=0 available=1 latest=1 overwritten=0 cursor=' + str(cursor) + ' json=' + json.dumps(payload, separators=(',', ':'))) + '\n' +
           'W12_V12_COMMAND nonce=' + NONCE + ' action=PPS_READ accepted=1\n').encode()
    end = ('W12_V12_END version=3 nonce=' + NONCE + ' ticket=0000000000000003 rows=4 crc32=' + f'{zlib.crc32(raw):08x}' +
           ' request_us=1000000 deadline_us=2500000 completed_us=1500000 body_driver_drained=1 counter_exhausted=0 capture_id=0000000000000000 capture_active=0 capture_deadline_us=0')
    for name, base in decoder.console.base.COUNTERS.items():
        end += f' {name}=0 {base}=0 req_{name}=0'
    end = end.encode()
    return raw + end + f' end_crc32={zlib.crc32(end, zlib.crc32(raw)):08x}\n'.encode()


def entry():
    return dict(request_nonce=NONCE, response_hex=response().hex(), after_cursor=0,
                stage_start_device_us=1000000, run='synthetic', stage='parallel_train')


class WireTest(unittest.TestCase):
    def test_complete_crc_and_bound_identity(self):
        rows, receipt = decoder.decode([entry()], MANIFEST, BOOT)
        self.assertEqual(rows[0]['stage_elapsed_us'], 1000000)
        self.assertFalse(receipt['hardware_access'])

    def test_bad_crc_truncation_wrong_boot_source_nonce(self):
        for mutate in (lambda r: r[:-2], lambda r: r.replace(b'latest=1', b'latest=2'),
                       lambda r: r.replace(b'synthetic-build', b'other-build')):
            e = entry();e['response_hex'] = mutate(response()).hex()
            with self.assertRaises(RuntimeError):decoder.decode([e], MANIFEST, BOOT)
        with self.assertRaises(RuntimeError):decoder.decode([entry()], MANIFEST, '0000000000000009')
        e = entry();e['request_nonce'] = '0000000000000009'
        with self.assertRaises(RuntimeError):decoder.decode([e], MANIFEST, BOOT)

    def test_replays_bad_stage_and_old_capability(self):
        e = entry()
        with self.assertRaises(RuntimeError):decoder.decode([e, e], MANIFEST, BOOT)
        e['after_cursor'] = 1
        with self.assertRaises(RuntimeError):decoder.decode([e], MANIFEST, BOOT)
        e = entry();e['stage_start_device_us'] = 3000000
        with self.assertRaises(RuntimeError):decoder.decode([e], MANIFEST, BOOT)
        with self.assertRaises(RuntimeError):decoder.decode([entry()], dict(MANIFEST, dual_radio_test_contracts=[]), BOOT)

    def test_profile_disabled_and_confirmed_activation_binding(self):
        device = dict(device_id='0000020000000001')
        fine = dict(mode='TWATCH_ULTRA_RX17_RELATIVE_FINE', phase80=100)
        value = dual_profile.profile(device, fine)
        self.assertFalse(value['enabled'])
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'profile.json'
            def load(v):
                path.write_text(json.dumps(v));return dual_profile.profile(device, fine, path)
            active = dict(value, enabled=True)
            with self.assertRaises(ValueError):load(active)
            active.update(utc_alignment_confirmed=True, ble_root='0000000000000010',
                          lora_emitter='0000000000000020', lora_root='0000000000000021',
                          ble_validation_sha256='a' * 64, lora_validation_sha256='b' * 64, review_sha256='c' * 64)
            self.assertTrue(load(active)['enabled'])
            for field, replacement in [('device', 'bad'), ('fine_profile_sha256', 'd' * 64),
                                       ('lora_validation_sha256', None), ('ble_existing_ns', 1000001),
                                       ('ble_depth', 1)]:
                bad = dict(active);bad[field] = replacement
                with self.assertRaises(ValueError):load(bad)
            legacy=dict(active);legacy.pop('study_fixed_priors')
            self.assertFalse(load(legacy)['study_fixed_priors'])
            self.assertTrue(load(dict(active,study_fixed_priors=True))['study_fixed_priors'])
            for bad in (1,'true',None):
                with self.assertRaises(ValueError):load(dict(active,study_fixed_priors=bad))
            with self.assertRaises(ValueError):load(dict(value,study_fixed_priors=True))
            header = dual_profile.header(active)
            self.assertIn('UINT64_C(0x0000000000000020)', header)
            # Synthetic millisecond event bias, plus the modulo-PPS boundary.
            # This is profile arithmetic, not measured RF accuracy evidence.
            for correction in (-3600000, 3600000, -499999999, 499999999):
                wide = dict(active, lora_correction_ns=correction)
                self.assertEqual(load(wide)['lora_correction_ns'], correction)
            for correction in (-500000000, 500000000, True, 3600000.0):
                with self.assertRaises(ValueError):
                    load(dict(active, lora_correction_ns=correction))


if __name__ == '__main__':
    unittest.main()
