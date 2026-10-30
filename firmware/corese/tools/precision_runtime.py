# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Scoped shadow servo and current coarse TZ overlay on the pinned receiver."""
import hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def precision(root):
 root=Path(root);manifest=json.loads((APP/'tools/precision_sources.json').read_text())
 for name,record in manifest['files'].items():
  data=subprocess.check_output(['git','-C',str(APP.parents[1]),'show',manifest['commit']+':'+name])
  if hashlib.sha256(data).hexdigest()!=record['sha256']:raise ValueError('Selected precision dependency changed: '+name)
  if name.endswith('w12_holdover_rate.hpp'):
   data=data.replace(b'value-clock.receiver_phase_ns',b'value').replace(b'clock.thermal={};',b'')
  if name.endswith('w12_holdover_horizon.hpp'):
   data=data.replace(b'clock.thermal.beta!=0||',b'').replace(b'clock.relative_at(at)-clock.receiver_phase_ns',b'clock.relative_at(at)')
  target=root/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
 def edit(name,old,new,count=1):
  p=root/'firmware/w12/main'/name;s=p.read_text()
  if s.count(old)!=count:raise ValueError('CoreSE precision anchor changed: '+old[:100])
  p.write_text(s.replace(old,new))
 edit('native_r2.hpp','#include <cstdint>','#include <cstdint>\n#include "holdover_view.hpp"\n#include "r2/r2-g1/time_zone.h"')
 edit('native_r2.hpp','struct R2Status {','struct R2Status {\n  corese::HoldoverView corese_holdover{};\n  r2_tz tz{};uint64_t tz_until_us=0;uint16_t tz_received=0,tz_conflicts=0;')
 edit('g1_corrected_clock.hpp','#include "g1_clock_prediction.hpp"','#include "g1_clock_prediction.hpp"\n#include "shadow_holdover.hpp"')
 edit('g1_corrected_clock.hpp','    quality_add(o.local,causal,causal_available);','    if(corese::shadow::pair_sink)corese::shadow::pair_sink(o.local,remote,available,o.channel,quality_generation_);\n    quality_add(o.local,causal,causal_available);')
 edit('native_g1.cpp','#include "g1_corrected_clock.hpp"','#include "g1_corrected_clock.hpp"\n#include "g1_time_zone.hpp"\n#include "mesh_discovery.hpp"\n#include "gm7_codec.h"\n#include "esp_heap_caps.h"\n#include <new>')
 # Coarse TZ changes RC discovery/control sizes; precision packet sizes stay
 # unchanged. Use the selected codec constants on every admission path.
 edit('native_g1.cpp','row.length!=212','row.length!=RC_DYNAMIC_CONTROL_BYTES')
 edit('native_g1.cpp','uint8_t wire[212]','uint8_t wire[RC_DYNAMIC_CONTROL_BYTES]')
 edit('native_g1.cpp','if(n==145&&','if(n==RC_DYNAMIC_CAPABILITY_BYTES+1&&')
 edit('native_g1.cpp','d.bytes+at+2,144,','d.bytes+at+2,RC_DYNAMIC_CAPABILITY_BYTES,')
 edit('native_r2.hpp','AdvStart,Connect};','AdvStart,Connect,Address};')
 edit('native_g1.cpp','#include "r2_bench_enrollment.h"','#include "r2_bench_enrollment.h"\n// Enrollment supplies trust, never a mesh-ID selection or allowlist.\nr2_group_config corese_group=[](){auto group=provision.group;group.id=0;return group;}();')
 # Route the whole receiver, including RC9, through the learned volatile
 # context. Keep the immutable enrollment only as the trusted key source.
 for name in ('native_g1.cpp','rc9_client_native.inc'):
  path=root/'firmware/w12/main'/name;s=path.read_text()
  if name=='native_g1.cpp':
   prefix,suffix=s.split('// Enrollment supplies trust, never a mesh-ID selection or allowlist.',1)
   line,suffix=suffix.split('\n',2)[1:]
   s=prefix+'// Enrollment supplies trust, never a mesh-ID selection or allowlist.\n'+line+'\n'+suffix.replace('provision.group','corese_group')
  else:s=s.replace('provision.group','corese_group')
  path.write_text(s)
 edit('native_g1.cpp','!r2_group_valid(&corese_group)','!r2_group_valid(&provision.group)')
 edit('native_g1.cpp','own_address=canonical(id);','own_address.type=BLE_ADDR_RANDOM;esp_fill_random(own_address.val,6);own_address.val[5]|=0xc0;')
 edit('native_g1.cpp','void observe_group(const Discovery& d){','#include "mesh_discovery.inc"\nvoid observe_group(const Discovery& d){\n corese_select_mesh(d);')
 edit('native_g1.cpp','G1CorrectedClock corrected_clock;','G1CorrectedClock corrected_clock;\ncorese::ShadowHoldover* corese_shadow=nullptr;\nr2_tz_lease coarse_tz{};G1TimeZoneOrder coarse_tz_order;\nvoid corese_pair(uint64_t local,uint64_t remote,uint64_t available,uint8_t channel,uint32_t generation){corese_shadow->pair(local,remote,available,channel,generation);}')
 edit('native_g1.cpp','void begin_close(uint64_t now){if(closing)return;closing=true;','void begin_close(uint64_t now){if(closing)return;closing=true;\n if(corese_shadow){corese_shadow->clear();}\n coarse_tz={};coarse_tz_order={};')
 edit('native_g1.cpp','void changed_scope(uint64_t now,G1Reason reason,bool preserve_clock,G1ParentReason parent_reason){','void changed_scope(uint64_t now,G1Reason reason,bool preserve_clock,G1ParentReason parent_reason){\n if(corese_shadow){corese_shadow->clear();}\n coarse_tz={};coarse_tz_order={};')
 edit('native_g1.cpp','joined_peer=c.sender;joined_boot=c.sender_boot;member=true;sync_address=peer.address;','joined_peer=c.sender;joined_boot=c.sender_boot;member=true;sync_address=peer.address;\n      (void)coarse_tz_order.observe(coarse_tz,c.tz,c.next_challenge,row.at,now,R2_TZ_RC_PERIOD_US,true);')
 edit('native_g1.cpp','member=true;joined_peer=r.sender;joined_boot=r.sender_boot;cooldown(r.sender,now,5000000);','member=true;joined_peer=r.sender;joined_boot=r.sender_boot;cooldown(r.sender,now,5000000);\n    (void)coarse_tz_order.bootstrap(coarse_tz,r.tz,row.at,now);')
 # Presence is authenticated and exact-peer bound; original receipt fixes expiry.
 edit('native_g1.cpp','    inc(relay_ok);\n#ifdef PTOB_N30','    inc(relay_ok);\n    if(row.identity==joined_peer)r2_tz_renew(&coarse_tz,row.at,R2_TZ_RC_PERIOD_US);\n#ifdef PTOB_N30')
 edit('native_g1.cpp','continue;}inc(p1_ok);','continue;}inc(p1_ok);\n    if(row.identity==joined_peer&&p.boot==joined_boot)r2_tz_renew(&coarse_tz,row.at,R2_TZ_GM_PERIOD_US);')
 edit('native_g1.cpp','continue;}inc(relay_ok);','continue;}inc(relay_ok);\n    if(row.identity==joined_peer&&p.route.boot==joined_boot)r2_tz_renew(&coarse_tz,row.at,R2_TZ_GM_PERIOD_US);')
 helper='''void corese_zone(const Discovery& d){
 const auto now=micros();
 if(rc_bound()&&d.sid==13&&(d.props&1)&&same_address(d.address,canonical(joined_peer))){
  for(unsigned at=0;at<d.length;){const unsigned n=d.bytes[at];if(!n||at+n>=d.length)break;
   if(n==RC_DYNAMIC_CAPABILITY_BYTES+1&&d.bytes[at+1]==0xff){rc7_control cap{};
    if(!rc7_capability_decode(&cap,d.bytes+at+2,RC_DYNAMIC_CAPABILITY_BYTES,&corese_group)&&cap.sender==joined_peer&&cap.sender_boot==joined_boot&&rc7_path_same(&cap.path,&rc_client.receiver.path))
     (void)coarse_tz_order.observe(coarse_tz,cap.tz,cap.next_challenge,d.at,now,R2_TZ_RC_PERIOD_US,true);
   }at+=n+1;
  }
 }
 if(d.sid!=14||rc_bound()||!anchor.valid(now))return;
 gm7_packet p{};if(gm7_ad_decode(&p,d.bytes,d.length,&corese_group)||p.kind!=GM7_ANNOUNCE)return;
 const auto& c=p.body.control;
 if(!same_address(d.address,canonical(c.sender))||c.sender!=joined_peer||c.boot!=joined_boot||c.sender!=c.active||c.boot!=c.active_boot||
    c.active!=anchor.root||c.active_boot!=anchor.boot||c.timing_epoch!=anchor.source_epoch||c.train_gen!=anchor.publication||
    now<anchor.at||now-anchor.at>UINT64_MAX-anchor.utc)return;
 const auto utc=anchor.utc+now-anchor.at,age=utc>=c.sent_utc_us?utc-c.sent_utc_us:c.sent_utc_us-utc;
 if(age<3*R2_TZ_GM_PERIOD_US)(void)coarse_tz_order.observe(coarse_tz,c.tz,c.sequence,d.at,now,R2_TZ_GM_PERIOD_US,false);
}
'''
 edit('native_g1.cpp','void observe_group(const Discovery& d){',helper+'void observe_group(const Discovery& d){\n corese_zone(d);')
 edit('native_g1.cpp','  portENTER_CRITICAL(&mux);\n  snap.mode=mode;', '''  corese_shadow->tick(corrected_clock.prediction(),tick,following_now&&corrected_valid&&corrected_clock.frequency_trained(tick)&&relative_quality.rms_ns<1000&&!fault&&!closing&&mode==R2Mode::Client,relative_quality.rate_guard_ppb);
  portENTER_CRITICAL(&mux);
  snap.corese_holdover=corese_shadow->view(tick);
  snap.tz=mode==R2Mode::Client&&!fault&&!closing?r2_tz_current(&coarse_tz,now):r2_tz_unknown();snap.tz_until_us=coarse_tz.until_us;
  snap.tz_received=coarse_tz.received;snap.tz_conflicts=coarse_tz.conflicts;
  snap.mode=mode;''')
 edit('native_g1.cpp','bool r2_init(uint64_t boot,const uint8_t factory[6]){','bool r2_init(uint64_t boot,const uint8_t factory[6]){\n if(!corese_shadow){void* memory=heap_caps_malloc(sizeof(corese::ShadowHoldover),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!memory)return false;corese_shadow=new(memory)corese::ShadowHoldover;}\n corese::shadow::pair_sink=corese_pair;')
 edit('native_g1.cpp','  portEXIT_CRITICAL(&mux);const auto now=micros();','''  portEXIT_CRITICAL(&mux);const auto now=micros();
  if(!result.requested||result.fault||now>=result.tz_until_us)result.tz=r2_tz_unknown();
  const auto elapsed=now>=result.follower_snapshot_us?now-result.follower_snapshot_us:UINT64_MAX;
  auto& h=result.corese_holdover;
  h.witness_age_us=elapsed<=UINT64_MAX-h.witness_age_us?h.witness_age_us+elapsed:UINT64_MAX;
  if(!result.requested||result.fault||h.witness_age_us>=5000000){h.ready=false;h.p95_ns=h.p99_ns=h.max_ns=UINT64_MAX;}
''')
 # CoreSE is a continuously listening diagnostic receiver. Give a previously
 # authenticated GM ten seconds of fresh discovery before relay fallback,
 # and three minutes to establish sustained precision instead of one.
 # These are selection preferences only; all source/model/admission deadlines
 # remain those of the pinned receiver. No other target source is changed.
 edit('g1_parent_selection.hpp','GmRecoveryUs=3000000','GmRecoveryUs=10000000')
 edit('g1_parent_selection.hpp','LockDwellUs=60000000','LockDwellUs=180000000')
 return manifest
