#!/usr/bin/env python3
"""Offline effective-flag and assertion preservation checks; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import unittest
from build import validate_size_compilation


class SizePolicyTests(unittest.TestCase):
    def setUp(self):
        self.receipt = {'application_optimization': 'W12_MAIN_OS_SDK_DEBUG_V1'}
        self.config = '\n'.join(('CONFIG_COMPILER_OPTIMIZATION_DEBUG=y',
                                 'CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE=y',
                                 'CONFIG_COMPILER_OPTIMIZATION_ASSERTION_LEVEL=2'))
        self.rows = [{'file': '/app/native_main.cpp', 'command': 'cc -Og -Os -o esp-idf/main/CMakeFiles/__idf_main.dir/native_main.cpp.obj'},
                     {'file': '/sdk/controller.c', 'command': 'cc -Og -o esp-idf/bt/CMakeFiles/__idf_bt.dir/controller.c.obj'}]

    def check_policy(self):
        validate_size_compilation(self.receipt, self.rows, self.config)

    def test_sdk_debug_and_final_app_size_option(self):
        self.check_policy()

    def test_gnss_size_policy_preserves_sdk(self):
        self.receipt['application_optimization'] = 'W12_MAIN_OZ_GNSS_SDK_DEBUG_V1'
        self.rows[0]['command'] = self.rows[0]['command'].replace('-Os', '-Oz')
        self.check_policy()
        self.rows[0]['command'] += ' -Os'
        with self.assertRaisesRegex(ValueError, 'effective -Oz'):
            self.check_policy()

    def test_later_debug_option_is_refused(self):
        self.rows[0]['command'] += ' -Og'
        with self.assertRaisesRegex(ValueError, 'effective -Os'):
            self.check_policy()

    def test_every_main_source_must_use_size_option(self):
        self.rows.append({'file': '/radio/Module.cpp', 'command': 'cc -Og -o esp-idf/main/CMakeFiles/__idf_main.dir/Module.cpp.obj'})
        with self.assertRaisesRegex(ValueError, 'effective -Os'):
            self.check_policy()

    def test_sdk_only_commands_are_refused(self):
        self.rows = self.rows[1:]
        with self.assertRaisesRegex(ValueError, 'compile commands missing'):
            self.check_policy()

    def test_missing_policy_is_refused(self):
        self.receipt.clear()
        with self.assertRaisesRegex(ValueError, 'policy missing'):
            self.check_policy()

    def test_disabled_assertions_are_refused(self):
        self.config = self.config.replace('ASSERTIONS_ENABLE=y', 'ASSERTIONS_DISABLE=y')
        with self.assertRaisesRegex(ValueError, 'assertion policy changed'):
            self.check_policy()

    def test_sdk_optimization_change_is_refused(self):
        self.config = self.config.replace('OPTIMIZATION_DEBUG=y', 'OPTIMIZATION_SIZE=y')
        with self.assertRaisesRegex(ValueError, 'optimization/assertion policy changed'):
            self.check_policy()

    def test_quoted_output_paths(self):
        self.rows[0]['command'] = 'cc -Og -Os -o "esp-idf/main/CMakeFiles/__idf_main.dir/folder with spaces/native_main.cpp.obj"'
        self.check_policy()


if __name__ == '__main__':
    unittest.main()
