// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "rc9_time.hpp"
#include <cassert>
int main(){
 v12::Rc9Status s;uint64_t utc=0;constexpr uint64_t now=1000000;
 s.sampled_us=now;s.utc_us=1700000000000000;s.until_us=now+12000000;
 s.server=1;s.server_boot=2;s.root=3;s.source_epoch=4;s.sequence=5;s.valid=s.following=true;
 assert(w12::rc9_time(s,now,utc)&&utc==s.utc_us);
 assert(w12::rc9_time(s,now+1,utc)&&utc==s.utc_us+1);
 const auto live=s;
 for(unsigned fail=0;fail<9;++fail){s=live;
  if(fail==0)s.following=false;
  if(fail==1)s.valid=false;
  if(fail==2)s.fault=true;
  if(fail==3)s.server_boot=0;
  if(fail==4)s.sequence=0;
  if(fail==5)s.until_us=now;
  if(fail==6)s.sampled_us=now+1;
  if(fail==7)s.sampled_us=now-w12::SyncSnapshotMaxUs;
  if(fail==8)s.utc_us=UINT64_MAX;
  assert(!w12::rc9_time(s,fail==8?now+1:now,utc)&&utc==0);
 }
 // Companion origin boots may be unknown; this is functional UTC only.
 s=live;s.root_boot=s.emitter_boot=0;s.authority=0;assert(w12::rc9_time(s,now,utc));
}
