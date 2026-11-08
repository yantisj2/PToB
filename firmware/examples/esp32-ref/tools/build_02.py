#!/usr/bin/env python3
"""Build and inspect ESP32-REF 02 using the collection's native builder."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
from build_01 import REPO, main, run_builder

if __name__ == '__main__':
    run_builder(lambda: main(example=2), REPO)
