#include "g1_gps_shadow.hpp"
#define PTOB_GPS_PHYSICAL_REFERENCE_ONLY 1
#include "ptob_gps_shadow_clock.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
G1GpsShadow::Map map(uint64_t tick,int phase=250){
 G1GpsShadow::Map m;m.clock={tick,int64_t(tick*50)+phase,0,0,INT64_C(1700000000000000000)};
 m.root=11;m.boot=22;m.source=m.train=m.scope=1;m.valid=m.following=true;m.until=tick+100000000;m.permission=tick+40000000;return m;
}
G1GpsShadow::Reference ref(uint64_t edge){return {edge-10000000,1700000000,1,true};}
int main(){
 G1GpsShadow w;assert(!w.view().samples&&w.view().reason==G1GpsShadow::Waiting);
 assert(!w.edge(20000000,1,100,true,ref(20000000)).map_valid);
 auto m=map(20000000);w.observe(m,20001000,20002000,10000000,2000000);
 auto race=w.edge(20001000,2,100,true,ref(20001000));assert(race.row.reason==G1GpsShadow::Race);
 auto good=w.edge(40000000,3,100,true,ref(40000000));assert(good.map_valid&&good.row.reference&&good.row.phase_ns==250);
 assert(w.view().samples==1&&w.view().mean_ns==250&&w.view().rms_ns==250&&w.view().jitter_ns==0);
 // A newer model observed after the edge cannot score that earlier edge.
 auto newer=map(80000000,900);w.observe(newer,80001000,80002000,10000001,2000001);
 auto old=w.edge(60000000,4,100,true,ref(60000000));assert(old.row.phase_ns==250&&old.map.clock.tick==20000000);
 auto invalid=ref(100000000);invalid.rmc20=100000001;
 assert(!w.edge(100000000,5,100,true,invalid).row.reference);
 assert(w.view().samples==2&&w.view().invalid_reference==1);
 auto duplicate=w.edge(100000000,5,100,true,ref(100000000));assert(duplicate.row.reason==G1GpsShadow::Duplicate&&w.view().samples==2&&w.view().duplicates==1);
 invalid=ref(120000000);invalid.ready=false;assert(w.edge(120000000,6,100,true,invalid).row.reason==G1GpsShadow::NoFix);
 auto expired=w.edge(newer.until,7,100,true,ref(newer.until));assert(!expired.map_valid&&expired.row.reason==G1GpsShadow::Expired);
 G1GpsShadow population;
 for(unsigned i=1;i<80;i++){const uint64_t edge=uint64_t(i)*20000000;auto x=map(edge-20000,i%2?500:-500);population.observe(x,edge-19000,edge-18000,i,i);auto q=population.edge(edge,i,150,true,ref(edge));assert(q.map_valid&&q.row.reference);}
 auto v=population.view();assert(v.samples==79&&v.rms_ns==500&&v.max_ns==500&&v.jitter_ns>=499&&v.overwritten==63);
 G1GpsShadow::Record row;assert(population.read(0,row)&&row.number==64);assert(!population.read(16,row));
 auto changed=map(1600000000,1000);changed.boot=23;population.observe(changed,1600000100,1600000200,80,80);population.edge(1620000000,80,100,true,ref(1620000000));assert(population.view().samples==1&&population.view().mean_ns==1000&&population.view().session==2);
 // Invalid withdrawal prevents fallback to an older valid map after revocation.
 auto no=map(1640000000);no.valid=false;population.observe(no,1640000100,1640000200,81,81);assert(!population.edge(1660000000,81,100,true,ref(1660000000)).map_valid);
 G1GpsShadow eviction;
 for(unsigned i=1;i<5;i++)eviction.observe(map(uint64_t(i)*20000000),uint64_t(i)*20000000+10,uint64_t(i)*20000000+20,i,i);
 assert(eviction.edge(10000000,1,100,true,ref(20000000)).row.reason==G1GpsShadow::Evicted);
 // Physical-reference-only GPS cannot accidentally publish a fitted GPS clock.
 ptob_gps::Shadow gps;for(unsigned i=1;i<40;i++){const uint64_t t=uint64_t(i)*20000000;gps.edge(uint32_t(uint64_t(i)*80000000),i,t,100,t+100,true);ptob_gnss::Rmc r;r.valid=r.fix=true;r.second=1700000000+i;assert(gps.rmc(r,t+1000));}
 assert(gps.coherent>=3&&!gps.raw.model.valid&&!gps.mapped.model.valid&&!gps.view(800000000).valid);
 std::printf("PASS causal shadow PPS ordering, availability race, expiry, fresh fix, bias-inclusive RMS, scope isolation and bounded retained tail; bytes=%zu\n",sizeof(G1GpsShadow));
}
