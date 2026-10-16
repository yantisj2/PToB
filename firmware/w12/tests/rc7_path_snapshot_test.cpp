#include "rc7_path_snapshot.hpp"
#include "rc7_client.hpp"
#include "rc7_relay_estimate.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace path_snapshot_test {
using namespace v12;
constexpr r2_group_config Group={{1,2,3},7,9};
rc7_path path(unsigned depth=1){rc7_path p{};p.source_epoch=9;p.depth=uint8_t(depth);
 p.node[0]={11,22,1,1,0,0};if(depth)p.node[1]={33,44,2,2,1,1};
 if(depth==2)p.node[2]={55,66,3,3,2,1};return p;}
rc5_admission admission(const rc7_path& p){rc5_admission a{};const auto& n=p.node[p.depth];
 a.parent_boot=n.boot;a.parent_train=n.train;a.parent_epoch=n.epoch;a.root_boot=p.node[0].boot;
 a.root_epoch=p.source_epoch;a.publication_floor=1;a.now_tick=16000000;
 a.root_lo_ns=1000000000;a.root_hi_ns=1001000000;a.root_expires_ns=2650000000;return a;}
void unhex(uint8_t (&bytes)[89],const char (&hex)[179]){
 assert(std::strlen(hex)==178);for(unsigned i=0;i<89;++i){unsigned value=0;
  for(unsigned j=0;j<2;++j){const char c=hex[2*i+j];assert((c>='0'&&c<='9')||(c>='a'&&c<='f'));
   value=16*value+unsigned(c<='9'?c-'0':c-'a'+10);}bytes[i]=uint8_t(value);}
}
bool zero(const char (&hex)[179]){for(unsigned i=0;i<178;++i)if(hex[i]!='0')return false;return hex[178]==0;}
rc7_path unpack(const uint8_t (&b)[89]){rc7_path p{};p.source_epoch=uint32_t(r2_group_get(b,4));p.depth=b[4];
 for(unsigned i=0;i<3;++i){const auto* n=b+5+28*i;assert(!n[26]&&!n[27]);
  p.node[i]={r2_group_get(n,8),r2_group_get(n+8,8),uint32_t(r2_group_get(n+16,4)),uint32_t(r2_group_get(n+20,4)),n[24],n[25]};}
 return p;
}
}

