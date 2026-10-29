"""Validate public TZ modes before any image generation. MIT, Jonathan Yantis 2026."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('t096_tz_build',Path(__file__).resolve().parents[1]/'tools/build.py')
build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)

class TimeZoneBuild(unittest.TestCase):
    def test_modes(self):
        self.assertEqual(build.time_zone_config(None,None,None,True)['mode'],'auto')
        self.assertEqual(build.time_zone_config('manual-zone',2,None,True)['mode_number'],1)
        self.assertEqual(build.time_zone_config('manual-offset',2,-360,True)['offset_min'],-360)
        self.assertIsNone(build.time_zone_config(None,None,None,False))

    def test_invalid_and_separate_profiles(self):
        for mode,zone,offset in [('auto',2,None),('manual-zone',65000,None),('manual-zone',2,-300),
                                 ('manual-offset',2,None),('manual-offset',1,60),('manual-offset',2,841),
                                 ('manual-offset',0,0),('manual-offset',65536,0)]:
            with self.subTest(mode=mode,zone=zone,offset=offset):
                with self.assertRaises(ValueError):build.time_zone_config(mode,zone,offset,True)
        for mode in ('auto','manual-zone','manual-offset'):
            with self.assertRaises(ValueError):build.time_zone_config(mode,2,-300,False)

if __name__=='__main__':unittest.main()
