"""A receiver gate must not redirect W12's later inspector import to N30.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import importlib.util,inspect,json,sys,tempfile
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(APP/'tools'))
import build
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)/'receipt.json';p.write_text(json.dumps({'status':'FAIL'}))
 before=list(sys.path)
 try:build.check_receiver_state_gate(p)
 except ValueError:pass
 else:raise AssertionError('Failed gate accepted')
 assert sys.path==before,'Gate changed subsequent module resolution'
 import inspect_build
 assert Path(inspect_build.__file__).resolve()==APP/'tools/inspect_build.py'
 assert 'allow_development' in inspect.signature(inspect_build.inspect).parameters
print('PASS receiver gate preserves W12 inspector identity and API; no hardware')
