// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Shared native watch-face presentations; no timing or radio authority.
void alias(char* out,size_t n,const char* name,size_t capacity){
 // A right arrow is visible in the incumbent bitmap font. It marks shortening.
 const auto len=std::strlen(name);
 std::snprintf(out,n,"%.*s%s",int(len>capacity?capacity-1:capacity),name,len>capacity?">":"");
}
void timer(char* out,size_t n,uint64_t seconds,bool known){
 if(!known){std::snprintf(out,n,"--:--:--");return;}
 if(seconds>=86400){duration_text(out,n,seconds);return;}
 std::snprintf(out,n,"%02u:%02u:%02u",unsigned(seconds/3600),unsigned(seconds/60%60),unsigned(seconds%60));
}
void reference_watch(Canvas& c,const Model& m,const View& v,uint64_t now){
 shell(c,m,"REFERENCE WATCH",now,"03 / GNSS + STANDBY");
 uint64_t utc=0;const bool live=time(m,now,utc)&&m.source!=Source::Retained;
 const auto& q=m.mesh.reference;const auto& r=m.mesh.roles;
 const bool same=q.reported&&q.scope.root==m.root&&q.scope.boot==m.boot&&q.scope.epoch==m.source_epoch;
 const bool qlive=fresh(m,now)&&mesh::current(q,{m.root,m.boot,m.source_epoch},now);
 const bool sv=same&&(q.fields&mesh::Satellites)&&q.sats!=UINT8_MAX;
 c.well(12,42,296,48);
 int64_t shown=0;uint64_t last_utc=0;c.text(20,49,!live&&readout_time(m,now,last_utc)?"LAST":clock_zone(m,now,shown),Dim);
 char t[12];clock_string(m,now,t);c.digital(55,51,t,28,31,live?Green:Amber);
 c.well(12,98,142,65);c.well(162,98,146,65);
 char gnss_alias[25];alias(gnss_alias,sizeof gnss_alias,same?q.root_name:"--",8);
 c.text(20,104,same&&!qlive?"SV / LAST":"SV USED",Dim);c.text(98,104,gnss_alias,qlive?Green:Amber,1,148);
 char s[64];if(sv)std::snprintf(s,sizeof s,"%02u",unsigned(q.sats));else std::snprintf(s,sizeof s,"--");
 c.digital(22,116,s,23,29,sv&&qlive?(q.sats<10?rgb(255,160,64):Green):Amber);
 const char* labels[]={"FIX","PPS","QUAL"};const uint8_t bits[]={mesh::Fix,mesh::Pps,mesh::Qualified};
 for(unsigned i=0;i<3;++i){
  const bool known=same&&(q.fields&bits[i]),yes=known&&(q.values&bits[i]);
  c.text(83,117+int(i)*10,labels[i],Dim);
  c.text(119,117+int(i)*10,known?(yes?"YES":"NO"):"--",!qlive||!known?Amber:yes?Green:Red,1,150);
 }
 char observed[24];const bool obs=qlive&&(q.fields&(mesh::Fix|mesh::Pps))==(mesh::Fix|mesh::Pps)&&
  (q.values&(mesh::Fix|mesh::Pps))==(mesh::Fix|mesh::Pps)&&mesh::current(m.mesh.reference_observation,{m.root,m.boot,m.source_epoch},now);
 timer(observed,sizeof observed,(now-m.mesh.reference_observation.since_us)/1000000,obs);
 std::snprintf(s,sizeof s,"OBS %s",observed);c.text(20,152,s,obs?Green:Amber,1,149);
 const bool role_scope=r.root==m.root&&r.root_boot==m.boot&&r.epoch==m.source_epoch;
 const bool empty=!m.root&&!m.boot&&!m.source_epoch;
 const bool nominated=fresh(m,now)&&role_scope&&mesh::hot_nomination_current(r,now);
 const bool hot=r.known&&r.hot&&(role_scope||empty);
 c.text(170,104,nominated?"HOT NOMINEE":hot?"HOT / LAST SEEN":"HOT NOMINEE",Dim);
 char hot_alias[25];alias(hot_alias,sizeof hot_alias,hot?r.hot_name:r.known?"NONE":"--",22);
 c.text(170,117,hot_alias,nominated?Green:Amber,std::strlen(hot_alias)>11?1:2,302);
 const bool self=hot&&r.hot_report_known&&now>=r.hot_received_us;
 const bool current=self&&now<r.hot_until_us;
 const char* role=self?(r.hot_role==3?"HOT":r.hot_role==2?"PREP":"OTHER"):hot?"WAIT":"--";
 std::snprintf(s,sizeof s,"SELF %s%s",role,current?"":self?" / OLD":"");c.text(170,138,s,current?Green:Amber,1,301);
 char unchanged[24];timer(unchanged,sizeof unchanged,now>=r.hot_since_us?(now-r.hot_since_us)/1000000:0,hot&&now>=r.hot_since_us);
 std::snprintf(s,sizeof s,"SAME %s",unchanged);c.text(170,152,s,nominated?Green:Amber,1,301);
 char qa[16]="--",ha[16]="--";
 if(same&&now>=q.received_us)std::snprintf(qa,sizeof qa,"%llus",(unsigned long long)((now-q.received_us)/1000000));
 if(self)std::snprintf(ha,sizeof ha,"%llus",(unsigned long long)((now-r.hot_received_us)/1000000));
 std::snprintf(s,sizeof s,"GNSS AGE %s / HOT SELF AGE %s",qa,ha);c.center(12,169,296,s,Ink);
 navigation(c,v);
}
void epoch_counter(Canvas& c,const Model& m,const View& v,uint64_t now){
 shell(c,m,"EPOCH COUNTER",now,"04 / FRACTIONAL TIME");
 uint64_t utc=0;const bool valid=time(m,now,utc),live=valid&&m.source!=Source::Retained;
 c.well(12,42,296,65);int64_t shown=0;const auto* zone=clock_zone(m,now,shown);
 uint64_t last_utc=0;c.text(20,48,!live&&readout_time(m,now,last_utc)?"LAST":zone,Dim);c.text(262,48,"ms",Dim);
 char t[12];clock_string(m,now,t);c.digital(22,56,t,23,29,live?Green:Amber);
 c.fill(204,81,3,3,live?Green:Amber);
 char ms[4]="---";
 // Fractional display follows the exact same permission as the integer clock.
 if(live)std::snprintf(ms,sizeof ms,"%03u",unsigned(utc%1000000/1000));
 c.digital(217,62,ms,20,23,live?Green:Amber);
 // Modeled millisecond phase vernier. No physical PPS/display edge claim.
 c.line(22,93,298,93,Rim);
 for(unsigned i=0;i<=20;++i){const int x=22+int(i)*276/20;c.line(x,91,x,i%5?94:96,Dim);}
 c.text(21,99,"0",Dim);c.text(81,99,"250",Dim);c.text(150,99,"500",Dim);c.text(219,99,"750",Dim);c.text(280,99,"ms",Dim);
 if(live){const int x=22+int((utc%1000000)*276/1000000);c.line(x-3,87,x+3,87,Green);c.line(x-2,88,x+2,88,Green);c.pixel(x,90,Green);}
 c.well(12,115,142,48);c.well(162,115,146,48);
 bool hold_visible=false;const auto& h=shadow_readout(m,now,hold_visible);
 const bool hold_live=shadow_current(m,now)&&h.ready;
 c.text(20,121,!hold_live&&hold_visible?"LAST P99 HOLD":h.lower_bound?"P99 <1us HOLD MIN":"P99 <1us HOLD",Dim);
 char hold[20];hold_duration(hold,sizeof hold,h,hold_visible);
 c.digital(22,134,hold,16,22,hold_live?Green:Amber);c.text(137,149,"s",hold_live?Green:Amber);
 const auto& r=m.mesh.roles;
 const bool same=r.root==m.root&&r.root_boot==m.boot&&r.epoch==m.source_epoch;
 uint64_t age=0;const bool age_known=live&&same&&r.wire_version>=3&&mesh::epoch_elapsed(r,utc,age);
 c.text(170,121,"MESH AGE / h:m:s",Dim);
 char a[24]="--:--:--";
 if(age_known){const auto s=age/1000000;
  if(s<100*3600)std::snprintf(a,sizeof a,"%02u:%02u:%02u",unsigned(s/3600),unsigned(s/60%60),unsigned(s%60));
  else duration_text(a,sizeof a,s);
 }
 if(!age_known||age/1000000<100*3600)c.digital(180,134,a,12,22,age_known?Green:Amber);
 else {c.fill(170,120,132,9,Well);c.text(170,121,"MESH AGE / d h m",Dim);c.center(162,140,146,a,Green);}
 char s[96],birth[32],founder_alias[25];mesh::epoch_start_text(birth,sizeof birth,same&&r.wire_version>=3?r.birth_utc_us:0);
 alias(founder_alias,sizeof founder_alias,same&&r.birth_utc_us?r.founder_name:"--",18);
 std::snprintf(s,sizeof s,"EPOCH %s%s / F:%s",birth,std::strcmp(birth,"--")?"Z":"",founder_alias);
 c.center(12,169,296,s,Ink);
 navigation(c,v);
}
