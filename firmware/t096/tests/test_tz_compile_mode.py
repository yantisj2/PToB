"""Native build gate for shared application/controller wire layout.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from tz_compile_mode import require_tz_compile_mode


class CompileModeTests(unittest.TestCase):
    def rows(self):
        return [{'file': '/source/' + leaf,
                 'command': 'cc -DR2_GM7_NATIVE=1 -DR2_TZ_WIRE=1 -c source.c'}
                for leaf in ('gm7_controller.c', 'gm7_owner.c', 'gm7_codec.c',
                             'gm7_service.c', 'rc_dynamic_service.c', 'lll_scan.c')]

    def test_consistent(self):
        self.assertEqual(require_tz_compile_mode(self.rows()), 6)

    def test_controller_missing_or_wrong(self):
        for mode in ('', '-DR2_TZ_WIRE=0', '-DR2_TZ_WIRE',
                     '-DR2_TZ_WIRE=1 -DR2_TZ_WIRE=0'):
            rows = self.rows()
            rows[0]['command'] = 'cc -DR2_GM7_NATIVE=1 ' + mode
            with self.assertRaisesRegex(ValueError, 'Mixed coarse-wire ABI'):
                require_tz_compile_mode(rows)

    def test_generated_controller_missing(self):
        rows = self.rows()
        rows[-1]['command'] = 'cc -DR2_GM7_NATIVE=1'
        with self.assertRaises(ValueError):
            require_tz_compile_mode(rows)

    def test_required_unit_missing_or_duplicate(self):
        for rows in (self.rows()[1:], self.rows() + self.rows()[:1]):
            with self.assertRaises(ValueError):
                require_tz_compile_mode(rows)

    def test_arguments_form(self):
        rows = self.rows()
        for row in rows:
            row['arguments'] = row.pop('command').split()
        self.assertEqual(require_tz_compile_mode(rows), 6)


if __name__ == '__main__':
    unittest.main()
