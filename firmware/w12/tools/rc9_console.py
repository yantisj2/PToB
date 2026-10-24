#!/usr/bin/env python3
"""Separate RC9 diagnostics under the unchanged nonce/CRC/original deadline."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import framed_console as base
class Parser(base.Parser):
 def __init__(self,nonce,manifest,session=None,capture_base=None):
  super().__init__(nonce,manifest,session,capture_base)
  self.expected_rows=('W12_R2_STATUS','W12_V12_STATUS','W12_RC9_STATUS','W12_RC9_ORIGIN','W12_RC9_QUALITY','W12_RC9_COUNTERS','W12_RC9_LEASE','W12_RC9_CONTRACT','W12_UI_CLOCK','W12_V12_COMMAND')
 def feed(self,data):
  frames=super().feed(data)
  for f in frames:
   rows=f['rows'];m=self.manifest;mesh=rows['W12_R2_STATUS']
   for key,value in [('profile','r2-g1'),('enrollment_sha',m['r2_enrollment_sha256']),('self','3'),('client_capabilities','0'),('client_control','0'),('source','0'),('serving','0'),('group_id',m['r2_group_id']),('key_epoch',str(m['r2_key_epoch'])),('client_identity',m['r2_client_identity'])]:base.require(mesh.get(key)==value,'RC9 identity/capability '+key)
   for key,value in dict(wire='1',source='RC8',hops='1',precision='UNKNOWN',BLE_training='NONE',GM='NONE',LoRa_TX='OFF').items():base.require(rows['W12_RC9_CONTRACT'].get(key)==value,'RC9 contract '+key)
   for row,keys in [('STATUS','sampled_us UTC_us until_us valid following fault'),('QUALITY','assurance authority metadata_known upstream_link_rms_ns'),('COUNTERS','sequence accepted rejected'),('LEASE','until_us clients serving radio_hold_ms')]:
    for key in keys.split():base.require(rows['W12_RC9_'+row].get(key,'').isdigit(),'RC9 numeric '+key)
   for key in ('server','server_boot','root','root_boot','emitter','emitter_boot'):base.require(base.HEX16.fullmatch(rows['W12_RC9_ORIGIN'].get(key,'')),'RC9 identity '+key)
   if self.manifest.get('version') in ('0.2.5','0.2.6','0.2.7','0.2.8'):base.require('service_flags' in rows['W12_RC9_LEASE'],'RC9 service flags required for version25/26')
   if 'service_flags' in rows['W12_RC9_LEASE']:base.require(rows['W12_RC9_LEASE']['service_flags'].isdigit() and int(rows['W12_RC9_LEASE']['service_flags'])<=15,'RC9 service flags')
   base.require(int(rows['W12_RC9_LEASE']['clients'])<=8,'Bounded RC9 children')
   ui=rows['W12_UI_CLOCK'];base.require(ui.get('phase')=='UTC_SECOND','Display scheduler contract')
   for key in ('page','valid','UTC','sampled_us','UTC_us','drawn_us','paint_us','next_us'):base.require(ui.get(key,'').isdigit(),'Clock numeric '+key)
  return frames
