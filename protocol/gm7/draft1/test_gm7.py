"""Offline synthetic checks. Copyright (c) 2026 Jonathan Yantis. MIT."""
import itertools
import json
from pathlib import Path
import unittest
from dataclasses import replace
from codec import *
from policy import *
from examples import packet, examples, T


def accepted(p, mono=100, verifier=None, lo=T, hi=T):
    v=verifier or Verifier(PUBLIC_KEY,p['group'],p['key_epoch'])
    return v.accept(encode(p,PUBLIC_KEY),mono,lo,hi,'GNSS_UTC')


def active(identity, birth=T-10000000, founder=1, term=1, **kw):
    return packet(sender=identity,active=identity,birth_us=birth,founder=founder,term=term,**kw)


class CodecTests(unittest.TestCase):
    def test_known_answers(self):
        self.assertEqual(json.loads(Path(__file__).with_name('vectors.json').read_text()),examples())
        for e in examples()['examples']:
            self.assertEqual(encode(e['decoded'],PUBLIC_KEY).hex(),e['hex'])

    def test_single_byte_corruption_every_position(self):
        p=packet(); w=encode(p,PUBLIC_KEY)
        for i in range(len(w)):
            changed=w[:i]+bytes([w[i]^1])+w[i+1:]
            with self.assertRaises(ValueError): decode(changed,PUBLIC_KEY,p['group'],1)

    def test_lengths_key_group_version_reserved(self):
        p=packet();w=encode(p,PUBLIC_KEY)
        for bad in (w[:-1],w+b'\x00',b'',w[:4]+b'\x02'+w[5:]):
            with self.assertRaises(ValueError):decode(bad,PUBLIC_KEY,p['group'],1)
        with self.assertRaises(ValueError):decode(w,bytes(32),p['group'],1)
        with self.assertRaises(ValueError):decode(w,PUBLIC_KEY,p['group']+1,1)
        with self.assertRaises(ValueError):decode(w,PUBLIC_KEY,p['group'],2)
        self.assertEqual(SIZE,162)
        self.assertLessEqual(SIZE+3,255) # AD type + synthetic company + PDU

    def test_zero_key_rejected_even_with_matching_mac(self):
        p=packet();body=encode(p,PUBLIC_KEY)[:-16]
        zero=bytes(32)
        forged=body+hmac.digest(zero,DOMAIN+body,'sha256')[:16]
        with self.assertRaises(ValueError):encode(p,zero)
        with self.assertRaises(ValueError):decode(forged,zero,p['group'],1)
        with self.assertRaises(ValueError):Verifier(zero,p['group'],1)

    def test_all_unsigned_fields_bounds_and_bool(self):
        for name,fmt in FIELDS:
            for bad in (-1,1<<(8*__import__('struct').calcsize(fmt)),True,1.0):
                with self.assertRaises(ValueError):encode(dict(packet(),**{name:bad}),PUBLIC_KEY)

    def test_semantic_invalid(self):
        for change in ({'birth_us':T+1},{'birth_us':0},{'birth_us':UTC_MIN-1},
                       {'sent_us':UTC_MAX},{'source_gen':0},{'flags':16},
                       {'flags':ELIGIBLE|HOT},{'active':9},{'hot_boot':0},
                       {'term':0},{'max_age_us':0},{'max_age_us':2750001},
                       {'yield_us':T+1},{'flags':ELIGIBLE|YIELD,'yield_us':T+1000001}):
            with self.assertRaises(ValueError):encode(dict(packet(),**change),PUBLIC_KEY)
        for change in ({'flags':ELIGIBLE|HOT},{'flags':SHADOW},
                       {'hot':2,'hot_boot':3},{'yield_us':T+1},
                       {'flags':SHADOW,'shadow_gen':2}):
            with self.assertRaises(ValueError):encode(packet(STATUS,**change),PUBLIC_KEY)


