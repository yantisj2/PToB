"""Synthetic schedule checks. Copyright (c) 2026 Jonathan Yantis. MIT."""
import itertools
import unittest
from schedule import *

class ScheduleTests(unittest.TestCase):
    def test_windows_jitter_extremes_and_guards(self):
        for identity in range(1,17):
            for jitter in (-250,0,250):
                self.assertLessEqual(windows(identity,jitter)['guard_us'],8000)
                windows(identity,jitter,Budget(4500,500,2000,1000))
        for budget in (Budget(4501,500,2000,1000),Budget(trusted=False),Budget(mapping_us=-1)):
            with self.assertRaises(ValueError):windows(1,0,budget)
        with self.assertRaises(ValueError):windows(1,251)

    def test_each_single_and_two_copy_loss(self):
        for kind in ('YIELD','WITHDRAW'):
            e=create_event(kind,(1,2,3),1,1000000)
            self.assertEqual(e.copies,(1040000,1290000,1540000))
            self.assertLess(e.copies[-1]+2000+Budget().guard(),e.deadline)
            for count in (1,2):
                for lost in itertools.combinations(range(3),count):
                    book=EventBook();accepted=[]
                    for i,t in enumerate(e.copies):
                        if i not in lost:accepted.append(book.receive(e,t+2000))
                    self.assertEqual(sum(accepted),1)
                    self.assertEqual(e.deadline,1790000 if kind=='YIELD' else 2040000)

    def test_all_lost_and_late_first_success(self):
        e=create_event('YIELD',(1,),1,1000000);book=EventBook()
        self.assertEqual(book.pending,{}) # all copies lost: no yield authority
        self.assertEqual(blackout_first_tx_bound(e.origin_mono,e.origin_mono+2000000,e.origin_mono),
                         (e.origin_mono+2750000,e.origin_mono+2757500))
        with self.assertRaises(ValueError):
            blackout_first_tx_bound(e.origin_mono,e.origin_mono+2000000,e.origin_mono+249000)
        self.assertTrue(book.receive(e,e.copies[2]+2000))
        self.assertEqual(book.pending[e.scope].deadline,e.origin_mono+750000)
        self.assertFalse(EventBook().receive(e,e.deadline))
        for late in (3000000,3000001):
            with self.assertRaises(ValueError):
                blackout_first_tx_bound(e.origin_mono,e.origin_mono+late,e.origin_mono)

    def test_dispatch_no_late_shift_and_untrusted_alignment(self):
        e=create_event('WITHDRAW',(1,),1,1000000)
        for i,t in enumerate(e.copies):
            self.assertTrue(e.dispatch(i,t))
            self.assertTrue(e.dispatch(i,t+1))
            self.assertTrue(e.dispatch(i,t+2000))
            self.assertFalse(e.dispatch(i,t+2001))
            self.assertFalse(e.dispatch(i,e.deadline))
        with self.assertRaises(ValueError):e.dispatch(0,e.origin_mono,Budget(trusted=False))
        with self.assertRaises(ValueError):create_event('YIELD',(1,),1,0,Budget(mapping_us=9000))

    def test_event_copies_are_identical_authenticated_bytes(self):
        from examples import examples
        from codec import PUBLIC_KEY,Verifier
        for example in examples()['examples']:
            if example['name'] not in ('announce-yield','withdraw-source-invalid'):continue
            p=example['decoded']; wire=bytes.fromhex(example['hex'])
            copies=(wire,wire,wire)
            for first in range(3):
                verifier=Verifier(PUBLIC_KEY,p['group'],p['key_epoch'])
                utc=p['sent_us']+first*FRAME
                accepted=verifier.accept(copies[first],1000000+first*FRAME,utc,utc,'GNSS_UTC')
                self.assertEqual(accepted.until_mono,1000000+p['max_age_us'])
                for duplicate in copies:
                    with self.assertRaises(ValueError):
                        verifier.accept(duplicate,1000000+first*FRAME,utc,utc,'GNSS_UTC')

    def test_duplicates_reorder_supersession(self):
        y=create_event('YIELD',(1,),1,1000000)
        w=create_event('WITHDRAW',(1,),2,1250000)
        for events in ((y,w,y,w),(w,y,w,y)):
            b=EventBook()
            for e in events:b.receive(e,1550000)
            self.assertEqual(b.pending,{})
            self.assertIn(w.scope,b.terminal)
            self.assertFalse(b.can_dispatch(y,2,y.copies[2]))
            self.assertTrue(b.can_dispatch(w,2,w.copies[2]))
        b=EventBook();self.assertTrue(b.receive(y,y.origin_mono))
        later=create_event('YIELD',(1,),3,1250000)
        self.assertFalse(b.receive(later,later.origin_mono))
        self.assertEqual(b.pending[y.scope].deadline,y.deadline)

if __name__=='__main__':unittest.main()
