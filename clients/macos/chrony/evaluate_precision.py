#!/usr/bin/env python3
"""Offline causal evaluation of retained USB bridge phase observations.

The CSV provides an observed phase at each observed host time. It cannot provide
independent UTC truth, or physical truth between observations. No device access.
"""
import argparse
from collections import deque
from dataclasses import dataclass
from decimal import Decimal
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


@dataclass(frozen=True)
class FrozenFit:
    origin: int
    mean_x: float
    mean_y: float
    slope: float

    def predict(self, at_ns: int) -> float:
        # Subtract integers before float conversion, including POSIX-sized epochs.
        return self.mean_y + self.slope * (float(at_ns - self.origin) - self.mean_x)


class CausalPhaseFit:
    """Fit only already-ingested observations; scopes never share training."""
    def __init__(self, window=1024, minimum=64):
        if not 2 <= minimum <= window:
            raise ValueError('require 2 <= minimum <= window')
        self.window, self.minimum = window, minimum
        self.samples = deque(maxlen=window)
        self.scope = None
        self.origin = None
        self.last_at = None

    def reset(self):
        self.samples.clear()
        self.scope = self.origin = self.last_at = None

    def freeze(self, scope):
        if scope != self.scope or len(self.samples) < self.minimum:
            return None
        mx = statistics.fmean(x for x, _ in self.samples)
        my = statistics.fmean(y for _, y in self.samples)
        xx = math.fsum((x - mx) ** 2 for x, _ in self.samples)
        if xx == 0:
            return None
        slope = math.fsum((x - mx) * (y - my) for x, y in self.samples) / xx
        return FrozenFit(self.origin, mx, my, slope)

    def predict(self, at_ns, scope):
        fit = self.freeze(scope)
        return None if fit is None else fit.predict(at_ns)

    def ingest(self, at_ns, phase_ns, scope):
        if not isinstance(at_ns, int) or not math.isfinite(phase_ns):
            raise ValueError('integer time and finite phase required')
        if scope != self.scope:
            self.reset()
            self.scope, self.origin = scope, at_ns
        if self.last_at is not None and at_ns <= self.last_at:
            raise ValueError('non-increasing observation time within segment')
        self.samples.append((float(at_ns - self.origin), float(phase_ns)))
        self.last_at = at_ns


def error_metrics(errors):
    if not errors:
        return {'count': 0}
    absolute = sorted(abs(x) for x in errors)
    return {
        'count': len(errors),
        'mean_ns': statistics.fmean(errors),
        'rmse_ns': math.sqrt(statistics.fmean(x * x for x in errors)),
        'sigma_ns': statistics.pstdev(errors),
        'p99_absolute_ns': absolute[math.ceil(.99 * len(absolute)) - 1],
        'p99_method': 'nearest rank, ceil(0.99*n)',
        'minimum_ns': min(errors), 'maximum_ns': max(errors),
        'maximum_absolute_ns': absolute[-1],
        'absolute_over_1000_ns': sum(x > 1000 for x in absolute),
    }


def evaluate_csv(path, window=1024, minimum=64):
    path = Path(path)
    fit = CausalPhaseFit(window, minimum)
    all_errors, segments = [], []
    current = None
    rows = source_valid = 0
    # Decimal parsing preserves integer origins before any float conversion.
    # The legacy CSV already contains fractional-ns floating-point exports;
    # rounding to the nearest ns adds <=0.5ns to the *time coordinate*, not phase.
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            elapsed = Decimal(row['hardware_phase_elapsed_ns'])
            phase = float(row['counter_phase_ns'])
            if not elapsed.is_finite() or not math.isfinite(phase):
                raise ValueError('non-finite CSV observation')
            label = row['hardware_segment']
            if current is None or current['hardware_segment'] != label:
                fit.reset()
                current = {'hardware_segment': label, 'contiguous_segment': len(segments) + 1,
                           'observations': 0, 'source_valid_observations': 0,
                           'first_elapsed_ns': str(elapsed), '_origin': elapsed,
                           '_errors': []}
                segments.append(current)
            at = int((elapsed - current['_origin']).to_integral_value())
            scope = current['contiguous_segment']
            prediction = fit.predict(at, scope)
            if prediction is not None:
                error = phase - prediction
                all_errors.append(error)
                current['_errors'].append(error)
            # Deliberately after prediction: no current-sample training leakage.
            fit.ingest(at, phase, scope)
            current['observations'] += 1
            current['last_elapsed_ns'] = str(elapsed)
            valid = row.get('source_valid', '').lower() == 'true'
            current['source_valid_observations'] += int(valid)
            rows += 1
            source_valid += int(valid)
    for segment in segments:
        segment['errors'] = error_metrics(segment.pop('_errors'))
        segment['warmup_observations'] = segment['observations'] - segment['errors']['count']
        segment['observed_span_s'] = float((Decimal(segment['last_elapsed_ns']) - segment.pop('_origin')) / Decimal(1_000_000_000))
    return {
        'schema': 1, 'input': str(path.resolve()),
        'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'window': window, 'minimum_prior_observations': minimum,
        'prediction': 'previous-window phase evaluated at current observed host time, before ingestion',
        'phase_error_sign': 'observed minus predicted',
        'observations': rows, 'source_valid_observations': source_valid,
        'errors': error_metrics(all_errors), 'segments': segments,
        'physical_UTC_qualified': False, 'between_update_physical_prediction_qualified': False,
        'limitations': [
            'CSV contains accepted hardware observations, not the rejected raw tuples or markers; rejection count is unavailable from this file.',
            'Intervals without accepted observations and source-unavailable rows are not successful source-clock output.',
            'Source-valid count is separate from hardware observation count; no ten-minute source coverage is inferred.',
            'Observed sample-time holdout is relative to Apple SOF/Mach evidence, not independent UTC truth.',
            'No physical truth between observed samples is fabricated; sparse samples cannot establish maximum intersample error.',
            'CSV time coordinates are rounded to integer ns after Decimal origin subtraction; legacy upstream floating-point rounding cannot be undone.',
            'No retrospective detrending, bias removal, or rejection of large prediction errors is applied.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--window', type=int, default=1024)
    parser.add_argument('--minimum', type=int, default=64)
    parser.add_argument('--output', type=Path, help='new output path; refuses overwrite')
    args = parser.parse_args()
    report = evaluate_csv(args.csv, args.window, args.minimum)
    text = json.dumps(report, indent=2) + '\n'
    if args.output:
        with args.output.open('x') as stream:
            stream.write(text)
    print(text, end='')


if __name__ == '__main__':
    main()
