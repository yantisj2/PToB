"""Scientific accounting regressions; no hardware. MIT, Jonathan Yantis, 2026."""
import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('pc_analyze',Path(__file__).resolve().parents[1]/'tools/pc_analyze.py')
analysis=importlib.util.module_from_spec(spec);spec.loader.exec_module(analysis)

class Accounting(unittest.TestCase):
    def test_bias_and_unknowns(self):
        s=analysis.statistics([3000,4000])
        self.assertEqual(s['mean_ns'],3500)
        self.assertEqual(s['std_ns'],500)
        self.assertEqual(s['rms_ns'],math.sqrt(12500000))
        self.assertIsNone(analysis.statistics([])['rms_ns'])

    def test_epochs_empty_hold_and_pre_window(self):
        rows=[dict(kind='window_boundary',host_monotonic=0,
                   payload=dict(boot='b',pc_after=9,shadow_after=9))]
        for epoch,error in [(1,3000),(2,4000)]:
            rows.append(dict(kind='shadow',segment='baseline',host_monotonic=epoch,
                payload=dict(boot='b',found=True,sequence=epoch+9,edge_tick=epoch*16000000,
                    map_valid=True,reference_seen=True,reference_fix=True,reference_pps=True,
                    reason=0,phase_ns=error,edge_epoch=1,root='r',root_boot='rb',root_epoch=epoch,
                    parent='p',parent_boot='pb',map_generation=1,association=0)))
        rows.append(dict(kind='pc',host_monotonic=3,
            payload=dict(boot='b',trial=1,mode=2,listen_ms=1000,hold_ms=2000,phase=4,
                requested_tick=100,hold_from_tick=160000000,hold_until_tick=192000000,
                rf_closed_tick=0,first_return_sequence=11,found=False,failures=0,
                root='r',root_boot='rb',root_epoch=1,parent='p',parent_boot='pb',capture_epoch=1)))
        with tempfile.TemporaryDirectory()as directory:
            path=Path(directory)/'input.jsonl'
            path.write_text(''.join(json.dumps(x)+'\n'for x in rows))
            result=analysis.analyze(path,Path(directory)/'out')
        baseline=[x for x in result['populations']if x['phase']=='baseline']
        held=[x for x in result['populations']if x['phase']=='hold']
        self.assertEqual(sorted(x['rms_ns']for x in baseline),[3000,4000])
        self.assertEqual(len(held),2)
        self.assertTrue(all(x['n']==0 and x['rms_ns']is None and x['unobserved_trial_windows']==1 for x in held))
        self.assertEqual(result['witness_cursor_gaps'],[])

    def test_old_canary_is_not_a_new_baseline_trial(self):
        row=dict(kind='pc',host_monotonic=0,payload=dict(boot='b',trial=1,mode=4,
            listen_ms=1000,hold_ms=2000,phase=4,requested_tick=100,hold_from_tick=1000,
            hold_until_tick=2000,now_tick=9000,rf_closed_tick=1200,first_return_sequence=10,
            found=False,failures=0))
        with tempfile.TemporaryDirectory()as directory:
            path=Path(directory)/'input.jsonl';path.write_text(json.dumps(row)+'\n')
            result=analysis.analyze(path,Path(directory)/'out')
        self.assertEqual(result['trials'],[])
        self.assertEqual(result['populations'],[])
        self.assertEqual(result['prior_trial_metadata_excluded'],[dict(boot='b',trial=1)])

if __name__=='__main__':unittest.main()
