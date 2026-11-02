"""Timing precision and population tests. Synthetic, not hardware evidence. MIT."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).parents[1]))
import history as h
from collector import sources

class HistoryTests(unittest.TestCase):
    @patch.object(h.time, 'CLOCK_BOOTTIME', 1, create=True)
    @patch.object(h.time, 'clock_gettime', lambda _: 100.0)
    def test_filtered_source_age_does_not_erase_observed_clock_estimates(self):
        state=dict(receiver=dict(fresh=True, age_s=0.1, data={}),
                   pps=dict(fresh=True, phase_ns=20),
                   sources=[dict(name='PTOB',kind='#',state='*',reach='377',poll=2,
                                 age_s=5,offset_ns=100,measured_offset_ns=110)],
                   source_stats=[dict(name='PTOB',stddev_ns=30,samples=12)],
                   tracking=dict(reference='PTOB',leap='Normal',tracking_offset_ns=40,
                                 tracking_rms_ns=50,system_offset_ns=60),
                   chain=dict(admitted=True,complete=True,selected=dict(name='PTOB')))
        result=h.sample(state,4)
        self.assertEqual(result['source_offset_ns'],100)
        self.assertEqual(result['tracking_rms_ns'],50)
        self.assertEqual(result['source_sample_age_s'],5)
        self.assertEqual(result['source_poll_s'],4)
        # Real edge loss and unreachable/failed source queries still produce gaps.
        state['pps']['fresh']=False
        self.assertIsNone(h.sample(state,4)['source_offset_ns'])
        self.assertIsNone(h.sample(state,4)['tracking_rms_ns'])
        state['pps']['fresh']=True
        state['sources'][0]['reach']='000'
        self.assertIsNone(h.sample(state,4)['source_offset_ns'])
        state['sources']=[]
        self.assertIsNone(h.sample(state,4)['tracking_offset_ns'])

    def test_epoch_nanoseconds_and_chrony_columns(self):
        self.assertEqual(h.ns('1793571054.000000063')-h.ns('1793571053.999999999'),64)
        self.assertEqual(h.ns('-0.000000063'),-63)
        row=sources('#,*,PTOB,0,0,377,0,0.000000123,-0.000000456,0.000000018')[0]
        self.assertEqual(row['offset_ns'],123)
        self.assertEqual(row['measured_offset_ns'],-456)
        for s in ('NaN','Infinity'):
            with self.assertRaises(ValueError):h.ns(s)

    def test_parsers_and_rms_not_stddev(self):
        t=h.tracking('50544F42,PTOB,1,1793571054.118538845,-0.000000264,0.000000245,0.000000167,-7.191,0.004,0.073,0.000000001,0.000001630,1.0,Normal')
        self.assertEqual(t['tracking_rms_ns'],167)
        self.assertEqual(h.source_stats('PTOB,25,15,24,0.004,0.066,0.000000001,0.000000636')[0]['stddev_ns'],636)
        s=h.statistics([100,100,None]);self.assertEqual(s['rms'],100);self.assertEqual(s['stddev'],0)
        self.assertEqual(s['n'],2)

    def test_full_population_extrema_nulls_context_and_export(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'history.sqlite3';db=h.History(path)
            for i in range(2000):
                db.add(dict(at_ms=i*1000,mono_ms=i*1000,context='a' if i<1000 else 'b',
                            source_offset_ns=99999 if i==1357 else 2,
                            fit_rms_ns=None if i==1851 else 50))
            db.close();data=h.read(path,21600)
            self.assertEqual(data['population'],2000)
            self.assertEqual(data['statistics']['source_offset_ns']['n'],2000)
            self.assertEqual(data['statistics']['source_offset_ns']['max'],99999)
            self.assertIn(99999,[r['source_offset_ns'] for r in data['points']])
            self.assertTrue(any(r['fit_rms_ns'] is None for r in data['points']))
            self.assertEqual(data['gaps'],1)
            self.assertEqual(len(h.read(path,21600,export=True)),2000)
            with self.assertRaises(ValueError):h.read(path,0)

    def test_averages_preserve_bias_and_do_not_count_repeats(self):
        rows=[dict(pps_sequence=i//2,pps_phase_ns=20,context='a',complete=True) for i in range(128)]
        result=h.averaging(rows)
        self.assertEqual(result['unique_edges'],64)
        last=result['blocks'][-1]
        self.assertEqual(last['n'],1)
        self.assertEqual(last['mean'],20)
        self.assertEqual(last['rms'],20)
        self.assertIsNone(result['lag1'])

    def test_average_blocks_cannot_cross_missing_edges(self):
        rows=[dict(pps_sequence=i if i<31 else i+1,pps_phase_ns=(-1)**i*20,context='a',complete=True) for i in range(64)]
        result=h.averaging(rows)
        self.assertEqual(result['segments'],2)
        self.assertEqual(result['blocks'][-1]['n'],0)
        self.assertEqual(result['lag1'],-1)

if __name__=='__main__':unittest.main()
