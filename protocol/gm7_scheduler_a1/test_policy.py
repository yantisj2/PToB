"""MIT Jonathan Yantis 2026. Schedule opportunities, not RF success."""
import unittest
from policy import *
class Tests(unittest.TestCase):
 def test_pairs(self):
  ids=[1,9,17,25,2**63,2**64-1]+[1<<b for b in range(64)]+[(2**64-1)^(1<<b) for b in range(64)]
  for a in ids:
   for b in ids:
    if a==b:continue
    self.assertTrue(any(discovery(a,n)!=discovery(b,n) for n in range(0,256,4)))
 def test_phase_and_suppression(self):
  for start in (0,4,252,256,1024):
   self.assertTrue(any(discovery(1,n)!=discovery(1+(1<<63),n) for n in range(start,start+256,4)))
  for kw in ({'event':True},{'withdrawn':True},{'eligible':False}):self.assertEqual(discovery(1,0,**kw),'listen')
  self.assertEqual(discovery(1,0,aligned=False),'broad-discovery')
  for bad in (0,-1,2**64,True):
   with self.assertRaises(ValueError):discovery(bad,0)
 def test_retry_and_admission(self):
  for initial in range(4):
   t=initial*FRAME_US+90000;residues=[];accepted=[]
   for attempt in range(8):
    nxt=retry_after(t,attempt);self.assertGreaterEqual(nxt-t,MIN_RETRY_US);self.assertLess(nxt-t,3000000)
    residues.append(frame(nxt)%4);accepted.append(response_admitted(nxt));t=nxt
   self.assertEqual(residues[:4],[0,1,2,3]);self.assertEqual(sum(accepted[:4]),3)
  self.assertFalse(response_admitted(3*FRAME_US+90000))
 def test_primary_unchanged(self):
  for n in range(256):
   copies,deadline=primary_event(n*FRAME_US+38000)
   self.assertEqual(copies,(n*FRAME_US+38000,(n+1)*FRAME_US+38000,(n+2)*FRAME_US+38000))
   self.assertEqual(deadline,n*FRAME_US+788000)
   for f in range(n,n+3):
    if reserved(f):self.assertEqual(discovery(2**64-1,f,event=True),'listen')
if __name__=='__main__':unittest.main()
