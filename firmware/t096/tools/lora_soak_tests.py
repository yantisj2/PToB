#!/usr/bin/env python3
"""Offline gate/storage checks. MIT, Jonathan Yantis, 2026. No hardware."""
import copy
import tempfile
import unittest
from pathlib import Path
from lora_soak import Gate, Budget, Monitor
import threading


def sample(s):
    return dict(diag=1,profile='PROD_60_300',boot='1',emitter='2',emitter_boot='3',stream=1,source_epoch=1,capture_epoch=7,
                at_us=int((s+10)*1e6),event_mono_us=int((s//60*60+10)*1e6),
                serial=int(s//300+1),cursor=int(s//60*2),accepted=int(s//60+1),
                event_ns=int((100000+s//60*60)*1e9),event_tick=int((s//60*60+10)*16e6),
                rejected=0,missing=0,late=0,bad=0,missing_map=0,storage_fail=0,hw_errors=[0]*5,
                requested=True,active=True,transmitter=False,fault=False,error=0,time_valid=True,
                physical_until_us=int((s//60*60+72)*1e6),meta_until_us=int((s//300*300+372)*1e6),
                trained=s>=180,link_fit=[min(6,int(s//60+1)),3000])


class Checks(unittest.TestCase):
    def feed(self,g,until=602):
        for s in range(0,until+1,2):g.observe(sample(s),int((s+1)*1e9))

    def test_two_real_cycles(self):
        g=Gate(5000);self.feed(g,598);self.assertFalse(g.qualified)
        g.observe(sample(600),601000000000);self.assertTrue(g.qualified)

    def test_serials_cannot_replace_elapsed_time(self):
        g=Gate(5000)
        for s in range(0,20,2):
            p=sample(s);p['serial']=s//2+1;p['trained']=True;g.observe(p,int((s+1)*1e9))
        self.assertFalse(g.qualified)

    def test_early_recovery_META_not_two_production_cycles(self):
        g=Gate(5000)
        for s in range(0,603,2):
            p=sample(s);p['serial']=1+int(s>=100)+int(s>=200)
            p['meta_until_us']=int((min(s//100,2)*100+372)*1e6)
            # Keep a synthetic lease live to isolate the real origin-span gate.
            if s>560:break
            g.observe(p,int((s+1)*1e9))
        self.assertFalse(g.qualified)

    def test_loss_of_training_and_serial_jump_remain_failures(self):
        for change in (dict(trained=False),dict(serial=5)):
            g=Gate(5000);self.feed(g);p=sample(604);p.update(change)
            self.assertIsNotNone(g.observe(p,605000000000));self.assertFalse(g.qualified)

    def test_gap_restarts(self):
        g=Gate(5000);self.feed(g);self.assertTrue(g.qualified)
        self.assertEqual(g.observe(sample(620),621000000000),'host capture gap');self.assertFalse(g.qualified)

    def test_each_error_or_reset_breaks_gate(self):
        for field in ('boot','emitter','emitter_boot','stream','source_epoch','capture_epoch','accepted','rejected','missing','late','bad','missing_map','storage_fail'):
            g=Gate(5000);self.feed(g);p=sample(604)
            p[field]=0 if field=='accepted' else 'new' if isinstance(p[field],str) else p[field]+1
            self.assertIsNotNone(g.observe(p,605000000000),field);self.assertFalse(g.qualified)

    def test_crc_and_expiry(self):
        for mutate in (lambda p:p['hw_errors'].__setitem__(0,1),lambda p:p.update(time_valid=False),lambda p:p.update(physical_until_us=1),lambda p:p.update(link_fit=[6,5001]),lambda p:p.update(event_ns=1,accepted=12)):
            g=Gate(5000);self.feed(g);p=sample(604);mutate(p);self.assertIsNotNone(g.observe(p,605000000000));self.assertFalse(g.qualified)

    def test_profile_rejected(self):
        p=sample(0);p['profile']='LAB_FAST_4_12'
        with self.assertRaises(ValueError):Gate(5000).observe(p,1)

    def test_immutable_original_deadlines(self):
        for field in ('physical_until_us','meta_until_us','event_ns'):
            g=Gate(5000);self.feed(g);p=sample(604);p[field]+=1
            self.assertIsNotNone(g.observe(p,605000000000));self.assertFalse(g.qualified)

    def test_stabilization_timeout_retained(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);b=Budget(out,10**7,0);stop=threading.Event();m=Monitor(out,['RX'],['RX'],5000,b,stop)
            m.gate_deadline=-1
            m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(0),host_mono_ns=1000000000))
            self.assertTrue(stop.is_set() and m.gate_timed_out);self.assertIsNone(m.armed_at)
            m.failure.close();m.csv.close()
            self.assertIn('initial stability gate timed out',(out/'failures.jsonl').read_text())

    def test_bounded_storage(self):
        with tempfile.TemporaryDirectory() as folder:
            b=Budget(Path(folder),10,0);b(10)
            with self.assertRaises(OSError):b(1)
            with self.assertRaises(OSError):Budget(Path(folder),100,2**63)(1)

    def test_persistent_failure_after_gate(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);b=Budget(out,10**7,0);m=Monitor(out,['RX'],['RX'],5000,b,threading.Event())
            for s in range(0,603,2):m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(s),host_mono_ns=int((s+1)*1e9)))
            self.assertIsNotNone(m.armed_at)
            m.event('RX',dict(kind='query_timeout',host_mono_ns=604000000000))
            self.assertEqual(m.violations,1);self.assertFalse(m.gates['RX'].qualified)
            m.failure.close();m.csv.close()
            self.assertIn('query_timeout',(out/'failures.jsonl').read_text())


if __name__=='__main__':unittest.main()
