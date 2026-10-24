#!/usr/bin/env python3
"""Inspect a local UI preparation image; never opens USB.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json
from pathlib import Path
from build import inspect
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);a=p.parse_args()
print(json.dumps(inspect(a.build.resolve()),indent=2))
