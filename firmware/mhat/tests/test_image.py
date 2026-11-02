"""Synthetic image-layout checks; no USB, RF or clock operations.
Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec=importlib.util.spec_from_file_location('mhat_build',Path(__file__).parents[1]/'tools/build.py')
build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
class ImageTests(unittest.TestCase):
 def image(self):
  raw=bytearray(1024);struct.pack_into('<II',raw,0,0x20010000,0x30401);return raw
 def test_module_bounds_platform_and_crc(self):
  data=build.pack_image(self.image())
  start,end,_,_,_,platform,kind,index,_=struct.unpack_from('<IIBBHHBB8s',data,0x200)
  self.assertEqual((start,end,platform,kind,index),(0x30000,0x30400,25,3,0))
  self.assertEqual(zlib.crc32(data[:-4]),int.from_bytes(data[-4:],'big'))
 def test_refuses_header_overlap(self):
  raw=self.image();raw[0x200]=1
  with self.assertRaisesRegex(ValueError,'reservation'):build.pack_image(raw)
 def test_refuses_bad_vector_and_partition_overflow(self):
  raw=self.image();struct.pack_into('<I',raw,4,0xf4001)
  with self.assertRaisesRegex(ValueError,'vector'):build.pack_image(raw)
  raw=self.image()+bytearray(0xa4000)
  with self.assertRaisesRegex(ValueError,'partition'):build.pack_image(raw)
if __name__=='__main__':unittest.main()
