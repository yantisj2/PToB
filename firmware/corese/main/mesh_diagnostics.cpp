// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "mesh_diagnostics.hpp"
#include <cstdio>
namespace corese::mesh {
bool live(const Roles& r,uint64_t now){return r.known&&now>=r.received_us&&now<r.until_us;}
bool current(const Path& p,uint64_t now){return p.known&&p.authenticated&&p.count>=1&&p.count<=3&&
 now>=p.sampled_us&&now-p.sampled_us<250000&&now<p.until_us;}
bool current(const Link& l,const Scope& s,uint64_t now){return l.known&&s.root&&s.boot&&s.epoch&&
 l.scope.root==s.root&&l.scope.boot==s.boot&&l.scope.epoch==s.epoch&&
 now>=l.sampled_us&&now-l.sampled_us<250000&&now<l.until_us;}
bool current(const Capabilities& c,uint64_t peer,uint64_t boot,uint64_t now){return c.known&&peer&&boot&&
 c.peer==peer&&c.boot==boot&&now>=c.sampled_us&&now<c.until_us;}
bool current(const Reference& r,const Scope& s,uint64_t now){return r.reported&&s.root&&s.boot&&s.epoch&&
 r.scope.root==s.root&&r.scope.boot==s.boot&&r.scope.epoch==s.epoch&&now>=r.received_us&&now<r.until_us;}
void reference_hdop(char* out,size_t size,uint16_t centi,bool known){
 if(!known||centi==UINT16_MAX){std::snprintf(out,size,"--");return;}
 std::snprintf(out,size,"%u.%02u",unsigned(centi/100),unsigned(centi%100));
}
void reference_ptp(char* out,size_t size,const Reference& r,bool visible){
 if(!visible||!(r.fields&Ptp)){std::snprintf(out,size,"C-- ACC -- VAR -- P1 -- P2 --");return;}
 char accuracy[8],variance[8];
 if(r.accuracy==254)std::snprintf(accuracy,sizeof accuracy,"UNKNOWN");else std::snprintf(accuracy,sizeof accuracy,"%02X",unsigned(r.accuracy));
 if(r.variance==UINT16_MAX)std::snprintf(variance,sizeof variance,"NONE");else std::snprintf(variance,sizeof variance,"%04X",unsigned(r.variance));
 std::snprintf(out,size,"C%u ACC %s VAR %s P1 %u P2 %u",unsigned(r.clock_class),accuracy,variance,unsigned(r.priority1),unsigned(r.priority2));
}
Roles RoleMonitor::snapshot(uint64_t)const {return state_;}
Reference ReferenceMonitor::snapshot(uint64_t)const {return state_;}
}
