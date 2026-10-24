"""Review-only schedule arithmetic. MIT Jonathan Yantis 2026."""
import unittest
from windows import *
class WindowsTests(unittest.TestCase):
 def test_rotating_colliding_low_bits_and_extremes(self):
  ids=(1,9,17,25)
  for a in ids:
   for b in ids:
    if a!=b:self.assertTrue(any(status_slot(a,11,c)!=status_slot(b,11,c) for c in range(32)))
  for identity in ids:
   for cycle in range(32):
    for jitter in (-250,250):verify_window(identity,11,cycle,jitter)
 def test_exhaustion(self):
  for kw in ({'guard':8001},{'airtime':2001},{'scheduling':2001}):
   with self.assertRaises(ValueError):verify_window(1,11,0,0,**kw)
if __name__=='__main__':unittest.main()
