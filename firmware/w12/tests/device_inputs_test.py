#!/usr/bin/env python3
"""Synthetic local-input validation; no enrollment files or hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build import GNSS_PROFILE,device_header,validate_inputs

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

if __name__=='__main__':unittest.main()