class FreshnessTests(unittest.TestCase):
    def test_delayed_packet_keeps_original_age_budget(self):
        delayed=accepted(packet(sent_us=T-2000000),mono=10000000)
        self.assertEqual(delayed.until_mono,10750000)
        # 2s transport delay leaves only .75s, not another 2.75s from receipt.
        self.assertIsNone(merge_choice([delayed],10750000))

    def test_age_future_boundaries(self):
        accepted(packet(sent_us=T-2749999))
        with self.assertRaises(ValueError):accepted(packet(sent_us=T-2750000))
        accepted(packet(sent_us=T+100000))
        with self.assertRaises(ValueError):accepted(packet(sent_us=T+100001))
        r=accepted(packet(sent_us=T+100000))
        self.assertEqual(r.until_mono,100+2750000)
        r=accepted(packet(sent_us=T-100000),hi=T+100000)
        self.assertEqual(r.until_mono,100+2550000)

    def test_anchor_and_overflow(self):
        p=packet(); v=Verifier(PUBLIC_KEY,p['group'],1);w=encode(p,PUBLIC_KEY)
        for lo,hi,domain in ((T,T,'BLE_SHADOW'),(T+1,T,'GNSS_UTC'),(T,T+200001,'GNSS_UTC')):
            with self.assertRaises(ValueError):v.accept(w,100,lo,hi,domain)
        with self.assertRaises(ValueError):v.accept(w,U64,T,T,'GNSS_UTC')
        self.assertEqual(v.seen,{})

    def test_replays_and_timestamp_regression(self):
        p=packet();v=Verifier(PUBLIC_KEY,p['group'],1)
        accepted(p,verifier=v)
        for q in (p,dict(p,sequence=0),dict(p,sequence=2,sent_us=T-1)):
            with self.assertRaises(ValueError):accepted(q,verifier=v)
        accepted(dict(p,sequence=2),verifier=v)
        with self.assertRaises(ValueError):accepted(dict(p,sequence=3),mono=99,verifier=v)
        # Cold boot resets cache: recent replay remains possible, documented gate.
        accepted(p)

    def test_bad_mac_does_not_consume_sequence(self):
        p=packet();v=Verifier(PUBLIC_KEY,p['group'],1);w=encode(p,PUBLIC_KEY)
        with self.assertRaises(ValueError):v.accept(w[:-1]+bytes([w[-1]^1]),100,T,T,'GNSS_UTC')
        accepted(p,verifier=v)


