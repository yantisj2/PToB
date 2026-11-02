"""Authority and observation-boundary checks. Copyright 2026 Jonathan Yantis. MIT."""
import importlib.util
from pathlib import Path
import unittest
import sys

sys.path.insert(0, str(Path(__file__).parents[1]))

spec = importlib.util.spec_from_file_location('collector', Path(__file__).parents[1] / 'collector.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class StateTests(unittest.TestCase):
    def test_source_reporting_never_treats_route_placeholder_as_gm(self):
        tracker=c.SourceTracker()
        tracker.update(dict(fresh=False),'2026-01-01T00:00:00Z')
        r=dict(fresh=True,data=dict(boot='test-boot',path=['0000000000000001'],
                                   following=False,clock_valid=False,offer_valid=0,
                                   acquisition=[4,2,0]+[0]*17))
        s=tracker.update(r,'2026-01-01T00:00:00Z')
        self.assertIsNone(s['root']);self.assertFalse(s['current'])
        self.assertEqual(s['parent'],'0000000000000002')
        self.assertEqual([e['event'] for e in s['changes']],['Receiver first observed'])
        r['data'].update(following=True,clock_valid=True,offer_valid=1,uptime_ms=2000,
                         clock_source=dict(valid=True,root='0000000000000003',root_boot='boot-1',
                                           epoch=1,last_valid_ms=2000))
        self.assertTrue(tracker.update(r,'2026-01-01T00:00:02Z')['current'])
        r['data']['clock_valid']=False;r['data']['uptime_ms']=5000
        r['data']['clock_source']['valid']=False
        s=tracker.update(r,'2026-01-01T00:00:05Z')
        self.assertFalse(s['current']);self.assertEqual(s['last_valid_age_s'],3)
        self.assertEqual(s['root'],'0000000000000003')
        self.assertEqual(s['changes'][-1]['event'],'Timing withdrawn')
        r['fresh']=False;r['data']['discovery']=dict(reports=[dict(fresh=True)])
        self.assertFalse(tracker.update(r,'2026-01-01T00:00:06Z')['reports'][0]['fresh'])

    def test_ntp_selected_is_not_ptob_transfer(self):
        rows = c.sources('^,*,192.0.2.1,2,6,377,8,0.000001,0.000001,0.001\n')
        state = c.chain_state({'fresh':False}, {'fresh':True}, rows)
        self.assertFalse(state['complete'])
        self.assertFalse(state['pps_disciplined'])

    def test_stale_receiver_blocks_complete_chain(self):
        rows = c.sources('#,*,PTOB,0,0,377,1,0.000001,0.000001,0.000001\n')
        r = dict(fresh=False, data=dict(following=True, clock_valid=True, offer_valid=1))
        self.assertFalse(c.chain_state(r, {'fresh':True}, rows)['complete'])
        r['fresh'] = True
        state = c.chain_state(r, {'fresh':True}, rows)
        self.assertTrue(state['complete'])
        self.assertFalse(state['sub_microsecond_qualified'])
        self.assertFalse(c.chain_state(r, {'fresh':False}, rows)['complete'])

    def test_reject_other_module_and_source(self):
        # Synthetic identity is deliberately not an actual factory binding.
        b = dict(device_id='test-module', particle_id='test-particle', allowed_sources=['test-source'])
        r = dict(target='mhat-b504e', device_id='wrong-module', particle_id='test-particle', source='test-source')
        with self.assertRaises(ValueError): c.receiver_view(r,b)
        r['device_id']='test-module'
        r['source']='wrong-source'
        with self.assertRaises(ValueError): c.receiver_view(r,b)
        r['source']='test-source'
        r.update(boot='0000000000000001', following=False, clock_valid=False,
                 rx=0, authenticated=0, rejected=0, pps_emitted=0, pps_rejected=0, pps_armed=0, offer_valid=0)
        self.assertNotIn('device_id',c.receiver_view(r,b))
        r['clock_valid']='false'
        with self.assertRaises(ValueError): c.receiver_view(r,b)


if __name__ == '__main__': unittest.main()
