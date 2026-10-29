"""MIT Jonathan Yantis2026. No radio or precision claim."""
import importlib.util,json,pathlib,unittest
from policy import *
ROOT=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('c1_prior_codec',ROOT.parent/'gm7_v2/reference.py');codec=importlib.util.module_from_spec(spec);spec.loader.exec_module(codec)
class Tests(unittest.TestCase):
 def test_initial_and_preconditions(self):
  r=Reservation.initial(1792800000000000,17,10000000,10010000)
  self.assertFalse(r.committed);self.assertEqual(r.birth,1792800000000000)
  for kw in ({'qualified':False},{'winner':False}):
   with self.assertRaises(ValueError):Reservation.initial(r.birth,17,10000000,10010000,**kw)
  with self.assertRaises(ValueError):Reservation.initial(r.birth,17,10000000,10002999)
 def test_cancel_before_exposure(self):
  r=Reservation.initial(1792800000000000,17,10000000,10010000)
  self.assertIsNone(r.abort());self.assertFalse(r.terminal)
  with self.assertRaises(ValueError):r.aux_started()
 def test_exposure_and_abort_retains(self):
  r=Reservation.initial(1792800000000000,17,10000000,10010000);r.aux_started()
  self.assertEqual(r.abort(),(r.birth,17,1));self.assertTrue(r.terminal);self.assertFalse(r.committed)
  with self.assertRaises(ValueError):r.complete(10010000,10011000)
 def test_completion_and_failure(self):
  r=Reservation.initial(1792800000000000,17,10000000,10010000)
  with self.assertRaises(ValueError):r.complete(10010000,10011000)
  r.aux_started()
  for args in ((10009999,10011000,True),(10010000,10015000,True),(10010000,10011000,False)):
   with self.assertRaises(ValueError):r.complete(*args)
  r.complete(10010000,10011000);self.assertTrue(r.committed);self.assertEqual(r.abort(),(r.birth,17,1))
 def test_inheritance(self):
  r=Reservation.inherit(1792799990000000,17,4,10000000,10010000)
  self.assertEqual((r.birth,r.founder,r.term),(1792799990000000,17,5))
  with self.assertRaises(ValueError):Reservation.inherit(r.birth,17,2**64-1,10000000,10010000)
 def test_unchanged_wire(self):
  data=json.loads((ROOT/'vectors.json').read_text())
  for row in data['vectors']:
   raw=bytes.fromhex(row['hex']);self.assertEqual(codec.decode(raw),row['decoded']);self.assertEqual(codec.encode(row['decoded']),raw);self.assertEqual(len(raw),174)
  p=data['vectors'][0]['decoded'];self.assertEqual(p['birth_us'],p['sent_utc_us'])
  with self.assertRaises(ValueError):codec.encode(dict(p,birth_us=p['sent_utc_us']+10000))
if __name__=='__main__':unittest.main()