class EpochTests(unittest.TestCase):
    def qualified(self):
        a=Admission()
        for t in (0,30000000,59999999,60000000):
            a.update(t,(1,2,3),t+40000000,gm=True,gnss=True,independent=True)
        return a

    def test_dwell_exact_and_local_independent(self):
        a=Admission()
        for t,expected in ((0,False),(30000000,False),(59999999,False),(60000000,True)):
            self.assertEqual(a.update(t,(1,2,3),t+40000000,gm=True,gnss=True,independent=True),expected)
        self.assertFalse(a.update(60000001,(1,2,3),90000000,gm=True,gnss=True,independent=False))
        self.assertFalse(a.update(60000002,(1,2,3),90000000,gm=True,gnss=True,independent=True))

    def test_source_change_gap_and_regression_reset(self):
        for source,now in (((2,2,3),60000001),((1,3,3),60000001),((1,2,4),60000001),((1,2,3),100000000),((1,2,3),59999999)):
            a=self.qualified()
            self.assertFalse(a.update(now,source,now+10000000,gm=True,gnss=True,independent=True))
        a=self.qualified();a.update(1,(1,2,3),100,gm=True,gnss=True,independent=True)
        self.assertTrue(a.failed)

    def test_routine_pps_and_fit_refresh_preserve_dwell(self):
        a=Admission()
        for t in range(0,60000001,1000000):
            result=a.update(t,(1,2,3),t+2000000,gm=True,gnss=True,independent=True)
        self.assertTrue(result)
        first=UTCMap(60000000,T,62000000,0,(1,2,3),True)
        refined=replace(first,anchor_utc=T+1,rate_ppb=-1000)
        self.assertEqual(create_lineage(7,60000000,a,first),(T,7,1))
        self.assertEqual(create_lineage(7,60000000,a,refined),(T+1,7,1))
        self.assertEqual(a.since,0)
        # Refresh fits before a transition; create_lineage is never called again
        # once the owner has created/inherited its lineage.

    def test_transition_projection_and_inheritance(self):
        a=self.qualified();m=UTCMap(59000000,T,70000000,1000,(1,2,3),True)
        self.assertEqual(create_lineage(7,60000000,a,m),(T+1000001,7,1))
        self.assertEqual(successor_lineage((T,7,9)),(T,7,10))
        with self.assertRaises(ValueError):successor_lineage((T,7,U64))
        for altered in (replace(m,leap_clear=False),replace(m,valid_until=60000000),replace(m,source=(9,2,3)),replace(m,anchor_utc=UTC_MAX-1),replace(m,rate_ppb=100001)):
            with self.assertRaises(ValueError):create_lineage(7,60000000,a,altered)
        with self.assertRaises(ValueError):create_lineage(7,60000001,a,m)

    def test_only_selected_active_can_seed(self):
        a,b=accepted(active(1)),accepted(active(2,birth=T-1))
        self.assertEqual(inherit_selected(a,[a,b],100),(T-10000000,1,1))
        for candidate,rows in ((b,[a,b]),(None,[]),(accepted(packet(STATUS)),[accepted(packet(STATUS))]),(a,[a])):
            if candidate==a: now=a.until_mono
            else: now=100
            with self.assertRaises(ValueError):inherit_selected(candidate,rows,now)


