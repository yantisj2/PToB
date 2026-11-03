#!/usr/bin/env python3
"""Offline gate/storage checks. MIT, Jonathan Yantis, 2026. No hardware."""
import copy
import csv
import tempfile
import unittest
from unittest.mock import patch
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
    def test_enhanced_schedule_keeps_600s_gate_and_rejects_wrong_control_period(self):
        g=Gate(10000,5)
        for s in range(901):
            p=sample(s);event=s//30*30
            p.update(profile='RAM_30_120_300',beat_us=30000000,meta_us=300000000,discovery_us=120000000,
                     hold_us=120000000,lease_us=420000000,cadence_bound_us=32510000,
                     event_mono_us=(event+10)*1000000,event_tick=(event+10)*16000000,
                     event_ns=(100000+event)*1000000000,cursor=s//30*2,accepted=s//30+1,
                     physical_until_us=(event+130)*1000000,meta_until_us=(s//300*300+430)*1000000)
            g.observe(p,(s+1)*1000000000)
        self.assertTrue(g.qualified)
        p['discovery_us']=60000000
        with self.assertRaises(ValueError):g.observe(p,902000000000)

    def test_wide_profile_is_separate_cadence_context(self):
        g=Gate(10000,5);p=sample(0)
        p.update(profile='RAM_30_120_300_BW500',beat_us=30000000,meta_us=300000000,discovery_us=120000000,
                 hold_us=120000000,lease_us=420000000,cadence_bound_us=32510000)
        g.observe(p,1000000000)
        p=copy.deepcopy(p)
        p.update(profile='RAM_30_120_300',at_us=p['at_us']+1000000)
        g.observe(p,2000000000)
        self.assertEqual(g.reason,'source or capture context changed')

    def test_learned_TZ_survives_time_loss_and_accepts_changed_zone(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                def status(zone,flags,time_valid=False,boot='one'):
                    return dict(boot=boot,tz=dict(id=zone[0],offset_min=zone[1],display_flags=flags),display_time=[time_valid,False,0])
                self.assertIsNone(m.observe_time_zone('RX',status((0,-32768),0)))
                self.assertIsNone(m.observe_time_zone('RX',status((2,-300),73)))
                self.assertIsNone(m.observe_time_zone('RX',status((2,-300),77)))
                self.assertIsNone(m.observe_time_zone('RX',status((2,-360),77,True)))
                self.assertIsNone(m.observe_time_zone('RX',status((1,0),73,True)))
                s=m.summary()['time_zone_durability']['RX']
                self.assertEqual((s['known_samples'],s['cached_samples'],s['zone_changes'],s['forgotten_samples']),(4,2,2,0))
                self.assertIsNotNone(m.observe_time_zone('RX',status((0,-32768),0)))
                self.assertEqual(s['forgotten_samples'],1)
                self.assertIsNone(m.observe_time_zone('RX',status((0,-32768),0,boot='two')))
                self.assertEqual((s['boot_changes'],s['learned']),(1,False))
            finally:m.failure.close();m.csv.close()

    def test_TZ_forgetting_invalidates_gate_and_records_post_gate_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX','TX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                m.gates['RX'].qualified=True;m.armed_at=1
                for at,zone in [(2,(2,-360)),(3,(0,-32768))]:
                    m.event('TX',dict(kind='reply',op='status',host_mono_ns=at,
                                     payload=dict(boot='one',tz=dict(id=zone[0],offset_min=zone[1],display_flags=73))))
                self.assertFalse(m.gates['RX'].qualified);self.assertEqual(m.violations,1)
                self.assertIn('learned display TZ forgotten',(out/'failures.jsonl').read_text())
            finally:m.failure.close();m.csv.close()

    def test_new_TZ_diagnostics_must_learn_before_initial_gate(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                # Supply a current timing witness so expiry is independent of TZ.
                m.gates['RX'].previous=(sample(0),1e-9,(),())
                m.gates['RX'].qualified=True
                def event(zone,at):
                    m.event('RX',dict(kind='reply',op='status',host_mono_ns=at,
                                     payload=dict(boot='one',tz=dict(id=zone[0],offset_min=zone[1],display_flags=0))))
                event((0,-32768),1);self.assertIsNone(m.armed_at)
                event((2,-360),2);self.assertEqual(m.armed_at,2)
            finally:m.failure.close();m.csv.close()

    def test_legacy_TZ_is_unsupported_and_malformed_new_diagnostics_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                self.assertIsNone(m.observe_time_zone('RX',dict(tz=dict(id=2,offset_min=-360))))
                self.assertIsNotNone(m.observe_time_zone('RX',dict(tz=dict(id=65536,offset_min=0,display_flags=73))))
                self.assertIsNone(m.observe_time_zone('RX',dict(boot='one',tz=dict(id=2,offset_min=-360,display_flags=73))))
                self.assertIsNotNone(m.observe_time_zone('RX',dict(tz=dict(id=2,offset_min=-360))))
                self.assertEqual(m.time_zones['RX']['unsupported_samples'],2)
            finally:m.failure.close();m.csv.close()

    def feed(self,g,until=602):
        for s in range(0,until+1,2):g.observe(sample(s),int((s+1)*1e9))

    def test_display_durability_is_separate_from_precision_gate(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            def event(s,**change):
                p=sample(s);p.update(display_available=True,display_held=False,display_utc_us=100000000+s*1000000,
                                    display_sampled_us=int((s+10)*1e6),display_ref_age_ms=s*1000)
                p.update(change);m.event('RX',dict(kind='reply',op='lora-diag',payload=p,host_mono_ns=int((s+1)*1e9)))
            try:
                event(0)
                event(2,stream=2,trained=False,time_valid=False,display_held=True)
                event(4,stream=2,trained=False,time_valid=False,display_held=True)
                event(6,display_available=False,display_held=False,display_utc_us=0,display_ref_age_ms=4294967295)
                stats=m.summary()['display_durability']['RX']
                self.assertEqual((stats['supported_samples'],stats['visible_samples'],stats['fresh_samples'],stats['held_samples'],stats['unavailable_samples']),(4,3,1,2,1))
                self.assertEqual((stats['advancing_intervals'],stats['held_context_changes'],stats['max_hold_reference_age_ms']),(2,1,4000))
                self.assertFalse(m.gates['RX'].qualified);self.assertIsNone(m.armed_at)
                m.csv.flush()
                with (out/'samples.csv').open() as stream:rows=list(csv.DictReader(stream))
                self.assertNotIn(None,rows[-1]);self.assertEqual(rows[2]['display_held'],'1');self.assertEqual(rows[2]['display_ref_age_ms'],'4000')
                self.assertEqual(rows[3]['display_utc_us'],'0')
            finally:m.failure.close();m.csv.close()

    def test_acquisition_samples_never_count_as_fresh_or_trained_HOLD(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                for s in range(0,604,2):
                    p=sample(s);p.update(trained=False,time_valid=False,display_available=True,display_held=False,display_acquiring=True,
                                        display_utc_us=100000000+s*1000000,display_sampled_us=int((s+10)*1e6),display_ref_age_ms=s*1000,stream=1+int(s>=300))
                    m.event('RX',dict(kind='reply',op='lora-diag',payload=p,host_mono_ns=int((s+1)*1e9)))
                stats=m.summary()['display_durability']['RX'];self.assertEqual(stats['acquisition_samples'],302)
                self.assertEqual((stats['fresh_samples'],stats['held_samples'],stats['acquisition_context_changes'],stats['max_acquisition_reference_age_ms']),(0,0,1,602000))
                self.assertIsNone(stats['max_hold_reference_age_ms']);self.assertFalse(m.gates['RX'].qualified);self.assertIsNone(m.armed_at)
                m.csv.flush()
                with (out/'samples.csv').open() as stream:rows=list(csv.DictReader(stream))
                self.assertEqual(rows[-1]['display_acquiring'],'1');self.assertEqual(rows[-1]['trained'],'0')
            finally:m.failure.close();m.csv.close()

    def test_older_display_fields_remain_unknown_and_gaps_are_retained(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(0),host_mono_ns=1))
                for n,(stamp,utc) in enumerate([(1000000000,100),(3000000000,100),(5000000000,99),(20000000000,200)]):
                    p=sample(n*2);p.update(display_available=True,display_held=True,display_utc_us=utc,display_sampled_us=n+1,display_ref_age_ms=n)
                    m.event('RX',dict(kind='reply',op='lora-diag',payload=p,host_mono_ns=stamp))
                stats=m.summary()['display_durability']['RX']
                self.assertEqual((stats['unsupported_samples'],stats['nonadvancing_intervals'],stats['backward_intervals'],stats['host_gap_intervals']),(1,1,1,1))
                m.csv.flush()
                with (out/'samples.csv').open() as stream:rows=list(csv.DictReader(stream))
                self.assertEqual(rows[0]['display_available'],'');self.assertEqual(rows[0]['display_ref_age_ms'],'')
            finally:m.failure.close();m.csv.close()

    def test_ram_profile_and_cohort_boundary(self):
        g=Gate(5000)
        for t in range(0,603,2):
            p=sample(t);p['profile']='RAM_60_300';g.observe(p,int((t+1)*1e9))
        self.assertTrue(g.qualified)
        self.assertEqual(g.observe(sample(604),605000000000),'source or capture context changed')
        self.assertFalse(g.qualified)

    def test_current_profile_lease_and_physical_cadence_bound(self):
        def current(s):
            p=sample(s);p.update(beat_us=60000000,hold_us=120000000,lease_us=420000000,cadence_bound_us=62510000)
            p['meta_until_us']+=58000000;p['physical_until_us']+=58000000
            return p
        g=Gate(5000)
        for s in range(0,603,2):g.observe(current(s),int((s+1)*1e9))
        self.assertTrue(g.qualified);self.assertEqual(g.meta_origin,10000000)
        for delta,failed in ((62510000,False),(62510001,True)):
            g=Gate(5000);a=current(0);g.observe(a,1000000000);b=current(60)
            b['event_mono_us']=a['event_mono_us']+delta
            b['event_tick']=a['event_tick']+delta*16;b['event_ns']=a['event_ns']+delta*1000
            b['at_us']=b['event_mono_us'];b['physical_until_us']=b['event_mono_us']+120000000
            self.assertEqual(g.observe(b,3000000000)=='precision cadence gap',failed)
        # Hold is not permission to miss the normal62.51s cadence.
        b=current(60);b['event_mono_us']=a['event_mono_us']+70000000
        self.assertEqual(Gate(5000).observe(current(0),1000000000),None)
        g=Gate(5000);g.observe(a,1000000000)
        self.assertEqual(g.observe(b,3000000000),'precision cadence gap')
        for change in (dict(cadence_bound_us=59000000),dict(cadence_bound_us=120000001),dict(lease_us=299999999),dict(beat_us=4000000),dict(cadence_bound_us=True)):
            p=current(0);p.update(change)
            with self.assertRaises(ValueError):Gate(5000).observe(p,1000000000)
        g=Gate(5000);g.observe(current(0),1000000000);p=current(2);p['lease_us']+=1
        self.assertEqual(g.observe(p,3000000000),'profile timing bounds changed')

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

    def test_pre_gate_TX_error_restarts_two_cycles_then_healthy_qualifies(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX','TX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            def tx(s,late=0):
                p=sample(s);p.update(transmitter=True,boot=p['emitter_boot'],late=late)
                m.event('TX',dict(kind='reply',op='lora-diag',payload=p,host_mono_ns=int((s+1)*1e9)))
            def rx(s):
                m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(s),host_mono_ns=int((s+1)*1e9)+1))
            try:
                for s in range(0,600,2):tx(s);rx(s)
                self.assertIsNotNone(m.gates['RX'].started)
                tx(599,1)
                self.assertIsNone(m.gates['RX'].started)
                tx(600,1);rx(600)
                self.assertIsNone(m.armed_at);self.assertFalse(m.gates['RX'].qualified)
                for s in range(602,1203,2):tx(s,1);rx(s)
                self.assertTrue(m.gates['RX'].qualified);self.assertIsNotNone(m.armed_at)
                self.assertEqual(m.violations,0)
                self.assertEqual(len((out/'failures.jsonl').read_text().splitlines()),1)
            finally:m.failure.close();m.csv.close()

    def test_each_TX_counter_regression_or_fault_breaks_matching_continuity(self):
        changes=[(field,None) for field in ('late','storage_fail','missing_map')]
        changes += [('hw_errors',i) for i in range(5)]
        changes += [('fault',None),('error',None)]
        for field,index in changes:
            for regression in (False,True) if field not in ('fault','error') else (False,):
                with self.subTest(field=field,index=index,regression=regression),tempfile.TemporaryDirectory() as folder:
                    out=Path(folder);m=Monitor(out,['RX','TX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
                    try:
                        tx=sample(0);tx['transmitter']=True
                        if regression:
                            if index is None:tx[field]=2
                            else:tx[field][index]=2
                        m.event('TX',dict(kind='reply',op='lora-diag',payload=copy.deepcopy(tx),host_mono_ns=1))
                        m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(0),host_mono_ns=1000000000))
                        self.assertIsNotNone(m.gates['RX'].started) # Historical TX baseline is allowed.
                        if index is not None:tx[field][index]=1
                        else:tx[field]=True if field=='fault' else -5 if field=='error' else 1
                        m.event('TX',dict(kind='reply',op='lora-diag',payload=tx,host_mono_ns=2000000000))
                        self.assertIsNone(m.gates['RX'].started);self.assertFalse(m.gates['RX'].qualified)
                        self.assertEqual(m.gates['RX'].resets,1);self.assertEqual(m.violations,0)
                    finally:m.failure.close();m.csv.close()

    def test_TX_failure_does_not_reset_unrelated_receiver(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX','OTHER','TX','FOREIGN'],['RX','OTHER'],5000,Budget(out,10**7,0),threading.Event())
            def rx(unit,s):
                p=sample(s)
                if unit=='OTHER':p['emitter']='other'
                m.event(unit,dict(kind='reply',op='lora-diag',payload=p,host_mono_ns=int((s+1)*1e9)))
            try:
                tx=sample(0);tx['transmitter']=True
                m.event('TX',dict(kind='reply',op='lora-diag',payload=copy.deepcopy(tx),host_mono_ns=1))
                for s in range(0,600,2):rx('RX',s);rx('OTHER',s)
                other_started=m.gates['OTHER'].started
                foreign=dict(tx,emitter='foreign',fault=True)
                m.event('FOREIGN',dict(kind='reply',op='lora-diag',payload=foreign,host_mono_ns=599000000000))
                self.assertIsNotNone(m.gates['RX'].started);self.assertEqual(m.gates['OTHER'].started,other_started)
                tx['late']=1
                m.event('TX',dict(kind='reply',op='lora-diag',payload=tx,host_mono_ns=600000000000))
                self.assertIsNone(m.gates['RX'].started);self.assertEqual(m.gates['OTHER'].started,other_started)
                rx('RX',600);rx('OTHER',600)
                self.assertFalse(m.gates['RX'].qualified);self.assertTrue(m.gates['OTHER'].qualified)
                self.assertIsNone(m.armed_at)
            finally:m.failure.close();m.csv.close()

    def test_tx_SPI_error_after_receiver_gate_is_retained(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX','TX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                tx=sample(0);tx.update(transmitter=True,sent=[1,2,3,4],received=[5,6,7,8],retries=9,owed=3,charged_us=17000000,tx_gate=256,pending=1,fh_disabled=0)
                m.event('TX',dict(kind='reply',op='lora-diag',payload=copy.deepcopy(tx),host_mono_ns=1))
                for s in range(0,603,2):m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(s),host_mono_ns=int((s+1)*1e9)))
                self.assertIsNotNone(m.armed_at)
                tx['hw_errors'][2]=1
                m.event('TX',dict(kind='reply',op='lora-diag',payload=copy.deepcopy(tx),host_mono_ns=604000000000))
                self.assertEqual(m.violations,1)
                # Stable errors do not multiply violations; recovery cannot
                # make the final zero-violation PASS predicate true again.
                m.event('TX',dict(kind='reply',op='lora-diag',payload=tx,host_mono_ns=606000000000))
                self.assertEqual(m.summary()['post_gate_violations'],1)
                self.assertFalse(m.gates['RX'].qualified)
                for s in range(608,1211,2):
                    m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(s),host_mono_ns=int((s+1)*1e9)))
                self.assertTrue(m.gates['RX'].qualified)
                self.assertEqual(m.violations,1)
                self.assertIn('hardware error counter advanced',(out/'failures.jsonl').read_text())
                m.csv.flush()
                with (out/'samples.csv').open() as stream:rows=list(csv.DictReader(stream))
                last=next(row for row in reversed(rows) if row['unit']=='TX');self.assertNotIn(None,last)
                for field,value in dict(sent_disc='1',received_follow='8',retries='9',owed='3',charged_us='17000000',tx_gate='256',pending='1',hw_spi_error='1').items():self.assertEqual(last[field],value)
            finally:m.failure.close();m.csv.close()

    def test_tx_counter_regression_after_gate_is_not_new_baseline(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX','TX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                tx=sample(0);tx.update(transmitter=True,late=2);tx['hw_errors'][2]=3
                m.event('TX',dict(kind='reply',op='lora-diag',payload=copy.deepcopy(tx),host_mono_ns=1))
                self.assertEqual(m.violations,0) # Historical baseline is retained.
                for s in range(0,603,2):m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(s),host_mono_ns=int((s+1)*1e9)))
                tx['late']=0;tx['hw_errors'][2]=0
                m.event('TX',dict(kind='reply',op='lora-diag',payload=tx,host_mono_ns=604000000000))
                self.assertEqual(m.violations,1)
                self.assertIn('error counter regressed',(out/'failures.jsonl').read_text())
            finally:m.failure.close();m.csv.close()

    def test_qualified_receiver_cannot_arm_before_host_hold(self):
        with tempfile.TemporaryDirectory() as folder,patch('lora_soak.time.monotonic') as clock:
            clock.return_value=0;out=Path(folder)
            m=Monitor(out,['RX'],['RX'],5000,Budget(out,10**7,0),threading.Event(),qualify_after_seconds=660)
            try:
                for s in range(0,660,2):
                    clock.return_value=s
                    m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(s),host_mono_ns=int((s+1)*1e9)))
                self.assertTrue(m.gates['RX'].qualified);self.assertIsNone(m.armed_at)
                clock.return_value=660
                m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(660),host_mono_ns=661000000000))
                self.assertIsNotNone(m.armed_at);self.assertEqual(m.summary()['qualify_after_seconds'],660)
            finally:m.failure.close();m.csv.close()

    def test_expired_fit_at_hold_end_cannot_arm(self):
        with tempfile.TemporaryDirectory() as folder,patch('lora_soak.time.monotonic') as clock:
            clock.return_value=0;out=Path(folder)
            m=Monitor(out,['RX'],['RX'],5000,Budget(out,10**7,0),threading.Event(),qualify_after_seconds=660)
            try:
                for s in range(0,660,2):
                    clock.return_value=s
                    m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(s),host_mono_ns=int((s+1)*1e9)))
                clock.return_value=660;p=sample(660);p['trained']=False
                m.event('RX',dict(kind='reply',op='lora-diag',payload=p,host_mono_ns=661000000000))
                self.assertIsNone(m.armed_at);self.assertFalse(m.gates['RX'].qualified)
                self.assertIn('trained timing fit lost',(out/'failures.jsonl').read_text())
                self.assertEqual(m.violations,0) # Retained startup failure, not certified time.
            finally:m.failure.close();m.csv.close()

    def test_unrelated_TX_after_hold_cannot_arm_stale_receiver(self):
        with tempfile.TemporaryDirectory() as folder,patch('lora_soak.time.monotonic') as clock:
            clock.return_value=0;out=Path(folder)
            m=Monitor(out,['RX','TX'],['RX'],5000,Budget(out,10**7,0),threading.Event(),qualify_after_seconds=660)
            try:
                for s in range(0,603,2):
                    clock.return_value=s
                    m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(s),host_mono_ns=int((s+1)*1e9)))
                self.assertTrue(m.gates['RX'].qualified);self.assertIsNone(m.armed_at)
                clock.return_value=660;tx=sample(660);tx['transmitter']=True
                m.event('TX',dict(kind='reply',op='lora-diag',payload=tx,host_mono_ns=661000000000))
                self.assertIsNone(m.armed_at);self.assertFalse(m.gates['RX'].qualified)
                self.assertIn('qualified receiver capture stale',(out/'failures.jsonl').read_text())
            finally:m.failure.close();m.csv.close()

    def test_final_drain_freshness_and_sleep_past_deadline(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                for s in range(0,603,2):m.event('RX',dict(kind='reply',op='lora-diag',payload=sample(s),host_mono_ns=int((s+1)*1e9)))
                self.assertIsNotNone(m.armed_at)
                good=m.finish(610000000000) # Normal7s final drain remains fresh.
                self.assertTrue(good['receivers']['RX']['qualified']);self.assertEqual(good['post_gate_violations'],0)
                stale=m.finish(614000000000) # Host stalled11s without another reply.
                self.assertFalse(stale['receivers']['RX']['qualified']);self.assertEqual(stale['post_gate_violations'],1)
                self.assertIn('qualified receiver capture stale',(out/'failures.jsonl').read_text())
            finally:m.failure.close();m.csv.close()

    def test_manual_requires_matching_transmitter_and_ordinary_META(self):
        p=sample(0);p.update(manual_jobs=0,manual_active=False,manual_serial=0,manual_stream=0)
        g=Gate(5000)
        self.assertIn('transmitter observation unavailable',g.observe(p,1000000000))
        tx=dict(p,transmitter=True,boot=p['emitter_boot'],manual_jobs=1,manual_active=False,manual_serial=1,manual_stream=1)
        self.assertIn('lab UTC META influence',g.observe(p,3000000000,tx))
        tx['manual_active']=True
        self.assertEqual(g.observe(p,5000000000,tx),'lab UTC request active')
        tx.update(manual_active=False,manual_stream=2)
        self.assertIsNone(Gate(5000).observe(p,7000000000,tx)) # A retired stream cannot taint a new stream.

    def test_completed_between_polls_invalidates_and_recovers_gate(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX','TX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            def event(s,jobs=0,active=False,floor=0):
                p=sample(s);p.update(manual_jobs=0,manual_active=False,manual_serial=0,manual_stream=0)
                tx=dict(p,transmitter=True,boot=p['emitter_boot'],manual_jobs=jobs,manual_active=active,manual_serial=floor,manual_stream=1)
                stamp=int((s+1)*1e9)
                m.event('TX',dict(kind='reply',op='lora-diag',payload=tx,host_mono_ns=stamp))
                m.event('RX',dict(kind='reply',op='lora-diag',payload=p,host_mono_ns=stamp+1))
            try:
                for s in range(0,603,2):event(s)
                self.assertTrue(m.gates['RX'].qualified)
                event(604,jobs=1,floor=3) # Already completed, no active poll was seen.
                self.assertFalse(m.gates['RX'].qualified);self.assertGreaterEqual(m.violations,1)
                for s in range(606,900,2):event(s,jobs=1,floor=3)
                self.assertIsNone(m.gates['RX'].started)
                # Serial4 at900 is the first ordinary META after the manual floor.
                for s in range(900,1503,2):event(s,jobs=1,floor=3)
                self.assertTrue(m.gates['RX'].qualified)
                self.assertIn('admission counter changed',(out/'failures.jsonl').read_text())
                event(1504,jobs=0,floor=0) # Reset cannot hide influence in a certified window.
                self.assertFalse(m.gates['RX'].qualified)
            finally:m.failure.close();m.csv.close()

    def test_manual_transmitter_freshness_and_foreign_boot(self):
        p=sample(0);p.update(manual_jobs=0)
        tx=dict(p,transmitter=True,boot='foreign',manual_jobs=0,manual_active=False,manual_serial=0,manual_stream=0)
        self.assertIn('transmitter observation unavailable',Gate(5000).observe(p,1,tx))
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);m=Monitor(out,['RX','TX'],['RX'],5000,Budget(out,10**7,0),threading.Event())
            try:
                tx['boot']=p['emitter_boot']
                m.event('TX',dict(kind='reply',op='lora-diag',payload=tx,host_mono_ns=1))
                m.event('RX',dict(kind='reply',op='lora-diag',payload=p,host_mono_ns=11000000000))
                self.assertIn('transmitter observation unavailable',m.gates['RX'].reason)
            finally:m.failure.close();m.csv.close()

    def test_new_TX_manual_META_excludes_older_RX(self):
        old=sample(0) # Old wire3 receiver exposes no manual-job fields.
        tx=dict(old,transmitter=True,boot=old['emitter_boot'],manual_jobs=1,manual_active=False,manual_serial=1,manual_stream=1)
        self.assertIn('lab UTC META influence',Gate(5000).observe(old,1000000000,tx))
        tx['manual_active']=True
        self.assertEqual(Gate(5000).observe(old,1000000000,tx),'lab UTC request active')
        tx['manual_active']=False
        ordinary=sample(300)
        self.assertIsNone(Gate(5000).observe(ordinary,301000000000,tx))


if __name__=='__main__':unittest.main()
