#!/usr/bin/env python3
"""Shared map and no-GPS N30 inputs; synthetic identities only."""
import json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'firmware/n30/tools'))
from onboard import prepare,onboard
from build import validate_inputs,device_header,validate_transmitter,lora_config_header
class Inputs(unittest.TestCase):
 def setUp(self):self.data={'schema':1,'devices':{'0000000000000001':{'alias':'Existing','target':'t096'}}}
 def test_both_names_and_identity(self):
  data,d1=prepare(self.data,'N3-1','02:00:00:00:30:01');data,d2=prepare(data,'N3-2','02:00:00:00:30:02')
  self.assertEqual(d1['device_id'],'0000020000003001');self.assertEqual(data['devices']['0000000000000001'],self.data['devices']['0000000000000001'])
  e={'group_id':'0123456789abcdef','key_epoch':1,'psk':bytes(range(32)).hex()}
  for factory in ('0000000000000000','0000000000000001'):validate_transmitter(factory,d1,data)
  for factory in ('bad','0000000000000002',d1['device_id']):
   with self.assertRaises(ValueError):validate_transmitter(factory,d1,data)
  self.assertIn('PTOB_N30_LORA_TX_FACTORY UINT64_C(0x0000000000000001)',lora_config_header(d1,e,'0000000000000001'))
  validate_inputs(e,d1);validate_inputs(e,d2);self.assertIn('PTOB_GNSS_INDICATORS 0',device_header(d1))
  for bad in ({**d1,'gnss_profile':'any'},{**d1,'board':'meshnology_w12_esp32s3'},{**d1,'label':'GM'},{**d1,'display_rotation':'guess'},{**d1,'device_id':d2['device_id']}):
   with self.assertRaises(ValueError):validate_inputs(e,bad)
  for mac in ('00:00:00:00:00:00','01:00:00:00:00:01','02:00:00:00:30:AA','not-a-mac'):
   with self.assertRaises(ValueError):prepare(data,'N3-1',mac)
  with self.assertRaises(ValueError):prepare(data,'N3-1','02:00:00:00:30:03')
  with self.assertRaises(ValueError):prepare(data,'N3-2',d1['factory_mac'])
 def test_actual_map_transaction(self):
  with tempfile.TemporaryDirectory(dir=ROOT/'artifacts',prefix='n30-test-') as name:
   p=Path(name);registry=p/'registry.json';registry.write_text(json.dumps(self.data))
   r=onboard(registry,p/'one','N3-1','02:00:00:00:30:01');self.assertFalse(r['hardware_access'])
   onboard(registry,p/'two','N3-2','02:00:00:00:30:02');before=registry.read_bytes()
   with self.assertRaises(ValueError):onboard(registry,p/'bad','N3-1','02:00:00:00:30:03')
   self.assertEqual(registry.read_bytes(),before)
   self.assertEqual(set(json.loads(before)['devices']),{'0000000000000001','0000020000003001','0000020000003002'})
if __name__=='__main__':unittest.main()
