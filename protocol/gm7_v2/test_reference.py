"""Offline wire/admission review tests. Jonathan Yantis (c) 2026 MIT."""
import json,unittest
from pathlib import Path
from reference import *
from admission import *

class ReviewTests(unittest.TestCase):
 def setUp(self):self.rows={r['name']:r for r in examples()['vectors']}
 def test_session_group_and_monotonic_reset(self):
  a=self.rows['serving-anchor']['decoded'];r=self.rows['bootstrap-request']['decoded']
  session=anchor_reply(a,r,10000000,10100000)
  p=self.rows['announce']['decoded']
  with self.assertRaises(ValueError):session.accept_control(dict(p,group=2),10100001)
  with self.assertRaises(ValueError):session.accept_control(p,9999999)
 def test_known_answers_and_lengths(self):
  self.assertEqual(json.loads(Path(__file__).with_name('vectors.json').read_text()),examples())
  for row in self.rows.values():
   raw=bytes.fromhex(row['hex']);self.assertEqual(decode(raw),row['decoded']);self.assertLessEqual(len(raw)+4,191)
 def test_bit_changes(self):
  for row in self.rows.values():
   raw=bytes.fromhex(row['hex'])
   for offset in range(len(raw)):
    for bit in range(8):
     with self.assertRaises(ValueError):decode(raw[:offset]+bytes([raw[offset]^(1<<bit)])+raw[offset+1:])
 def test_bounds_and_flags(self):
  for row in self.rows.values():
   p=row['decoded']
   for key,fmt in SCHEMAS[p['kind']]:
    for val in (-1,1<<(struct.calcsize(fmt)*8),True):
     with self.assertRaises(ValueError):encode(dict(p,**{key:val}))
   with self.assertRaises(ValueError):encode(p,bytes(32))
   with self.assertRaises(ValueError):encode(dict(p,flags=65535))
 def test_relay_anchor_never_independent_candidate(self):
  p=self.rows['relay-anchor']['decoded'];req=dict(self.rows['bootstrap-request']['decoded'],target=p['responder'],target_boot=p['responder_boot'])
  self.assertTrue(anchor_reply(p,req,1000000,1100000).serving)
  self.assertNotEqual(p['active'],p['responder']);self.assertEqual(p['flags']&1,0)
  for changed in ({'flags':23},{'depth':0},{'depth':3},{'source_gen':3}):
   with self.assertRaises(ValueError):encode(dict(p,**changed))

 def test_nonce_scope_and_rtt(self):
  req=self.rows['bootstrap-request']['decoded'];p=self.rows['serving-anchor']['decoded']
  s=anchor_reply(p,req,1000000,1100000);self.assertTrue(s.serving)
  self.assertEqual(s.source_until,2200000)
  for changed in ({'nonce_lo':1},{'requester_boot':3},{'responder_boot':4},{'responder':5}):
   with self.assertRaises(ValueError):anchor_reply(dict(p,**changed),req,1000000,1100000)
  with self.assertRaises(ValueError):anchor_reply(p,req,1000000,1250001)
  with self.assertRaises(ValueError):anchor_reply(dict(p,valid_us=100000),req,1000000,1100000)
 def test_candidate_freshness_not_timing(self):
  p=self.rows['candidate-anchor']['decoded'];req=dict(self.rows['bootstrap-request']['decoded'],target=p['responder'],target_boot=p['responder_boot'])
  s=anchor_reply(p,req,1000000,1100000);self.assertFalse(s.serving)
  status=dict(self.rows['eligible-candidate-status']['decoded'],sent_mono_us=p['sampled_mono_us'])
  self.assertGreater(s.accept_control(status,1100000),1100000)
  with self.assertRaises(ValueError):s.accept_control(status,1100001)
  with self.assertRaises(ValueError):s.accept_control(dict(status,sequence=4),s.until)
 def test_continuous_permission_and_exact_expiry(self):
  p=self.rows['serving-anchor']['decoded'];req=self.rows['bootstrap-request']['decoded']
  s=anchor_reply(p,req,1000000,1100000);a=self.rows['announce']['decoded']
  for step in range(12):
   q=dict(a,sequence=7+step,sent_mono_us=a['sent_mono_us']+step*250000)
   now=1100000+step*250000
   s.accept_control(q,now);self.assertGreater(s.source_until,now+250000)
  expired=dict(a,sequence=99,source_valid_us=110000)
  s2=anchor_reply(p,req,1000000,1100000)
  with self.assertRaises(ValueError):s2.accept_control(expired,1100000)
  withdrawn=dict(a,kind=3,flags=0,sequence=99,source_valid_us=0,max_age_us=1000000,sent_mono_us=a['sent_mono_us']+3000000)
  s.accept_control(withdrawn,4100000);self.assertEqual(s.source_until,4100000)

 def test_original_age_and_sequence_floor(self):
  p=self.rows['serving-anchor']['decoded'];req=self.rows['bootstrap-request']['decoded']
  s=anchor_reply(p,req,1000000,1100000);a=self.rows['announce']['decoded']
  with self.assertRaises(ValueError):s.accept_control(dict(a,sequence=6),1100000)
  expiry=s.accept_control(a,1100000)
  self.assertEqual(expiry,3740000)
  with self.assertRaises(ValueError):s.accept_control(dict(a,sequence=8,sent_mono_us=999999999),1100001)

if __name__=='__main__':unittest.main(verbosity=2)
