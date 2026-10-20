"""Explicit factory grants, profile exclusion and private configuration binding.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib,importlib.util,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('t096_builder',Path(__file__).parents[1]/'tools/build.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
class LoRaBuildBinding(unittest.TestCase):
    def setUp(self):
        self.registry={'devices':{'0000000000000001':{'target':'t096','alias':'DISPLAY ONLY'},'0000000000000002':{'target':'w12','alias':'TC-01'}}}
    def test_absent_is_disabled_in_every_profile(self):
        for profile in ('GM','RC','DC','PC'):
            self.assertEqual(b.lora_binding(None,profile,self.registry),'0000000000000000')
    def test_exact_factory_ignores_alias(self):
        for profile in ('GM','RC'):
            self.assertEqual(b.lora_binding('0000000000000001',profile,self.registry),'0000000000000001')
        with self.assertRaises(ValueError):b.lora_binding('0000000000000002','GM',self.registry)
    def test_missing_malformed_and_zero_fail_closed(self):
        for value in ('0000000000000000','0000000000000003','1','00000000000000AF','TC-01'):
            with self.assertRaises(ValueError):b.lora_binding(value,'GM',self.registry)
    def test_dc_pc_reject_binding(self):
        for profile in ('DC','PC'):
            with self.assertRaises(ValueError):b.lora_binding('0000000000000001',profile,self.registry)
    def test_binding_changes_private_watchdog_identity(self):
        enrolled='0'*64
        self.assertEqual(b.private_binding(enrolled,'0000000000000000'),enrolled)
        expected=hashlib.sha256((enrolled+':lora-tx:0000000000000001').encode()).hexdigest()
        self.assertEqual(b.private_binding(enrolled,'0000000000000001'),expected)
        self.assertNotEqual(expected,b.private_binding(enrolled,'0000000000000002'))
if __name__=='__main__':unittest.main()
