// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "mesh_owner.hpp"
extern "C" {
#include "../../t096/protocol/role_announcement.h"
#include "../../t096/protocol/gnss_diagnostics.h"
}
#include "n30_timing_probe.hpp"
#include "../../ss3/main/td_publication.hpp"
#include "esp_heap_caps.h"
#include <new>
#include <cstring>
namespace corese::owner {
namespace {
mesh::RoleMonitor monitor;mesh::ReferenceMonitor reference;Observation current;Observation* slots=nullptr;v12::TdPublication publication;
uint64_t last_publication=0;
void publish(uint64_t now,bool force=false){if(!slots||(!force&&now>=last_publication&&now-last_publication<50000))return;const int slot=publication.reserve();if(slot<0)return;current.roles=monitor.snapshot(now);current.reference=reference.snapshot(now);slots[slot]=current;publication.commit(slot);last_publication=now;}
}
bool init(){slots=static_cast<Observation*>(heap_caps_calloc(3,sizeof(Observation),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!slots)return false;for(unsigned i=0;i<3;++i)new(slots+i)Observation;return true;}
void advertisement(const uint8_t* data,unsigned size,uint8_t sid,const uint8_t address[6],mesh::Scope scope,const r2_group_config& group,uint64_t at,uint64_t now){
 if(sid!=12||!slots)return;
 for(unsigned off=0;off<size;){const unsigned n=data[off];if(!n||off+n>=size)return;
  const auto* w=data+off;
  const bool gs=n+1==R2_ROLE_AD_BYTES&&w[1]==0xff&&w[4]=='G'&&w[5]=='S';
  const bool gd=(n+1==R2_GNSS_AD_BYTES||n+1==R2_GNSS_LEGACY_AD_BYTES)&&w[1]==0xff&&w[4]=='G'&&w[5]=='D';
  if(gs||gd){
   const uint64_t id=r2_group_get(w+20,8);uint8_t key[16];
   r2_group_pair_key(key,&group,id,id);key[5]|=0xc0;
   if(std::memcmp(key,address,6)){off+=n+1;continue;}
   if(gs)monitor.receive(w,R2_ROLE_AD_BYTES,group,id,scope,at,now);
   else reference.receive(w,n+1,group,id,scope,at,now);
   publish(now,true);
  }off+=n+1;
 }
}
void source_lost(uint64_t now){
 // Published diagnostics retain explicit historical reports and their original
 // expiry. Current source-bound capability, residual and scan state is retired.
 current.cap={};current.offset_scope={};current.offset=0;current.offset_at=0;
 current.offset_valid=current.scan_allowed=false;current.scan_deadline=0;publish(now,true);
}
void capabilities(uint64_t peer,uint64_t boot,uint8_t flags,uint64_t at,uint64_t until){current.cap={peer,boot,at,until,flags,true};publish(at,true);}
void scan_state(bool allowed,uint64_t deadline,uint32_t windows,uint64_t now){
 if(current.scan_allowed==allowed&&current.scan_deadline==deadline&&current.scan_windows==windows)return;
 current.scan_allowed=allowed;current.scan_deadline=deadline;current.scan_windows=windows;publish(now,true);
}
void residual(const v12::n30_probe::Pair& p,uint64_t now){
 current.offset_valid=p.causal&&p.root&&p.boot&&p.source&&p.predicted_ns>0&&p.remote_ns<=uint64_t(INT64_MAX);
 current.offset_scope={p.root,p.boot,p.source};current.offset_at=now;
 current.offset=current.offset_valid?p.predicted_ns-int64_t(p.remote_ns):0;publish(now);
}
bool snapshot(Observation& out,uint64_t){if(!slots)return false;const int slot=publication.pin();if(slot<0)return false;out=slots[slot];publication.release(slot);return true;}
}
