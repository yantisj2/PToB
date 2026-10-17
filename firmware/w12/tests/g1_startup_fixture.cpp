#include "g1_parent_selection.hpp"
#include "g1_client_policy.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using v12::G1ParentSelection;
static void scan(G1ParentSelection& p,uint64_t start,uint64_t until){
 p.tick(start,false,true);for(uint64_t t=start+100000;t<=until;t+=100000)p.tick(t,false,true);
}
int main(){
  v12::G1Anchor traffic{true,11,22,1700000000000000,1000000,2500000,1,1,80000};
  for(const auto threshold:{100000u,125000u,149999u,150000u,174999u,175000u,200000u,225000u,250000u}){
    const auto now=traffic.at+uint64_t(threshold-traffic.uncertainty-100)*500;
    traffic.until=now+1500000; // Fresh source permission, never a new replay anchor.
    assert(traffic.guard(now)==threshold);
    assert(traffic.refresh(now));
    assert(traffic.refresh(now,true)==(threshold>=150000));
    assert(traffic.urgent(now,true)==(threshold>=175000));
    assert(traffic.valid(now)==(threshold<249900));
  }
  const auto lost=traffic.at+1000000;traffic.until=lost-1500000;
  assert(traffic.refresh(lost,true)&&traffic.urgent(lost,true));
 unsigned n=0;
#define CHECK(x) do{assert(x);++n;}while(false)
 {G1ParentSelection p;p.seen(22,true,-20,1000000);scan(p,1000000,4000000);
  CHECK(!p.choose(1040000));p.seen(11,false,-80,3700000);p.seen(22,true,-20,4000000);
  CHECK(!p.choose(4000000)); // Retained regression: GM 300ms old cannot lose to the relay at 3s.
  p.seen(11,false,-80,4100000);CHECK(p.choose(4100000)==11);
  CHECK(!p.parent()&&p.history().entries(4100000)==0);
  p.failed(11,false,4100000);CHECK(!p.choose(4100000)); // Original cooldown does not open early fallback.
  p.seen(11,false,-80,9200000);p.seen(22,true,-20,9200000);CHECK(p.choose(9200000)==11);
 }
 {G1ParentSelection p;p.seen(22,true,-20,1000000);scan(p,1000000,4000000);
  p.seen(22,true,-20,15999999);CHECK(!p.choose(15999999));
  p.seen(22,true,-20,16000000);CHECK(p.choose(16000000)==22); // No GM: finite fallback.
 }
 {G1ParentSelection p;p.seen(22,true,-20,1000000);p.seen(22,true,-20,16000000);
  CHECK(!p.choose(16000000)); // Wall time without actual scanning cannot pass.
  scan(p,16000000,18900000);p.seen(22,true,-20,18900000);CHECK(!p.choose(18900000));
  p.tick(19000000,false,true);p.seen(22,true,-20,19000000);CHECK(p.choose(19000000)==22);
 }
 {G1ParentSelection p;p.seen(22,true,-20,1000000);scan(p,1000000,4000000);
  p.seen(11,false,-20,15700000);p.seen(22,true,-80,16000000);CHECK(!p.choose(16000000));
  p.seen(22,true,-80,17699999);CHECK(!p.choose(17699999)); // Waiting does not connect a stale GM.
  p.seen(22,true,-80,17700000);CHECK(p.choose(17700000)==22); // Re-observation grace is bounded.
 }
 {G1ParentSelection p;p.seen(22,true,-20,1000000);scan(p,1000000,4000000);
  p.seen(11,false,-20,16000000);p.seen(22,true,-80,16000000);p.failed(11,false,16000000);
  CHECK(p.choose(16000000)==22); // Failed/cooling GM does not indefinitely block fallback.
 }
 {G1ParentSelection p;p.seen(22,true,-20,1000000);p.seen(11,false,-80,4000000);
  p.seen(33,false,-60,4000000);CHECK(p.choose(4000000)==33); // Score among fresh GMs.
 }
 {G1ParentSelection p;p.diagnostic(true);p.seen(22,true,-50,1000000);p.seen(11,false,-20,1000000);CHECK(p.choose(1040000)==22);}
 {G1ParentSelection p;p.target(22,true);p.seen(22,true,-50,1000000);CHECK(p.choose(1040000)==22);}
 {G1ParentSelection p;p.target(11,false);p.seen(11,false,-80,1000000);CHECK(p.choose(1040000)==11);}
 {G1ParentSelection p;p.seen(22,true,-20,1000000);p.bind(22,122,1,4000000);
  p.unavailable();p.reset();p.seen(22,true,-20,5000000);p.seen(11,false,-80,5000000);CHECK(p.choose(5040000)==11);} // Normal reacquisition also prefers the available GM.
 {G1ParentSelection p;p.seen(22,true,-20,1000000);CHECK(!p.choose(999999));CHECK(!p.choose(4000000));}
 {G1ParentSelection p;p.seen(22,true,-20,1000000);p.tick(1000000,false,true);
  p.tick(16000000,false,true);p.seen(22,true,-20,16000000);CHECK(!p.choose(16000000)); // Long unknown gap not credited.
 }
 {G1ParentSelection p;p.seen(22,true,-20,1000000);
  for(uint64_t t=1000000;t<=16000000;t+=100000)p.tick(t,false,false);
  p.seen(22,true,-20,16000000);CHECK(!p.choose(16000000)); // Quiet and unowned scans not credited.
 }
 std::printf("PASS %u startup GM-first assertions; no hardware or RF claims\n",n);
}