class ElectionTests(unittest.TestCase):
    def test_withdraw_never_contends_or_initializes(self):
        yielded=accepted(packet(kind=WITHDRAW,flags=0,max_age_us=1000000))
        cleared=accepted(packet(kind=WITHDRAW,flags=0,max_age_us=1000000,source_gen=0,capture_gen=0))
        self.assertIsNone(merge_choice([cleared],100))
        candidate=accepted(packet(STATUS,sender=2))
        for rows in ((yielded,candidate),(candidate,yielded)):
            self.assertEqual(initial_choice(rows,100),candidate)
            self.assertIsNone(merge_choice(rows,100))
        with self.assertRaises(ValueError):inherit_selected(yielded,[yielded],100)
        with self.assertRaises(ValueError):encode(packet(flags=0),PUBLIC_KEY)
        with self.assertRaises(ValueError):encode(packet(flags=YIELD,yield_us=T+1,hot=0,hot_boot=0),PUBLIC_KEY)
        self.assertEqual(candidate.packet['train_gen'],0)
        self.assertEqual(candidate.packet['birth_us'],0)
        # Decoder structure does not grant current-nomination takeover authority.
        unknown=accepted(packet(kind=WITHDRAW,sender=9,active=9,flags=0,max_age_us=1000000))
        self.assertIsNone(merge_choice([unknown],100))

    def test_scoped_withdrawal_is_terminal_negative_state(self):
        c=accepted(packet())
        base=packet(kind=WITHDRAW,flags=0,max_age_us=1000000,sequence=2)
        fence=WithdrawalFence();w=accepted(base)
        for changes in ({'active':9,'sender':9},{'boot':9,'active_boot':9},
                        {'term':2},{'founder':9},{'birth_us':T-1},
                        {'train_gen':6},{'sequence':1},{'hot':9}):
            self.assertFalse(fence.consume(accepted(dict(base,**changes)),c,100))
        self.assertFalse(fence.consume(w,c,w.until_mono))
        self.assertTrue(fence.consume(w,c,100))
        self.assertFalse(fence.consume(w,c,100))
        self.assertTrue(fence.blocks(accepted(packet(sequence=99,train_gen=8))))
        self.assertFalse(fence.blocks(accepted(packet(term=2))))
        cp=c.packet
        standby=accepted(packet(STATUS,sender=2,birth_us=cp['birth_us'],
                                founder=cp['founder'],term=cp['term'],
                                active=cp['active'],active_boot=cp['active_boot']))
        self.assertFalse(fence.blocks(standby))
        self.assertEqual(initial_choice([r for r in [standby,c] if not fence.blocks(r)],100),standby)
        for changes in ({'flags':ELIGIBLE},{'flags':YIELD,'yield_us':T+1},
                        {'max_age_us':1000001},{'yield_us':T+1}):
            with self.assertRaises(ValueError):encode(dict(base,**changes),PUBLIC_KEY)
        with self.assertRaises(ValueError):encode(packet(flags=YIELD,yield_us=T+1),PUBLIC_KEY)

    def test_quality_and_active_suppression(self):
        a=accepted(packet(STATUS,sender=3,variance=99))
        b=accepted(packet(STATUS,sender=2,variance=100,priority2=0))
        for rows in itertools.permutations([a,b]):self.assertEqual(initial_choice(rows,100),a)
        self.assertIsNone(initial_choice([a,accepted(active(4))],100))

    def test_merge_age_founder_term_identity(self):
        cases=[(active(8,birth=T-20,founder=8,term=1,priority1=255),active(1,birth=T-10,founder=1,term=999)),
               (active(8,founder=1,term=1),active(1,founder=2,term=999)),
               (active(8,term=2),active(1,term=1)),
               (active(1,term=2),active(8,term=2))]
        for p,q in cases:
            a,b=accepted(p),accepted(q)
            for rows in ((a,b),(b,a)):self.assertEqual(merge_choice(rows,100),a)

    def test_duplicate_even_ineligible_and_invalid_order(self):
        a=accepted(packet(STATUS,sender=1))
        dup=accepted(packet(STATUS,sender=1,boot=9,flags=0))
        b=accepted(packet(STATUS,sender=2,priority1=200))
        invalid=replace(a,packet_items=tuple(dict(a.packet,variance=-1).items()))
        for rows in itertools.permutations([a,dup,b,invalid]):
            self.assertEqual(initial_choice(rows,100),b)
        for rows in ((a,invalid,b),(invalid,a,b)):
            self.assertEqual(initial_choice(rows,100),a)
        self.assertEqual(initial_choice([a,a],100),a)
        aa=accepted(active(1)); old=accepted(active(1,boot=9,active_boot=9))
        self.assertIsNone(merge_choice([aa,old],100))

    def test_known_term_floor_and_group_scope(self):
        old=accepted(active(1,term=1)); new=accepted(active(2,term=2))
        floors={(T-10000000,1):2}
        self.assertIsNone(merge_choice([old],100,floors))
        candidate=accepted(packet(STATUS,sender=3))
        self.assertEqual(initial_choice([old,candidate],100,floors),candidate)
        self.assertIsNone(initial_choice([new,candidate],100,floors))
        self.assertEqual(merge_choice([old,new],100,floors),new)
        with self.assertRaises(ValueError):inherit_selected(old,[old],100,floors)
        other=accepted(active(3,founder=2,term=1))
        self.assertEqual(merge_choice([other],100,floors),other)
        foreign=accepted(packet(STATUS,group=99))
        own=accepted(packet(STATUS,sender=2))
        for rows in ((foreign,own),(own,foreign)):
            self.assertIsNone(initial_choice(rows,100))
        # The API accepts current snapshots, never an append-only packet history.
        a=accepted(packet(STATUS)); b=accepted(packet(STATUS,sequence=2))
        self.assertIsNone(initial_choice([a,b],100))
        self.assertEqual(initial_choice([b],100),b)

    def test_expired_unknown_and_before_receive(self):
        a=accepted(active(1))
        self.assertIsNone(merge_choice([a],a.until_mono))
        self.assertIsNone(merge_choice([a],99))
        self.assertIsNone(merge_choice([replace(a,anchor_domain='UNKNOWN')],100))

if __name__=='__main__': unittest.main(verbosity=2)
