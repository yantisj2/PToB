#!/usr/bin/env python3
"""Synthetic protocol/analysis regressions, never hardware observations.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'tools/dual_radio_test.py'
spec = importlib.util.spec_from_file_location('dual_campaign', path)
campaign = importlib.util.module_from_spec(spec)
spec.loader.exec_module(campaign)


def record(i, stage='parallel_train'):
    row = dict(schema=1, contract=campaign.CONTRACT, run='SYNTHETIC', stage=stage,
               stage_elapsed_us=i * 1000000, boot='synthetic-boot', firmware='synthetic-image',
               pps=dict(number=i + 1, seen_us=(i + 1) * 1000000, capture_epoch=1,
                        reference_valid=True, capture_valid=True, aided=False))
    for name, hz, phase, correction in [('ble', 20000000, 100, 2900), ('lora', 16000000, 35000, 0)]:
        tick = (i + 1) * hz
        row[name] = dict(eligible=True, authenticated=True, strict=True, map_valid=True,
                         requested=True, owner_active=True, scope='synthetic-same-origin-' + name,
                         counter_domain=name + '-domain-epoch1', settled_ms=10000,
                         capture_epoch=7, sample_epoch=7, owner_generation=2, sample_generation=2,
                         edge_tick=tick, available_tick=tick - hz // 10,
                         until_tick=tick + hz, permission_tick=tick + hz,
                         mapping_enclosure_ns=100, phase_ns=phase,
                         current_correction_ns=correction,
                         observation_id=str(i if name == 'ble' else i // 30))
    row['clock'] = dict(valid=True, instance='synthetic-clock1', counter_domain='common-domain1',
                        mode='BLE_FINE', authoritative=True, fine=True,
                        utc_ns=1700000000000000000 + i * 1000000000,
                        counter_tick=(i + 1) * 20000000, phase_ns=100, rate_ppb=-21000)
    return row


class CampaignTest(unittest.TestCase):
    def test_empty_capture_is_not_a_pass(self):
        with self.assertRaisesRegex(ValueError, 'No PPS records'):
            campaign.analyze([])

    def test_installed_image_is_not_claimed_ready(self):
        p = campaign.plan(dict(version='0.1.10'), 2900, 0)
        self.assertEqual(p['active_measurement_seconds'], 5400)
        self.assertFalse(p['hardware_access'])
        self.assertFalse(p['capture_ready'])
        self.assertFalse(p['shared_clock_firmware_ready'])
        self.assertEqual(len(p['missing_firmware_contracts']), 2)

    def test_separate_corrections_and_no_double_application(self):
        result = campaign.analyze([record(i) for i in range(1200)])
        choices = {v['channel']: v for v in result['calibration_candidates']}
        self.assertEqual(choices['ble']['candidate_total_correction_ns'], 2800)
        self.assertEqual(choices['ble']['incremental_correction_ns'], -100)
        self.assertEqual(choices['lora']['candidate_total_correction_ns'], -35000)
        self.assertFalse(result['offsets_confirmed'])
        self.assertFalse(result['shared_clock_activation_authorized'])

    def test_fourteen_points_are_preliminary_only(self):
        result = campaign.analyze([record(i) for i in range(14)])
        self.assertFalse(result['calibration_candidates'])
        self.assertEqual(result['statistics'][0]['samples'], 14)

    def test_outlier_is_retained(self):
        values = [100] * 99 + [1000000]
        result = campaign.statistics(values)
        self.assertEqual(result['samples'], 100)
        self.assertEqual(result['maximum_abs_ns'], 1000000)
        self.assertEqual(result['p99_abs_ns'], 10099)
        self.assertEqual(result['mean_ns'], 10099)

    def test_ineligible_edges_still_count_as_observed(self):
        rows = [record(i) for i in range(3)]
        rows[1]['lora']['strict'] = False  # Human-display fallback is not timing authority.
        result = campaign.analyze(rows)
        lora = next(r for r in result['statistics'] if r['channel'] == 'lora')
        self.assertEqual(lora['samples'], 2)
        self.assertEqual(lora['observed_stage_rows'], 3)
        self.assertEqual(lora['context_rows'], 3)
        self.assertEqual(lora['ineligible_rows'], 1)
        self.assertEqual(lora['invalid_reasons']['STRICT'], 1)

    def test_later_or_expired_model_never_scores(self):
        for change in ('available_tick', 'permission_tick', 'until_tick'):
            row = record(0)
            row['ble'][change] = row['ble']['edge_tick']
            reasons = campaign.channel_reasons(row, 'ble')
            self.assertIn('RACING_FUTURE_OR_EXPIRED_MODEL', reasons)

    def test_scope_epochs_authentication_and_aid(self):
        for field in ('authenticated', 'map_valid', 'requested', 'owner_active'):
            row = record(0)
            row['ble'][field] = '0'
            self.assertIn(field.upper(), campaign.channel_reasons(row, 'ble'))
        row = record(0)
        row['lora']['sample_epoch'] = 8
        self.assertIn('CAPTURE_EPOCH_MISMATCH', campaign.channel_reasons(row, 'lora'))
        row['pps']['aided'] = True
        self.assertIn('GNSS_AID_NOT_EXCLUDED', campaign.channel_reasons(row, 'lora'))

    def test_lora_pair_count_is_not_pps_count(self):
        rows = [record(i) for i in range(1200)]
        for row in rows:
            row['lora']['observation_id'] = 'one-correlated-model'
        result = campaign.analyze(rows)
        self.assertEqual([v['channel'] for v in result['calibration_candidates']], ['ble'])

    def test_source_contexts_are_not_pooled(self):
        rows = [record(i) for i in range(1200)]
        for row in rows[600:]:
            row['ble']['scope'] = 'different-origin-or-path'
        result = campaign.analyze(rows)
        self.assertNotIn('ble', [v['channel'] for v in result['calibration_candidates']])

    def test_new_reference_capture_is_not_pooled(self):
        rows = [record(i) for i in range(1200)]
        for row in rows[600:]:
            row['pps']['capture_epoch'] = 2
        self.assertFalse(campaign.analyze(rows)['calibration_candidates'])

    def test_reference_loss_splits_populations_and_missing_is_retained(self):
        rows = [record(i) for i in range(1200)]
        rows[600]['pps']['reference_valid'] = False
        result = campaign.analyze(rows)
        self.assertFalse(result['calibration_candidates'])
        self.assertEqual(len({r['reference_segment'] for r in result['statistics']}), 3)
        rows = [record(i) for i in range(1200) if i != 600]
        result = campaign.analyze(rows)
        self.assertEqual(result['missing_PPS_records'], 1)
        self.assertEqual(result['records'], 1199)

    def test_fixed_windows_and_mixed_images(self):
        row = record(1200)
        with self.assertRaisesRegex(ValueError, 'fixed stage window'):
            campaign.analyze([row])
        rows = [record(0), record(1)]
        rows[1]['firmware'] = 'new-image'
        with self.assertRaisesRegex(ValueError, 'Mixed boot or image'):
            campaign.analyze(rows)

    def test_missing_and_duplicate_pps(self):
        self.assertEqual(campaign.analyze([record(0), record(2)])['missing_PPS_records'], 1)
        with self.assertRaisesRegex(ValueError, 'Duplicate or backwards'):
            campaign.analyze([record(0), record(0)])

    def test_valid_pps_requires_the_reported_one_hz_cadence(self):
        rows = [record(0), record(1)]
        rows[1]['pps']['seen_us'] = 1500000
        with self.assertRaisesRegex(ValueError, 'PPS cadence'):
            campaign.analyze(rows)

    def test_one_clock_across_handoffs_and_holdover(self):
        rows = [record(i, 'both_loss_holdover') for i in range(60)]
        for i, row in enumerate(rows):
            row['clock'].update(mode='HOLD', authoritative=False, fine=False, holdover_age_us=(i + 1) * 1000000)
            row['ble']['eligible'] = row['lora']['eligible'] = False
        result = campaign.analyze(rows)
        self.assertTrue(result['shared_clock_evidence']['single_instance_per_run'])
        self.assertEqual(result['clock_statistics'][0]['samples'], 60)
        self.assertEqual([v['target_seconds'] for v in result['holdover_observations']], [1, 10, 60])
        self.assertEqual(result['shared_clock_evidence']['hold_authority_claims'], 0)

    def test_bad_common_clock_continuity_is_visible(self):
        rows = [record(0), record(1)]
        rows[1]['clock'].update(instance='wrong-second-clock', counter_tick=1, utc_ns=1,
                                mode='LORA_COARSE', fine=True)
        result = campaign.analyze(rows)['shared_clock_evidence']
        self.assertFalse(result['single_instance_per_run'])
        self.assertEqual(result['utc_backsteps'], 1)
        self.assertEqual(result['counter_reversals'], 1)
        self.assertEqual(result['coarse_fine_claims'], 1)
        self.assertEqual(result['handoffs'][0]['rate_change_ppb'], 0)

    def test_lost_gnss_does_not_create_measured_clock_errors(self):
        rows = [record(0), record(1)]
        for row in rows:
            row['pps']['reference_valid'] = False
            del row['clock']['phase_ns']
        rows[1]['clock']['mode'] = 'LORA_COARSE'
        rows[1]['clock']['fine'] = False
        result = campaign.analyze(rows)
        self.assertFalse(result['clock_statistics'])
        self.assertIsNone(result['shared_clock_evidence']['handoffs'][0]['phase_change_ns'])


if __name__ == '__main__':
    unittest.main()
