"""MIT Jonathan Yantis 2026."""
import unittest
from reference import *
class Tests(unittest.TestCase):
 def test_contains_actual_and_permission(self):
  # Local real TX/RX, server sample in server clock with arbitrary offset.
  for queue in (0,1,10000,100000):
   for processing in (0,1,10000):
    lower=1000000;tx=lower+queue;rx=tx+10000;upper=rx+processing
    sample=tx+5000+7000000
    result=Pending.begin(lower).accept(upper,sample,1200000)
    self.assertLessEqual(result['offset_lo'],tx-sample)
    self.assertGreaterEqual(result['offset_hi'],rx-sample)
    self.assertLessEqual(result['source_until'],rx+1200000-(rx-tx))
    self.assertEqual(result['source_until'],lower+1200000)
 def test_original_deadline_queue_and_processing(self):
  self.assertEqual(Pending.begin(100).accept(250100,200,1000000)['remaining'],750000)
  for upper in (250101,500000):
   p=Pending.begin(100)
   with self.assertRaises(ValueError):p.accept(upper,200,1000000)
   self.assertEqual(p.deadline,250100)
 def test_scope_replay_reset_bounds(self):
  p=Pending.begin(100)
  with self.assertRaises(ValueError):p.accept(200,150,1000,False)
  self.assertFalse(p.consumed)
  p.accept(201,150,1000)
  with self.assertRaises(ValueError):p.accept(202,150,1000)
  p=Pending.begin(100)
  with self.assertRaises(ValueError):p.accept(99,150,1000)
  self.assertTrue(p.consumed)
  for x in (-1,True,U64-LIMIT+1):
   with self.assertRaises(ValueError):Pending.begin(x)
 def test_permission_exhaustion(self):
  p=Pending.begin(100)
  with self.assertRaises(ValueError):p.accept(1100,200,1000)
  self.assertTrue(p.consumed)
if __name__=='__main__':unittest.main()
