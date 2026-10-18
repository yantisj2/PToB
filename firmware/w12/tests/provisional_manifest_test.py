#!/usr/bin/env python3
"""Bounded status cannot hide or silently enable a provisional path profile.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import sys,unittest,copy
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from framed_console import validate_provisional_profile
from build import provisional_profile
class ProfileStatus(unittest.TestCase):
 def setUp(self):
  self.profile=provisional_profile({'device_id':'0000020000000005','provisional_path':{
   'schema':1,'scope':'AGGREGATE_PATH_RELATIVE_PPS','offset_ns':47000,'root':'0000020000000001',
   'root_boot':'0000000000000001','source_epoch':1,'receiver':'0000020000000002',
   'receiver_source_sha256':'1'*64,'receiver_phase_correction_ns':10543,
   'evidence_sha256':'2'*64,'baseline_evidence_sha256':'3'*64}})
  self.manifest={'provisional_path_profile':self.profile,'r2_client_identity':self.profile['publisher']}
 def test_legacy_and_zero_default(self):
  for status in ({},{'pcfg':'00000000'}):validate_provisional_profile(status,{})
 def test_matching_optin(self):
  validate_provisional_profile({'pcfg':self.profile['id'][:8]},self.manifest)
 def test_unreviewed_profile_refused(self):
  for value in (self.profile['id'][:8],'0/0','0123456789abcdef/47000'):
   with self.subTest(value=value),self.assertRaises(RuntimeError):validate_provisional_profile({'pcfg':value},{})
 def test_missing_wrong_prefix_refused(self):
  for value in (None,'00000000','x'):
   with self.subTest(value=value),self.assertRaises(RuntimeError):validate_provisional_profile({'pcfg':value},self.manifest)
 def test_changed_full_profile_refused_even_matching_status_prefix(self):
  for key,value in (('offset_ns',-47000),('evidence_sha256','4'*64),('publisher','0000020000000006'),('source_epoch',2),('allow_source_epoch_transfer',True)):
   manifest=copy.deepcopy(self.manifest);manifest['provisional_path_profile'][key]=value
   with self.subTest(key=key),self.assertRaises(RuntimeError):validate_provisional_profile({'pcfg':self.profile['id'][:8]},manifest)
 def test_malformed_manifest_refused(self):
  for profile in ({},[],False,{'id':1,'offset_ns':47000}):
   with self.subTest(profile=profile),self.assertRaises(RuntimeError):validate_provisional_profile({'pcfg':self.profile['id'][:8]},{'provisional_path_profile':profile})
if __name__=='__main__':unittest.main()
