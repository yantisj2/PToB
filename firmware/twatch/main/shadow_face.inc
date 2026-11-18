// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Included inside watch.cpp's renderer namespace; native RGB565 geometry only.
constexpr auto ShadowCase=rgb(13,19,23),ShadowInk=rgb(235,242,232),ShadowLime=rgb(211,242,107),ShadowMuted=rgb(155,175,181),ShadowRule=rgb(54,73,79),ShadowDim=rgb(30,43,47);
void shadow_value(char* out,unsigned size,int64_t tenth_ns,bool signed_value){
 const auto magnitude=uint64_t(tenth_ns<0?-tenth_ns:tenth_ns);const char* sign=signed_value?(tenth_ns<0?"-":"+"):"";
 if(magnitude<10000)std::snprintf(out,size,"%s%llu.%llu",sign,(unsigned long long)(magnitude/10),(unsigned long long)(magnitude%10));
 else if(magnitude<10000000)std::snprintf(out,size,"%s%llu.%02llu",sign,(unsigned long long)(magnitude/10000),(unsigned long long)((magnitude%10000)/100));
 else std::snprintf(out,size,"%s%llu.%02llu",sign,(unsigned long long)(magnitude/10000000),(unsigned long long)((magnitude%10000000)/100000));
}
const char* shadow_unit(int64_t tenth_ns){const auto n=tenth_ns<0?-tenth_ns:tenth_ns;return n<10000?"ns":n<10000000?"us":"ms";}
void shadow_face(Canvas& c,const Model& m,uint64_t now){
 const auto& s=m.shadow;char b[64];
 c.rect(0,0,Width,Height,Black);
 for(int y=20;y<482;++y){const int inset=std::max(0,44-std::min(y-20,481-y));c.rect(18+inset,y,374-2*inset,1,ShadowCase);}
 c.frame(18,20,374,462,44,ShadowRule);c.frame(23,25,364,452,42,ShadowMuted);
 c.rect(75,31,48,3,ShadowLime);c.rect(287,31,48,3,ShadowLime);
 centered(c,205,34,"PToB / SHADOW",26,ShadowInk);
 if(m.battery>=0)std::snprintf(b,sizeof b,"%d%%",m.battery);else std::snprintf(b,sizeof b,"--%%");c.text(299,67,b,14,ShadowMuted);
 const bool live=m.time_valid&&now>=m.sampled_us&&now-m.sampled_us<250000&&now<m.until_us;
 const auto sec=live?(m.utc_us+now-m.sampled_us)/1000000:0;
 time_t t=time_t(sec)+m.tz_minutes*60;tm date{};if(live)gmtime_r(&t,&date);
 if(live){std::strftime(b,sizeof b,"%a %d %b",&date);for(auto* p=b;*p;++p)if(*p>='a'&&*p<='z')*p-=32;}else std::snprintf(b,sizeof b,"MESH TIME WAIT");c.text(44,67,b,14,ShadowMuted);
 const int h=live?date.tm_hour:-11,n=live?date.tm_min:-11;
 c.digit(44,91,45,76,h/10,ShadowInk);c.digit(96,91,45,76,h%10,ShadowInk);
 c.rect(153,111,5,5,live?ShadowLime:ShadowDim);c.rect(153,143,5,5,live?ShadowLime:ShadowDim);
 c.digit(171,91,45,76,n/10,ShadowInk);c.digit(223,91,45,76,n%10,ShadowInk);
 c.digit(287,128,19,36,live?date.tm_sec/10:-1,ShadowLime);c.digit(311,128,19,36,live?date.tm_sec%10:-1,ShadowLime);
 c.text(287,94,live?zone_name(m.tz_id,m.tz_minutes):"---",14,ShadowMuted);
 // Physical pulse annunciator, never a free-running simulated PPS.
 const bool fresh=now>=s.sampled_us&&now-s.sampled_us<500000;
 const bool pps=fresh&&s.pps&&s.last_pps_us&&now>=s.last_pps_us&&now-s.last_pps_us<=1500000;
 const bool flash=pps&&now-s.last_pps_us<220000;
 c.line(44,186,366,186,ShadowRule);
 c.line(44,186,64,186,pps?ShadowLime:ShadowRule);c.line(64,186,64,178,flash?ShadowLime:ShadowRule);c.line(64,178,74,178,flash?ShadowLime:ShadowRule);c.line(74,178,74,186,flash?ShadowLime:ShadowRule);
 c.text(84,173,pps?"PPS 1Hz":"PPS --",14,pps?ShadowLime:ShadowMuted);
 char rx[24];if(receiver_rms_current(m,now))rms_text(rx,sizeof rx,m.rms_ns);else std::snprintf(rx,sizeof rx,"--");
 std::snprintf(b,sizeof b,"%s %s",rms_caption(m),rx);centered(c,225,67,b,14,ShadowMuted);
 std::snprintf(b,sizeof b,"MESH %s",m.local_time?"HOLD":live?source(m.source):"WAIT");c.text(250,173,b,14,ShadowMuted);
 // Actual azimuth/elevation only. The acquisition sweep has no satellite dots.
 constexpr int cx=109,cy=250,radius=49;
 circle(c,cx,cy,radius,ShadowRule);circle(c,cx,cy,25,ShadowDim);c.line(cx-radius,cy,cx+radius,cy,ShadowDim);c.line(cx,cy-radius,cx,cy+radius,ShadowDim);
 centered(c,cx,193,"N",14,ShadowMuted);
 if(fresh&&s.powered&&!s.fix){const auto q=polar(cx,cy,radius-2,double(now%4000000)*360/4000000);c.line(cx,cy,q.x,q.y,ShadowLime);}
 for(unsigned i=0;fresh&&i<s.satellite_count&&i<16;++i){const auto& sat=s.satellites[i];if(!sat.position_known)continue;const auto q=polar(cx,cy,(90-std::min(90,int(sat.elevation)))*radius/90.0,sat.azimuth);circle(c,q.x,q.y,3,sat.cn0?ShadowLime:ShadowMuted,true);}
 c.text(185,202,"SATELLITE RECEPTION",14,ShadowMuted);
 std::snprintf(b,sizeof b,"%02u / %02u",fresh?s.used:0,fresh?s.in_view:0);c.text(185,218,b,36,ShadowInk);c.text(302,236,"USED / VIEW",14,ShadowMuted);
 for(unsigned i=0;i<12;++i){const auto level=fresh&&i<s.satellite_count?std::min(55,int(s.satellites[i].cn0)):0;const int x=186+int(i)*14;
  c.rect(x,264,9,27,ShadowDim);if(level)c.rect(x,291-level*27/55,9,level*27/55,ShadowLime);
 }
 c.text(185,293,"C/N0",14,ShadowMuted);c.text(305,293,"0-55 dBHz",14,ShadowMuted);
 const bool comparison=shadow_comparison_allowed(m,now);
 const auto stage=fresh?(!comparison&&(s.stage==ShadowStage::Measuring||s.stage==ShadowStage::Settling)?ShadowStage::WaitMesh:s.stage):ShadowStage::Off;
 const char* status=stage==ShadowStage::Starting?"GNSS STARTING":stage==ShadowStage::Searching?"SEARCHING SKY":stage==ShadowStage::Fix?"SATELLITES FOUND":stage==ShadowStage::WaitPps?"FIX / WAITING PPS":stage==ShadowStage::WaitMesh?"PPS / WAITING MESH":stage==ShadowStage::Settling?"PPS LOCK / SETTLING":stage==ShadowStage::Measuring?"PPS LOCK / MEASURING":stage==ShadowStage::Lost?"SIGNAL LOST / REACQUIRE":stage==ShadowStage::Fault?"GNSS FAULT":"GNSS OFF";
 const auto state_color=stage==ShadowStage::Fault||stage==ShadowStage::Lost?Amber:ShadowLime;
 circle(c,48,326,3,state_color,true);c.text(60,313,status,20,state_color);
 const char* steps[]={"SCAN","FIX","PPS","10s","LIVE"};
 const int progress=stage==ShadowStage::Measuring?4:stage==ShadowStage::Settling?3:pps?2:s.fix?1:0;
 for(int i=0;i<5;++i){const int x=45+i*65;c.rect(x,342,56,2,stage!=ShadowStage::Off&&i<=progress?ShadowLime:ShadowRule);c.text(x,346,steps[i],14,i<=progress&&stage!=ShadowStage::Off?ShadowInk:ShadowMuted);}
 c.line(44,370,366,370,ShadowRule);c.line(216,378,216,423,ShadowRule);
 const bool metric=comparison&&fresh&&pps&&s.valid&&stage==ShadowStage::Measuring;
 c.text(44,373,"MEAN",14,ShadowMuted);c.text(235,373,s.approximate?"P99 ABS ~":"P99 ABS",14,ShadowMuted);
 if(metric){shadow_value(b,sizeof b,s.mean_tenth_ns,true);}else std::snprintf(b,sizeof b,"--");int end=c.text(44,388,b,36,ShadowInk);c.text(end+6,405,metric?shadow_unit(s.mean_tenth_ns):"ns",14,ShadowMuted);
 if(metric){shadow_value(b,sizeof b,int64_t(s.p99_ns)*10,false);}else std::snprintf(b,sizeof b,"--");end=c.text(235,388,b,std::strlen(b)>5?26:36,ShadowInk);c.text(end+6,405,metric?shadow_unit(int64_t(s.p99_ns)*10):"ns",14,ShadowMuted);
 if(stage==ShadowStage::Settling)std::snprintf(b,sizeof b,"SETTLE %us / 10s",unsigned(s.settling_ms/1000));
 else if(metric){char value[24];shadow_value(value,sizeof value,int64_t(s.rms_ns)*10,false);std::snprintf(b,sizeof b,"RMS %s %s   N %u",value,shadow_unit(int64_t(s.rms_ns)*10),unsigned(s.samples));}
 else if(stage==ShadowStage::Searching)std::snprintf(b,sizeof b,"Take the watch outside");
 else if(stage==ShadowStage::Fault)std::snprintf(b,sizeof b,"Leave face to stop GNSS");
 else std::snprintf(b,sizeof b,"Waiting for fresh reference");
 centered(c,205,425,b,14,ShadowMuted);
 c.text(66,451,"GNSS AUTO",14,ShadowMuted);c.frame(164,445,82,29,4,ShadowLime);centered(c,205,448,"MODE",20,ShadowLime);
 for(int i=0;i<4;++i)c.rect(278+i*12,460,8,3,i==3?ShadowLime:ShadowRule);
}
