#!/usr/bin/env python3
"""Synthetic local-input validation; no enrollment files or hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import sys,copy
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build import GNSS_PROFILE,device_header,validate_inputs,provisional_profile

class DeviceInputs(unittest.TestCase):
 def setUp(self):
  self.enrollment={'group_id':'123456789abcdef0','key_epoch':1,'psk':'01'*32}
  self.device={'board':'meshnology_w12_esp32s3','factory_mac':'02:00:00:00:00:05','device_id':'0000020000000005','label':'Example','legacy_wire_id':2}
 def test_default_has_no_gnss(self):
  validate_inputs(self.enrollment,self.device)
  self.assertIn('#define PTOB_GNSS_INDICATORS 0\n',device_header(self.device))
 def test_alias_cannot_enable_gnss(self):
  self.device['label']='C-3';validate_inputs(self.enrollment,self.device)
  self.assertIn('#define PTOB_GNSS_INDICATORS 0\n',device_header(self.device))
 def test_explicit_route_enables_bound_capability(self):
  self.device['gnss_profile']=GNSS_PROFILE;validate_inputs(self.enrollment,self.device)
  self.assertIn('#define PTOB_GNSS_INDICATORS 1\n',device_header(self.device))
 def test_unknown_routes_refused(self):
  for value in (True,False,None,{},'auto','rx38','meshnology_w12_l76k'):
   with self.subTest(value=value),self.assertRaises(ValueError):
    validate_inputs(self.enrollment,{**self.device,'gnss_profile':value})
 def test_unexpected_pin_overrides_refused(self):
  with self.assertRaises(ValueError):validate_inputs(self.enrollment,{**self.device,'gnss_profile':GNSS_PROFILE,'rx_pin':38})
 def test_identity_not_replaced_by_gnss(self):
  self.device.update(gnss_profile=GNSS_PROFILE,device_id='0000020000000006')
  with self.assertRaises(ValueError):validate_inputs(self.enrollment,self.device)

class ProvisionalInputs(unittest.TestCase):
 def setUp(self):
  DeviceInputs.setUp(self)
  self.device['provisional_path']={'schema':1,'scope':'AGGREGATE_PATH_RELATIVE_PPS','offset_ns':47000,
   'root':'0000020000000001','root_boot':'0000000000000001','source_epoch':1,
   'receiver':'0000020000000002','receiver_source_sha256':'1'*64,
   'receiver_phase_correction_ns':10543,'evidence_sha256':'2'*64,'baseline_evidence_sha256':'3'*64}
 def test_explicit_profile_and_generated_identity(self):
  validate_inputs(self.enrollment,self.device);p=provisional_profile(self.device)
  self.assertEqual(p['offset_ns'],47000);self.assertIn('UINT64_C(0x'+p['id']+')',device_header(self.device))
  self.assertIn('UNKNOWN_RF_UTC',p['qualification'])
 def test_default_zero(self):
  del self.device['provisional_path'];self.assertIsNone(provisional_profile(self.device))
  self.assertNotIn('PTOB_PROVISIONAL',device_header(self.device))
 def test_every_metadata_change_binds_identity(self):
  first=provisional_profile(self.device)['id']
  for key in self.device['provisional_path']:
   if key in ('schema','scope'):continue
   d=copy.deepcopy(self.device);v=d['provisional_path'][key]
   d['provisional_path'][key]=v+1 if type(v)is int else ('1' if v[0]!='1' else '2')+v[1:]
   self.assertNotEqual(first,provisional_profile(d)['id'],key)
  d=copy.deepcopy(self.device);d['device_id']='0000020000000006'
  self.assertNotEqual(first,provisional_profile(d)['id'])
 def test_epoch_transfer_is_explicit_and_bound(self):
  p=provisional_profile(self.device);self.assertEqual(p['source_epoch_transfer'],'EXACT_TRAINING_EPOCH')
  self.device['provisional_path']['allow_source_epoch_transfer']=True
  q=provisional_profile(self.device);self.assertNotEqual(p['id'],q['id'])
  self.assertEqual(q['source_epoch_transfer'],'UNMEASURED_PROVISIONAL')
  self.assertIn('PTOB_PROVISIONAL_ALLOW_SOURCE_EPOCH_TRANSFER 1',device_header(self.device))
  for value in (1,0,None,'true'):
   self.device['provisional_path']['allow_source_epoch_transfer']=value
   with self.subTest(value=value),self.assertRaises(ValueError):validate_inputs(self.enrollment,self.device)
 def test_invalid_profile_refused(self):
  bad={'schema':[True,2,None],'scope':['TX_ONLY','UTC_CALIBRATED'],'offset_ns':[True,0,100001,-100001,47000.0],
   'source_epoch':[False,0,2**32],'root':['0'*16,'bad',11],'root_boot':['0'*16,None],
   'receiver':[None,'0'*16],'receiver_phase_correction_ns':[None,True,100001],
   'receiver_source_sha256':['0'*64,'x'*64,None],'evidence_sha256':['a'*63,None],
   'baseline_evidence_sha256':['0'*64,'x'*64]}
  for key,values in bad.items():
   for value in values:
    d=copy.deepcopy(self.device);d['provisional_path'][key]=value
    with self.subTest(key=key,value=value),self.assertRaises(ValueError):validate_inputs(self.enrollment,d)
 def test_missing_extra_null_profile_refused(self):
  for change in ('extra','missing','null'):
   d=copy.deepcopy(self.device)
   if change=='extra':d['provisional_path']['jitter_is_uncertainty']=73
   if change=='missing':del d['provisional_path']['evidence_sha256']
   if change=='null':d['provisional_path']=None
   with self.subTest(change=change),self.assertRaises(ValueError):validate_inputs(self.enrollment,d)

if __name__=='__main__':unittest.main()
