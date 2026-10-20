#!/usr/bin/env python3
"""Acquire the same pinned public SDK as W12. No hardware."""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).resolve().parents[2]/'w12/tools/dependencies.py'),run_name='__main__')
