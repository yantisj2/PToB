"""Synthetic PPS populations, not hardware acceptance. Jonathan Yantis. MIT."""
import ast
import importlib.util
import json
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('capture',Path(__file__).parents[1]/'tools/pps_capture.py')
capture=importlib.util.module_from_spec(spec);spec.loader.exec_module(capture)


def status(at,eligible=True,context='a'):
    return dict(boottime_s=at,eligible=eligible,context=context)


def edge(at,stamp,sequence,seed=False):
    return dict(boottime_s=at,assertion=f'{stamp}#{sequence}',seed=seed)


def record(edges,statuses=None):
    return dict(start_boottime_s=0,end_boottime_s=10,edges=edges,
                status=statuses or [status(i) for i in range(11)])


class PPSCaptureTests(unittest.TestCase):
    def test_epoch_integer_precision_and_strict_tail_thresholds(self):
        self.assertEqual(capture.timestamp('1793571054.000000063#12'),(1793571054000000063,12))
        s=capture.statistics([0,1000,-1001,2000,-2001,5000,-5001])
        self.assertEqual((s['n'],s['above_1us'],s['above_2us'],s['above_5us']),(7,5,3,1))
        self.assertEqual(s['p99_abs_ns'],5001)
        self.assertEqual(s['max_abs_ns'],5001)
        self.assertEqual(capture.statistics([])['max_abs_ns'],None)
        self.assertEqual(capture.statistics([])['above_1us'],None)

    def test_old_or_zero_seed_never_creates_a_new_pulse_or_gap(self):
        for stamp,seq in [('0.000000000',0),('1700000000.000000000',20)]:
            rows=[edge(0,stamp,seq,True),edge(.5,'1793571054.000000063',seq+1),edge(1.5,'1793571055.000000064',seq+2)]
            s=capture.analyse(record(rows))
            self.assertEqual(s['all_new_edge_phase']['n'],2)
            self.assertEqual(s['gaps'],[])
            self.assertEqual(s['observed_consecutive_period_error']['mean_ns'],1)
        # A delayed observer can miss assertions after reading an old seed.
        # Count their sequence delta without assigning ancient missing slots.
        s=capture.analyse(record([edge(0,'1700000000.000000000',20,True),
                                  edge(.5,'1793571054.000000063',23)]))
        self.assertEqual(s['unobserved_assertions'],2)
        self.assertIsNone(s['gaps'][0]['nominal_missing_slots'])

    def test_unobserved_assertions_and_missing_nominal_slots_are_distinct(self):
        rows=[edge(.5,'1793571054.000000000',10),edge(1.5,'1793571056.000000000',12),
              edge(2.5,'1793571183.000000000',13)]
        s=capture.analyse(record(rows))
        self.assertEqual(s['unobserved_assertions'],1)
        self.assertEqual(s['gaps'][0]['nominal_missing_slots'],0)
        self.assertEqual(s['gaps'][1]['nominal_missing_slots'],126)
        self.assertEqual(s['observed_consecutive_period_error']['n'],0)

    def test_boundaries_source_loss_and_context_changes_retain_full_tails(self):
        rows=[edge(.5,'1793571054.000000001',1),edge(1.5,'1793571055.000006000',2),
              edge(2.5,'1793571056.000000002',3),edge(3.5,'1793571057.000000003',4)]
        states=[status(1),status(2,False),status(3,True,'b'),status(4,True,'b')]
        s=capture.analyse(record(rows,states))
        self.assertEqual(s['all_new_edge_phase']['n'],4)
        self.assertEqual(s['all_new_edge_phase']['max_abs_ns'],6000)
        self.assertEqual(s['source_eligible_edge_phase']['n'],1)
        self.assertEqual(s['source_eligible_edge_phase']['max_abs_ns'],3)
        self.assertEqual(s['status_context_changes'],1)

    def test_partial_capture_duplicates_and_malformed_edges_remain_explicit(self):
        rows=[edge(.5,'1793571054.000007000',1),edge(.6,'1793571054.000007000',1),
              edge(1,'NaN#2',2)]
        raw=record(rows);raw['status']=[];raw['error']='test interruption'
        s=capture.analyse(raw)
        self.assertEqual(s['all_new_edge_phase']['n'],1)
        self.assertEqual(s['all_new_edge_phase']['above_5us'],1)
        self.assertEqual(s['source_eligible_edge_phase']['n'],0)
        self.assertEqual(s['duplicate_records_ignored'],1)
        self.assertEqual(len(s['invalid_edges']),1)
        self.assertEqual(s['capture_failure'],'test interruption')

    def test_remote_observer_source_compiles_without_hardware(self):
        compile(capture.REMOTE_SOURCE,'<pps-observer>','exec')

    def test_bad_source_metadata_cannot_reach_capture_serialization(self):
        # Execute only imports and pure helpers, never affinity or capture IO.
        tree=ast.parse(capture.REMOTE_SOURCE)
        pure=ast.Module(body=[n for n in tree.body if isinstance(n,(ast.Import,ast.FunctionDef))],type_ignores=[])
        scope={};exec(compile(pure,'<offline-metadata>','exec'),scope)
        decode=scope['observation']
        state=dict(observed_boottime_s=100.0,receiver=dict(fresh=True,data=dict(
            boot='1111111111111111',error=0,receiver_error=0,capture_error=0,uart_error=0,
            following=True,clock_valid=True,offer_valid=1,pps_emitted=1,rx=2,authenticated=2)),
            source_status=dict(root='2222222222222222',root_boot='3333333333333333',epoch=1,
                               parent='2222222222222222',context='4444444444444444',current=True),
            pps=dict(fresh=True),chain=dict(complete=True))
        self.assertTrue(decode(json.dumps(state),101)['eligible'])
        self.assertFalse(decode(json.dumps(state),99)['eligible'])
        self.assertFalse(decode(json.dumps(state),104)['eligible'])
        for value in [float('nan'),float('inf'),True,'1']:
            state['source_status']['epoch']=value
            with self.assertRaises(ValueError):decode(json.dumps(state),101)
        state['source_status']['epoch']=1
        state['receiver']['data']['rx']=float('nan')
        with self.assertRaises(ValueError):decode(json.dumps(state),101)
        state['receiver']['data']['rx']=2
        state['observed_boottime_s']=float('inf')
        with self.assertRaises(ValueError):decode(json.dumps(state),101)
        state['observed_boottime_s']=100.0
        with self.assertRaises(ValueError):decode(json.dumps(state).replace('100.0','1e999'),101)


if __name__=='__main__':unittest.main()
