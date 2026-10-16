#!/usr/bin/env python3
"""Known-truth causal tests; no daemons, devices, wall clocks or sleeps."""
import csv
import io
from pathlib import Path
import tempfile
import unittest

from evaluate_precision import CausalPhaseFit, error_metrics, evaluate_csv


class PrecisionEvaluationTests(unittest.TestCase):
    def test_frozen_intermediate_and_future_predictions(self):
        origin = 1_790_000_000_000_000_000
        truth = lambda t: 37.0 + (t - origin) * 3e-6
        fit = CausalPhaseFit()
        errors = []
        for i in range(160):
            at = origin + i * 1_000_000_000
            frozen = fit.freeze('a')
            if frozen:
                # Evaluate actual specified truth BEFORE adding the next sample.
                for ahead in (250_000_000, 500_000_000, 1_000_000_000):
                    future = at - 1_000_000_000 + ahead
                    errors.append(truth(future) - frozen.predict(future))
            fit.ingest(at, truth(at), 'a')
        self.assertEqual(len(errors), 96 * 3)
        self.assertLess(error_metrics(errors)['maximum_absolute_ns'], 1e-6)

    def test_current_outlier_cannot_train_its_prediction(self):
        fit = CausalPhaseFit(window=8, minimum=4)
        for i in range(8):
            fit.ingest(i * 1000, 0, 'a')
        frozen = fit.freeze('a')
        before = frozen.predict(8000)
        error = 1_000_000 - before
        fit.ingest(8000, 1_000_000, 'a')
        self.assertEqual(error, 1_000_000)
        self.assertEqual(frozen.predict(8000), before)
        self.assertNotEqual(fit.predict(8000, 'a'), before)

    def test_scopes_and_explicit_reset_cannot_leak(self):
        fit = CausalPhaseFit(window=8, minimum=4)
        for i in range(8):
            fit.ingest(i, 100, 'old')
        self.assertIsNone(fit.predict(8, 'new'))
        for i in range(4):
            self.assertIsNone(fit.predict(i, 'new'))
            fit.ingest(i, 500, 'new')
        self.assertEqual(fit.predict(4, 'new'), 500)
        fit.reset()
        self.assertIsNone(fit.predict(5, 'new'))

    def test_constant_bias_is_not_hidden_by_sigma(self):
        metrics = error_metrics([50_000.] * 100)
        self.assertEqual(metrics['sigma_ns'], 0)
        self.assertEqual(metrics['rmse_ns'], 50_000)
        self.assertEqual(metrics['mean_ns'], 50_000)
        self.assertEqual(metrics['absolute_over_1000_ns'], 100)

    def test_integer_epoch_translation_preserves_nanoseconds(self):
        small, large = CausalPhaseFit(8, 4), CausalPhaseFit(8, 4)
        origin = 1_790_000_000_000_000_000
        for i in range(8):
            small.ingest(i, i * 2., 'a')
            large.ingest(origin + i, i * 2., 'a')
        self.assertEqual(small.predict(9, 'a'), large.predict(origin + 9, 'a'))
        self.assertEqual(large.predict(origin + 9, 'a'), 18)

    def test_step_and_rate_ramp_are_visible_before_ingestion(self):
        fit = CausalPhaseFit(16, 4)
        for i in range(16):
            fit.ingest(i * 1_000_000_000, 0, 'a')
        frozen = fit.freeze('a')
        # An unannounced phase step is an error, never a silently removed bias.
        self.assertEqual(2000 - frozen.predict(16_000_000_000), 2000)
        ramp_truth = lambda t: 1000 * ((t - 15_000_000_000) / 1e9) ** 2
        errors = [ramp_truth(t) - frozen.predict(t) for t in (15_500_000_000, 16_000_000_000)]
        self.assertEqual(errors, [250, 1000])

    def test_csv_segment_reentry_resets_and_retains_outlier(self):
        fields = ['hardware_segment', 'hardware_phase_elapsed_ns', 'counter_phase_ns', 'source_valid']
        buf = io.StringIO()
        writer = csv.writer(buf)
        writer.writerow(fields)
        for segment, bias in [('1', 0), ('2', 10000), ('1', 0)]:
            for i in range(6):
                writer.writerow([segment, str(1_790_000_000_000_000_000 + i), bias + (2000 if i == 4 else 0), 'false'])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'input.csv'
            path.write_text(buf.getvalue())
            report = evaluate_csv(path, window=8, minimum=4)
        self.assertEqual(report['observations'], 18)
        self.assertEqual(report['errors']['count'], 6)
        self.assertEqual(len(report['segments']), 3)
        self.assertTrue(all(s['warmup_observations'] == 4 for s in report['segments']))
        self.assertGreaterEqual(report['errors']['absolute_over_1000_ns'], 3)
        self.assertEqual(report['source_valid_observations'], 0)
        self.assertFalse(report['physical_UTC_qualified'])

    def test_nonincreasing_time_rejected(self):
        fit = CausalPhaseFit(8, 4)
        fit.ingest(4, 0, 'a')
        with self.assertRaises(ValueError):
            fit.ingest(4, 0, 'a')


if __name__ == '__main__':
    unittest.main()
