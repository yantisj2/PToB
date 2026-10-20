#!/usr/bin/env python3
"""N30 ten-row CRC console parser. Import opens no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[2]/'w12/tools/framed_console.py'
spec=importlib.util.spec_from_file_location('n30_w12_console',p);base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
fields=base.fields;fresh_nonce=base.fresh_nonce;status_command=base.status_command
class Parser(base.Parser):
 def __init__(self,nonce,manifest,session=None,capture_base=None):
  super().__init__(nonce,manifest,session,capture_base);base.require(manifest.get('r2_profile')=='r2-g1','N30 G1 only')
  self.expected_rows=(*self.expected_rows,'N30_UI')
 def feed(self,data):
  frames=super().feed(data)
  for f in frames:
   mesh=f['rows']['W12_R2_STATUS'];ui=f['rows']['N30_UI'];status=f['rows']['W12_V12_STATUS'];m=self.manifest
   # The inherited whole-frame/nonce/CRC/boot/source/deadline checks still run.
   # Its historical nine-row G1 branch does not apply to this ten-row target;
   # validate the exact N30 G1 identity/capability separately here.
   for key,value in [('profile','r2-g1'),('enrollment_sha',m['r2_enrollment_sha256']),('self','3'),('client_capabilities','0'),('client_control','0'),('source','0'),('serving','0'),('group_id',m['r2_group_id']),('key_epoch',str(m['r2_key_epoch'])),('client_identity',m['r2_client_identity'])]:base.require(mesh.get(key)==value,'N30 G1 identity/capability mismatch: '+key)
   base.validate_provisional_profile(mesh,m)
   base.require(ui.get('profile')=='RC' and ui.get('GPS')=='ABSENT' and ui.get('display')=='64x128' and ui.get('page') in ('0','1'),'N30 display/capability mismatch')
   base.require(status.get('GPS_inputs')=='0','N30 physical GNSS unexpectedly enabled')
  return frames

def mutation_command(frame,nonce,action):
 if action not in ('ui page 0','ui page 1'):return base.mutation_command(frame,nonce,action)
 base.require(base.HEX16.fullmatch(nonce) and int(nonce,16) and nonce!=frame['begin']['nonce'],'Fresh UI request nonce required')
 b=frame['begin'];return f"v12 do {b['ticket']} {nonce} {b['factory']} {b['session']} {action}\n".encode()
