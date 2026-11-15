#!/usr/bin/env python3
"""Watch Console V3 envelope; importing this module never accesses hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[2]/'w12/tools/framed_console.py'
spec=importlib.util.spec_from_file_location('watch_console_base',p)
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
fresh_nonce=base.fresh_nonce;status_command=base.status_command
class Parser(base.Parser):
 def __init__(self,nonce,manifest,session=None):
  super().__init__(nonce,manifest,session)
  base.require(manifest.get('r2_profile')=='r2-g1','Watch G1 contract required')
  self.expected_rows=(*self.expected_rows,'TWATCH_UI')
 def feed(self,data):
  frames=super().feed(data)
  for frame in frames:
   ui=frame['rows']['TWATCH_UI']
   for key,value in [('version','1'),('width','410'),('height','502'),('GNSS','OFF'),('precision','UNQUALIFIED')]:
    base.require(ui.get(key)==value,'Unexpected watch UI contract: '+key)
   base.require(ui.get('ready') in ('0','1'),'Missing display readiness')
  return frames

def mutation_command(frame,nonce,action):
 base.require(action in ('prepare_update','bootrom','ui page 0','ui page 1','ui page 2','ui page 3'),'Unsupported watch action')
 base.require(base.HEX16.fullmatch(nonce) and int(nonce,16) and nonce!=frame['begin']['nonce'],'Fresh nonce required')
 b=frame['begin']
 return f"v12 do {b['ticket']} {nonce} {b['factory']} {b['session']} {action}\n".encode()
