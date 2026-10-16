#include "../legacy/m5stick_s3/ui.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace sticks3;
TdHealth input(const char* text,TdHealth::Level level){TdHealth h;h.status=text;h.level=level;return h;}
int main(){
 const auto good=input("GOOD HEALTH",TdHealth::Good),loss=input("LINK LOSS",TdHealth::Warning),fault=input("TIME INVALID",TdHealth::Fault),unknown=input("GM NOT SEEN",TdHealth::Unknown);
 for(const auto& raw:{loss,fault,unknown}){
  TdHealthDisplay f;assert(f.update(good,1000000).good());
  assert(f.update(raw,1010000).good());assert(f.update(raw,1159999).good());assert(!f.update(raw,1160000).good());
  assert(f.changes==1&&f.min_qualified_us==150000);
  assert(!f.update(good,1160001).good());assert(!f.update(good,1310000).good());assert(f.update(good,1310001).good());
 }
 {TdHealthDisplay f;f.update(good,1000000);f.update(loss,1010000);assert(f.update(good,1159999).good()&&f.cancelled==1&&!f.pending&&f.changes==0);
  f.update(loss,1160000);assert(f.update(loss,1309999).good());assert(!f.update(loss,1310000).good());
  f.update(good,1320000);assert(!f.update(loss,1469999).good()&&!f.pending); // Brief recovery does not flash green.
 }
 {TdHealthDisplay f;f.update(good,1000000);f.update(loss,1010000);f.update(unknown,1060000);f.update(fault,1110000);assert(!f.update(loss,1160000).good()); // Alternating bad reasons cannot starve the warning.
  assert(!std::strcmp(f.shown.text(),"LINK LOSS"));f.update(fault,1170000);assert(!std::strcmp(f.update(fault,1319999).text(),"LINK LOSS"));assert(!std::strcmp(f.update(fault,1320000).text(),"TIME INVALID"));
 }
 {TdHealthDisplay f;f.update(good,1000000);f.update(loss,1010000);assert(f.update(loss,900000).good());assert(f.update(loss,1049999).good());assert(!f.update(loss,1050000).good());}
 {TdHealthDisplay f;f.update(good,1000000);f.update(loss,1010000);assert(f.update(loss,2000000).good());assert(!f.update(loss,2150000).good());}
 {TdHealthDisplay f;assert(!f.update(unknown,0).good());f.update(good,1);assert(!f.update(good,150000).good());assert(f.update(good,150001).good());}
 {TdHealthDisplay f;f.update(good,1);for(uint64_t now=1000;now<1000000;now+=1000)assert(f.update((now/1000)%100<99?loss:good,now).good());assert(f.changes==0&&f.cancelled>0);}
 for(unsigned i=1;i<sizeof(TdHealthBanner::Labels)/sizeof(*TdHealthBanner::Labels);i++){auto h=input(TdHealthBanner::Labels[i],i==13?TdHealth::Good:TdHealth::Warning);auto b=TdHealthBanner::from(h);assert(b.valid&&b.code==i&&!std::strcmp(b.text(),h.status));}
 assert(!TdHealthBanner::from(input("FUTURE UNKNOWN STATE",TdHealth::Good)).good());
 // The exact renderer uses the one LCD-owned label for text and color in both
 // pages, while the original raw source permission remains immediately invalid.
 View v;v.native_dashboard=true;v.td.enabled=true;v.td.now_us=1000000;v.td.owner_stale=true;
 assert(!std::strcmp(TdHealth::evaluate(v.td).status,"OWNER STALE"));
 uint8_t frame[FrameBytes],strip[StripBytes],raw[FrameBytes];
 for(auto page:{0,5}){v.page=page;v.td_health_banner={};render(raw,v);TdHealthDisplay f;v.td_health_banner=f.update(good,1);RenderAudit audit;render(frame,v,&audit);assert(!audit.text_overflow&&!audit.clipped_pixels);assert(std::memcmp(raw+32*Width*2,frame+32*Width*2,17*Width*2));
  for(unsigned y=0;y<Height;y+=StripRows){render_strip(strip,y,v);assert(!std::memcmp(strip,frame+y*Width*2,StripBytes));}
  assert(!TdHealth::evaluate(v.td).time_valid); // UI hold never creates source/clock permission.
 }
 puts("PASS 149999/150000us entry/recovery, transient cancellation, alternating causes, reason hold, rollback/stall, high-rate noise, raw health immediate, two-page actual strip rendering");
}