#ifdef V12_PATH_OWNER_TEST
int main(int argc,char** argv){using namespace v12;using namespace path_snapshot_test;assert(argc==2);
 const std::string which=argv[1];const uint8_t mac[]={0x44,0xb1,0x76,0xae,0x17,0x78};
 assert(r2_init(44,mac)&&r2_host_init()&&r2_host_sync());step();
 assert(zero(r2_status().rc7_path_hex));const auto p=path();const auto a=admission(p);
 assert(rc7_bind(&rc_client.receiver,&p,provision.identity,false,255,&a));
 publish(fake_now);char expected[179];rc7_path_snapshot::encode(expected,&p);
 assert(!std::strcmp(r2_status().rc7_path_hex,expected));assert(!r2_status().following);
 if(which=="unbound"){rc_client.fence();publish(fake_now);assert(zero(r2_status().rc7_path_hex));}
 else if(which=="pending"||which=="failed"){
  assert(r2_set_relay_only(true));assert(rc_bound()); // Caller has not mutated controller/receiver.
  assert(zero(r2_status().rc7_path_hex)&&!r2_status().rc7_bound);
  if(which=="failed"){fake_now=parent_diagnostic.deadline_us;r2_observe(fake_now);
   assert(parent_diagnostic.state==4&&zero(r2_status().rc7_path_hex));}
 }else assert(which=="bound");
 std::printf("PASS actual native published path and unbound/pending/failure diagnostic read fences; status_bytes=%zu\n",sizeof(R2Status));
}
#else
int main(){using namespace v12;using namespace path_snapshot_test;unsigned cases=0;
 char hex[179];std::memset(hex,'x',sizeof hex);rc7_path_snapshot::encode(hex,nullptr);assert(zero(hex));++cases;
 for(unsigned depth=0;depth<=2;++depth){const auto p=path(depth);assert(rc7_path_valid(&p));
  rc7_path_snapshot::encode(hex,&p);uint8_t bytes[89];unhex(bytes,hex);const auto decoded=unpack(bytes);
  assert(rc7_path_same(&p,&decoded));uint8_t actual[16],expected[16];
  assert(!rc7_path_mac(actual,&p,&Group));assert(!r2_group_mac(expected,&Group,"RC7PATHS",bytes,89));
  assert(!std::memcmp(actual,expected,16));++cases;
 }
 {auto p=path();p.source_epoch=UINT32_C(0xfedcba98);
  p.node[0].id=UINT64_C(0x1122334455667788);p.node[0].boot=UINT64_C(0x8877665544332211);
  p.node[1].id=UINT64_C(0x1020304050607080);p.node[1].boot=UINT64_C(0x8172635445362718);
  p.node[1].train=UINT32_C(0x12345678);p.node[1].epoch=UINT32_C(0x89abcdef);
  rc7_path_snapshot::encode(hex,&p);uint8_t bytes[89];unhex(bytes,hex);const auto decoded=unpack(bytes);
  assert(rc7_path_same(&p,&decoded)&&bytes[0]==0x98&&bytes[3]==0xfe&&bytes[5]==0x88&&bytes[12]==0x11);
  uint8_t actual[16],expected[16];assert(!rc7_path_mac(actual,&p,&Group));
  assert(!r2_group_mac(expected,&Group,"RC7PATHS",bytes,89)&&!std::memcmp(actual,expected,16));++cases;
 }
 {auto p=path(2);p.source_epoch=0;rc7_path_snapshot::encode(hex,&p);assert(zero(hex));++cases;}
 {auto p=path(2);p.node[0].id=0;assert(!rc7_path_valid(&p));rc7_path_snapshot::encode(hex,&p);assert(zero(hex));++cases;}
 {auto p=path(2);p.node[2].id=p.node[0].id;assert(!rc7_path_valid(&p));rc7_path_snapshot::encode(hex,&p);assert(zero(hex));++cases;}
 {auto p=path();p.node[2].id=99;rc7_path_snapshot::encode(hex,&p);assert(zero(hex));++cases;}
 const auto p=path(2);const auto a=admission(p);rc7_receiver receiver{};Rc7Client client;
 for(unsigned i=0;i<=p.depth;++i){assert(!client.path_usable(p,p.node[i].id));
  assert(!rc7_bind(&receiver,&p,p.node[i].id,false,255,&a));++cases;}
 assert(client.path_usable(p,77)&&rc7_bind(&receiver,&p,77,false,255,&a));++cases;
 assert(!rc7_bind(&receiver,&p,77,true,3,&a));++cases; // Depth2 parent cannot produce RC3.
 assert(!client.path_usable(path(0),77));++cases; // RC admission needs a relay, not root-only locator.
 const rc5_origin origin={11,22,9,1000000000,2650000000,1700000000000000000,1000000000,0,0};
 const G1ClockPrediction map={20000100,1000005000,0,0,origin.utc_offset};uint8_t proof[16];
 assert(!rc7_origin_sign(proof,&origin,&Group));
 Rc7RelayEstimate next;assert(next.input(path(),origin,proof,map,20000000,1,20000100,0,77,88,Group));
 assert(next.path.depth==2&&next.path.node[2].id==77);++cases;
 Rc7RelayEstimate refused;assert(!refused.input(p,origin,proof,map,20000000,1,20000100,0,77,88,Group));++cases;
 for(const auto self:{uint64_t(11),uint64_t(33)}){assert(!refused.input(path(),origin,proof,map,20000000,1,20000100,0,self,88,Group));++cases;}
 proof[0]^=1;assert(!refused.input(path(),origin,proof,map,20000000,1,20000100,0,77,88,Group));++cases;
 assert(!refused.input(path(),origin,nullptr,map,0,1,20000100,0,77,88,Group));++cases;
 assert(!refused.input(path(),origin,nullptr,map,20000000,1,20000100,500000,77,88,Group));++cases;
 std::printf("{\"status\":\"PASSED\",\"cases\":%u,\"canonical_bytes\":89,\"hex_chars\":178}\n",cases);
}
#endif
