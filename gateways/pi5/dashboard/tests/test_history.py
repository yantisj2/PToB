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
        held=h.sample(state,4)
        self.assertIsNone(held['source_offset_ns'])
        self.assertIsNone(held['tracking_rms_ns'])
        self.assertEqual(held['reported_tracking_rms_ns'],50)
        self.assertEqual(held['reported_source_offset_ns'],100)
        self.assertEqual(held['tracking_mode'],'holdover')
        self.assertEqual(held['timing_reason'],'PPS paused; no current edge')
        state['pps']['fresh']=True
        state['sources'][0]['reach']='000'
        self.assertIsNone(h.sample(state,4)['source_offset_ns'])
        state['sources']=[]
        failed=h.sample(state,4)
        self.assertIsNone(failed['tracking_offset_ns'])
        self.assertIsNone(failed['reported_source_offset_ns'])

    def test_availability_does_not_replace_the_clock_scope_or_live_population(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'history.sqlite3';db=h.History(path)
            context=['test-source','boot',['root','leaf'],'PTOB',True,'root','root-boot',1,'root']
            for i in range(5):
                context[3]='NTP' if i>=3 else 'PTOB'
                context[4]=i!=1
                context[5]='new-root' if i==4 else 'root'
                db.add(dict(at_ms=i*1000,mono_ms=i*1000,context=__import__('json').dumps(context),
                            selected=context[3],tracking_reference=context[3],complete=i!=1,gm_admitted=i!=1,
                            fit_drift_ppb=i,tracking_rms_ns=None if i==1 else 50,
                            reported_tracking_rms_ns=50,tracking_mode='holdover' if i==1 else 'live',
                            pps_phase_ns=None if i==1 else i,pps_sequence=i,receiver_boot='boot'))
            db.close();data=h.read(path,60);rows=data['points']
            self.assertEqual([r['series_segments']['receiver'] for r in rows],[0,0,0,0,1])
            self.assertEqual([r['series_segments']['tracking'] for r in rows],[0,0,0,1,1])
            self.assertEqual(data['timing_health']['source_changes'],1)
            self.assertEqual(data['timing_health']['pps_unavailable_snapshots'],1)
            self.assertEqual(data['statistics']['tracking_rms_ns']['n'],4)
            self.assertEqual(data['held_estimates']['tracking_rms_ns'],1)
            self.assertIn('withdrawn',rows[1]['timing_reason'])
            exported=h.read(path,60,export=True)
            self.assertIsNone(exported[1]['tracking_rms_ns'])
            self.assertEqual(exported[1]['reported_tracking_rms_ns'],50)

    def test_historical_unknowns_and_two_second_physical_intervals_are_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'history.sqlite3';db=h.History(path)
            for i in range(3):
                db.add(dict(at_ms=i*1000,mono_ms=i*1000,context='same',selected='PTOB',gm_admitted=i!=1,
                            complete=i!=1,pps_sequence=10 if i<2 else 11,
                            pps_phase_ns=None if i==1 else 1,tracking_rms_ns=None if i==1 else 50,
                            pps_interval_ns=2000000000 if i==2 else 1000000000,pps_gap=i==2))
            db.close();data=h.read(path,60)
            self.assertIsNone(data['points'][1]['tracking_rms_ns'])
            self.assertNotIn('reported_tracking_rms_ns',data['points'][1])
            self.assertEqual(data['timing_health']['pps_gap_events'],1)
            self.assertEqual(data['timing_health']['nominal_missing_pps_slots'],1)

    def test_unknown_telemetry_does_not_count_as_a_source_change(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'history.sqlite3';db=h.History(path)
            known=['firmware','boot',['root','leaf'],'PTOB',True,'root','gm-boot',1,'parent']
            unknown=[None,None,None,'PTOB',False,None,None,None,None]
            changed=known.copy();changed[5]='new-root'
            for i, context in enumerate((known,unknown,known,unknown,changed)):
                db.add(dict(at_ms=i*1000,mono_ms=i*1000,context=__import__('json').dumps(context),
                            selected='PTOB',complete=context is not unknown,
                            pps_phase_ns=1,pps_sequence=i,receiver_boot=context[1]))
            db.close();data=h.read(path,60)
            self.assertEqual(data['timing_health']['source_changes'],1)
            self.assertEqual(data['timing_health']['availability_changes'],4)
            self.assertEqual([r['series_segments']['receiver'] for r in data['points']],[0,1,2,3,4])

    def test_legacy_display_warmup_does_not_erase_valid_measured_history(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'history.sqlite3';db=h.History(path)
            for i in range(20):
                db.add(dict(at_ms=i*1000,mono_ms=i*1000,context='same',selected='PTOB',
                            complete=True,gm_admitted=True,pps_phase_ns=1,pps_sequence=i,
                            fit_rms_ns=55,tracking_rms_ns=50,source_stddev_ns=300,
                            report_policy='stable-path-10s-v1',report_ready=False,report_state='warming'))
            db.close();data=h.read(path,60)
            self.assertEqual(data['statistics']['tracking_rms_ns']['n'],20)
            self.assertTrue(all(r['tracking_rms_ns']==50 for r in data['points']))
            exported=h.read(path,60,export=True)
            self.assertTrue(all(r['report_ready'] is False for r in exported))

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
