#include "g1_parent_selection.hpp"
#include <cassert>
#include <cstdio>
using v12::G1ParentSelection;
using v12::G1ParentHistory;
namespace {
unsigned checks=0;
#define CHECK(x) do{assert(x);++checks;}while(false)
void admitted(G1ParentSelection& p){p.bind(99,199,1,1);p.reset();}
void pair(G1ParentSelection& p,uint64_t now,int gm=-50,int rc=-50){p.seen(11,false,int8_t(gm),now);p.seen(22,true,int8_t(rc),now);}
void bind(G1ParentSelection& p,uint64_t id,bool relay,uint64_t now){
 p.seen(id,relay,-50,now);p.bind(id,id+100,1,now);p.authenticated(id,relay,id+100,900,901,1,now);
}
void receive(G1ParentSelection& p,uint64_t start,unsigned seconds,bool delivered,bool usable=true,bool report=true){
 for(uint64_t t=start;t<=start+uint64_t(seconds)*1000000;t+=62500){
  if(report)p.usable(usable,t);p.tick(t,true,false);if(delivered)p.delivered(1,uint16_t(t/62500),t);
 }
}
}
int main(){
 CHECK(sizeof(G1ParentHistory)==656);CHECK(sizeof(G1ParentHistory::Row)==40);
 {G1ParentSelection p;admitted(p);pair(p,1000000);CHECK(!p.choose(1039999));CHECK(p.choose(1040000)==11);
  CHECK(p.selection_view(1040000).best_score==216); // Same RSSI, +16 GM points.
  p.reset();pair(p,2000000,-55,-50);CHECK(p.choose(2040000)==11); // Reacquire a fresh direct GM before opening an RC.
  p.reset();pair(p,3000000,-54,-50);CHECK(p.choose(3040000)==11); // Equal score, stable ID tie.
  for(uint64_t t=3000000;t<=6100000;t+=50000)p.tick(t,false,true);
  CHECK(p.seeking_relay());pair(p,6110000,-45,-50);CHECK(p.choose(6120000)==11); // Cold does not exclude GM.
  p.diagnostic(true);CHECK(p.choose(6120000)==22);p.target(11,false);CHECK(!p.choose(6120000));
  p.diagnostic(false);CHECK(p.choose(6120000)==11);p.target(22,true);CHECK(p.choose(6120000)==22);
  p.reset();CHECK(!p.pinned()&&!p.diagnostic());CHECK(!p.choose(6200000));}
 {G1ParentSelection p;admitted(p);bind(p,22,true,1000000);receive(p,1000000,6,true);
  CHECK(p.quality().healthy&&!p.quality().poor);auto r=p.history().find(22,true,7000000);CHECK(r&&r->delivery==1000&&r->usable==1000);
  CHECK(r->delivery_samples==6&&r->usable_samples==6);
  const auto count=p.quality().samples;const auto history_samples=r->delivery_samples;
  for(uint64_t t=7062500;t<10000000;t+=62500){p.usable(false,t);p.tick(t,false,false);}
  CHECK(p.quality().samples==count);CHECK(p.history().find(22,true,10000000)->delivery_samples==history_samples);
  p.bind(22,122,1,10000000);CHECK(p.quality().samples==count); // Same renewal retains quality.
  p.reset();CHECK(!p.quality().received&&p.parent()==0);CHECK(p.history().find(22,true,10000000)->delivery==1000);
  CHECK(!p.choose(10000000));bind(p,22,true,11000000);CHECK(!p.quality().samples);
  CHECK(p.history().find(22,true,11000000)->delivery==1000); // Mode/re-admission keeps only advice.
  const auto oldtag=p.history().find(22,true,11000000)->context;
  p.authenticated(22,true,999,9999,9998,2,11000000);
  CHECK(p.history().find(22,true,11000000)->context!=oldtag);CHECK(!p.quality().samples);
  CHECK(!p.history().find(22,false,11000000)); // Role is part of advisory key.
  p.unavailable();pair(p,11100000,-20,-80);CHECK(p.choose(11150000)==11); // Hard-unavailable bypasses dwell.
 }
 {G1ParentSelection p;admitted(p);bind(p,22,true,1000000);pair(p,1100000,-30,-50);
  CHECK(p.choose(1140000)==22);CHECK(p.selection_view(1140000).reason==G1ParentSelection::Decision::Dwell);
  CHECK(p.choose(1140000,true)==22); // Renewal does not erase dwell.
  p.bind(22,122,1,9000000);pair(p,11000000,-30,-50);CHECK(p.choose(11000000)==11); // Renewal does not restart dwell.
  p.reset();bind(p,22,true,12000000);pair(p,23000000,-51,-50);CHECK(p.choose(23000000)==11);
  CHECK(p.selection_view(23000000).reason==G1ParentSelection::Decision::ReturnGM);
  p.reset();bind(p,22,true,24000000);pair(p,35000000,-50,-50);CHECK(p.choose(35000000)==11); // Exact margin is sufficient.
 }
 {G1ParentSelection p;admitted(p);bind(p,22,true,1000000);receive(p,1000000,6,false,false);
  CHECK(p.quality().poor);const auto r=p.history().find(22,true,7000000);CHECK(r&&r->delivery==0&&r->usable==0&&r->usable_samples==6);
  pair(p,7100000,-80,-30);CHECK(p.choose(7150000)==11); // Proven poor RC can lose to weaker GM.
  p.reset();pair(p,8000000,-80,-30);CHECK(p.choose(8040000)==11); // Learning prevents immediate cycle back.
  p.diagnostic(true);CHECK(p.choose(8040000)==22); // Explicit role constraint still applies.
  p.reset();pair(p,1810000000,-80,-30);CHECK(p.choose(1810040000)==11);CHECK(p.history().entries(1810040000)==0); // Advice expires; direct GM preference remains.
 }
 {G1ParentSelection p;admitted(p);bind(p,22,true,1000000);receive(p,1000000,2,true,false,false);
  auto r=p.history().find(22,true,3000000);CHECK(r&&r->delivery==1000&&r->usable_samples==0); // Missing usable reports remain unknown.
  receive(p,4000000,2,true,false,true);r=p.history().find(22,true,6000000);CHECK(r&&r->usable_samples>0&&r->usable==0);
  const auto n=r->delivery_samples;p.usable(true,7000000);p.tick(7000000,true,false);CHECK(p.history().find(22,true,7000000)->delivery_samples==n); // Owner stall is not listening loss.
 }
 {G1ParentSelection p;admitted(p);bind(p,22,true,1000000);p.tick(1000000,true,false);p.delivered(1,65535,1000000);p.delivered(1,65535,1000000);
  CHECK(p.quality().received==1);p.delivered(1,0,1062500);CHECK(p.quality().received==2);p.delivered(1,65535,1062501);CHECK(p.quality().received==2);
  p.tick(999999,true,false);CHECK(p.history().entries(999999)==0);CHECK(p.quality().samples==0);
 }
 {G1ParentSelection p;admitted(p);pair(p,1000000);p.failed(11,1000000);CHECK(p.history().find(11,false,1000000)->delivery_samples==0);
  CHECK(p.choose(1040000)==22);p.seen(11,false,-50,6100000);p.seen(22,true,-50,6100000);CHECK(p.choose(6140000)==11); // Bias equals one failure, stable tie.
  p.failed(11,6200000);CHECK(!p.choose(6200000)||p.choose(6200000)==22); // Consumed advertisement cannot retry.
  CHECK(p.history().failures(*p.history().find(11,false,6200000),6200000)==2);
  CHECK(p.history().failures(*p.history().find(11,false,6200000),36200000)==1);
  CHECK(p.history().failures(*p.history().find(11,false,6200000),66200000)==0);
  p.reset();CHECK(p.history().entries(7000000)==1);p.clear_history();CHECK(!p.history().entries(7000000));
 }
 {G1ParentHistory h;h.authenticated(1,true,2,0);CHECK(h.entries(0)==1);CHECK(h.find(1,true,1799999999));CHECK(!h.find(1,true,1800000000));
  for(uint64_t i=1;i<=16;i++)h.sample(i,true,1000,true,1000,i*1000000);
  CHECK(h.entries(16000000)==16);h.sample(17,true,500,true,500,17000000);CHECK(!h.find(1,true,17000000));CHECK(h.find(17,true,17000000));CHECK(h.entries(17000000)==16);
  const auto observed=h.find(17,true,17000000)->observed;h.authenticated(17,true,99,18000000);CHECK(h.find(17,true,18000000)->observed==observed); // Admission is not a performance sample.
  h.advance(1000000);CHECK(!h.entries(1000000)); // Backward clock fails closed.
  const uint64_t wrap=uint64_t(UINT32_MAX-3)*1000000;h.sample(99,false,0,true,0,wrap);CHECK(h.find(99,false,wrap+10000000));CHECK(!h.find(99,false,wrap+1800000000));
  CHECK(!h.find(99,false,wrap+(uint64_t(UINT32_MAX)+1)*1000000)); // Ancient rows cannot revive on seconds wrap.
 }
 {G1ParentHistory h;h.sample(1,true,1000,true,1000,1000000);h.sample(1,true,0,true,0,2000000);
  CHECK(h.find(1,true,2000000)->delivery==750);CHECK(h.find(1,true,2000000)->usable==750);
  for(unsigned i=0;i<66000;i++)h.sample(1,true,1000,false,0,3000000+i);
  CHECK(h.find(1,true,4000000)->delivery_samples==UINT16_MAX);CHECK(h.find(1,true,4000000)->usable_samples==2);
 }
 {G1ParentHistory h;h.sample(1,true,0,true,0,1000000);for(unsigned i=0;i<4;i++)h.failed(1,true,1000000);
  CHECK(G1ParentHistory::score(h.find(1,true,1000000),true,-127,1000000,h)==-344);
  G1ParentSelection p;admitted(p);p.failed(7,true,1000000);CHECK(p.history().find(7,true,1000000));CHECK(!p.history().find(7,false,1000000));
 }
 {G1ParentSelection p;admitted(p);bind(p,22,true,1000000);receive(p,1000000,6,true);
  p.seen(11,false,-20,7100000);CHECK(p.choose(7100000,true)==0); // Strong challenger cannot bypass live dwell via missing ADV.
  CHECK(p.selection_view(7100000).reason==G1ParentSelection::Decision::Dwell);
  p.seen(22,true,-50,7110000);CHECK(p.choose(7110000,true)==22); // Fresh incumbent locator enables renewal.
  receive(p,7162500,5,true);p.seen(11,false,-80,12162500);CHECK(p.choose(12162500,true)==11);
  CHECK(p.selection_view(12162500).reason==G1ParentSelection::Decision::ReturnGM); // Fresh GM return bypasses RC score margin after dwell.
  p.seen(33,false,-20,12170000);CHECK(p.choose(12170000,true)==33); // Sufficient fresh improvement after dwell.
  p.unavailable();CHECK(p.choose(12200000,true)==33);
 }
 {G1ParentSelection p;admitted(p);bind(p,22,true,1000000);p.unavailable();p.seen(22,true,-50,12000000);
  p.bind(22,122,1,12000000);p.seen(33,false,-20,12000000);CHECK(p.choose(12040000)==22); // New binding clears only cold fallback.
  p.seen(22,true,-50,23000000);p.bind(22,999,2,23000000);p.seen(33,false,-20,23000000);
  CHECK(p.choose(23000000,true)==22);CHECK(p.selection_view(23000000).reason==G1ParentSelection::Decision::Dwell); // New boot/epoch, same ID starts fresh dwell.
  p.bind(22,999,2,30000000);p.seen(22,true,-50,33000000);p.seen(33,false,-20,33000000);
  CHECK(p.choose(33000000,true)==33); // Same-scope renewal did not restart dwell.
 }
 // Active hints affect only comparable eligible relays, never authority or
 // stronger measured quality, stale locators, named choices or live dwell.
 for(unsigned order=0;order<2;++order){const bool reverse=order!=0;
  G1ParentSelection p;admitted(p);
  if(reverse)p.seen(22,true,-54,1000000,true);
  p.seen(11,true,-50,1000000,false);
  if(!reverse)p.seen(22,true,-54,1000000,true);
  CHECK(p.choose(1040000)==22);CHECK(p.selection_view(1040000).best_score==184);
  p.seen(22,true,-54,1040000,false);CHECK(p.choose(1040000)==11);
  p.reset();p.seen(11,true,-50,2000000);p.seen(22,true,-55,2000000,true);CHECK(p.choose(2040000)==11);
  p.reset();p.seen(22,true,-50,3000000,true);p.seen(11,true,-50,3200000);CHECK(p.choose(3200000)==11);
  p.reset();p.seen(11,true,-50,4000000);p.seen(22,true,-50,4000000,true);p.cooldown(22,4000000,5000000);CHECK(p.choose(4040000)==11);
  p.reset();p.target(11,true);p.seen(11,true,-50,5000000);p.seen(22,true,-50,5000000,true);CHECK(p.choose(5040000)==11);
  p.reset();pair(p,6000000);p.seen(22,true,-50,6000000,true);CHECK(p.choose(6040000)==11); // GM rank unaffected.
 }
 {G1ParentSelection p;admitted(p);bind(p,22,true,1000000);receive(p,1000000,6,false,false);p.unavailable();
  p.seen(22,true,-50,7100000,true);p.seen(11,true,-50,7100000,false);CHECK(p.choose(7140000)==11);
 }
 {G1ParentSelection p;admitted(p);bind(p,11,true,1000000);p.seen(11,true,-50,1100000,false);p.seen(22,true,-50,1100000,true);
  CHECK(p.choose(1140000)==11);CHECK(p.selection_view(1140000).reason==G1ParentSelection::Decision::Dwell);
 }
 {G1ParentSelection p;admitted(p);bind(p,22,true,1000000);receive(p,1000000,6,false,false);p.unavailable();p.diagnostic(true);
  p.seen(22,true,-30,7100000,false);p.seen(11,true,-60,7100000,false);CHECK(p.choose(7140000)==22); // RC ordering is RSSI, as on T096.
 }
 {G1ParentSelection p;admitted(p);p.seen(11,true,-50,1000000);p.pending(11,1040000);
  p.seen(11,true,-50,1100000);p.seen(22,true,-20,1100000,true);CHECK(p.choose(1140000)==11);
  CHECK(p.selection_view(1140000).reason==G1ParentSelection::Decision::Admission);
  p.pending(11,5000000);p.seen(11,true,-50,6000000);p.seen(22,true,-20,6000000,true);CHECK(p.choose(6000000)==11);
  p.seen(11,true,-50,6040000);p.seen(22,true,-20,6040000,true);CHECK(p.choose(6040000)==22); // Repeated pending did not rearm5s.
  p.reset();p.seen(11,true,-50,7000000);p.pending(11,7000000);p.seen(22,true,-20,7300000,true);CHECK(!p.choose(7340000)); // Missing fresh chosen hint cannot open another relay.
  p.failed(11,true,7340000);CHECK(p.choose(7340000)==22); // A concrete failed admission permits another choice.
 }
 std::printf("PASS %u focused parent-selection/history assertions; history=%zuB selector=%zuB; synthetic advisory policy only\n",checks,sizeof(G1ParentHistory),sizeof(G1ParentSelection));
}
