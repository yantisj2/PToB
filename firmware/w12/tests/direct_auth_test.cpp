#include "direct_auth.hpp"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace v12;
// Deterministic public fixture RNG only; NEVER linked into the target.
struct FixtureRng {uint32_t counter=0;uint64_t* clock=nullptr;bool expire=false;};
static int random_fixture(void* context,uint8_t* output,size_t n){
  auto& r=*static_cast<FixtureRng*>(context);
  while(n){uint8_t input[16]={'V','1','2','T','E','S','T','O','N','L','Y',0};
    const uint32_t c=++r.counter;for(unsigned i=0;i<4;++i)input[12+i]=uint8_t(c>>(8*i));
    uint8_t digest[32];assert(!mbedtls_sha256(input,sizeof(input),digest,0));
    const size_t take=n>32?32:n;std::memcpy(output,digest,take);output+=take;n-=take;}
  if(r.expire&&r.clock)*r.clock+=2000000;
  return 0;
}
struct LinkFixture {bool encrypted=true,direct=true,connected=true;uint64_t peer=101,coordinator=201;};
static LinkFixture links;
static bool check_link(void*,uint64_t incarnation){return links.encrypted&&links.direct&&links.connected&&(incarnation==links.peer||incarnation==links.coordinator);}
static uint64_t read_clock(void* p){return *static_cast<uint64_t*>(p);}
static void key(AuthLocal& local,unsigned d,FixtureRng& rng){
  mbedtls_ecp_group group;mbedtls_ecp_group_init(&group);mbedtls_ecp_point point;mbedtls_ecp_point_init(&point);
  mbedtls_mpi private_key;mbedtls_mpi_init(&private_key);
  assert(!mbedtls_ecp_group_load(&group,MBEDTLS_ECP_DP_SECP256R1));assert(!mbedtls_mpi_lset(&private_key,d));
  assert(!mbedtls_ecp_mul(&group,&point,&private_key,&group.G,random_fixture,&rng));
  assert(!mbedtls_mpi_write_binary(&private_key,local.private_d,32));uint8_t xy[65];size_t size=0;
  assert(!mbedtls_ecp_point_write_binary(&group,&point,MBEDTLS_ECP_PF_UNCOMPRESSED,&size,xy,65));
  assert(size==65);std::memcpy(local.identity.public_xy,xy+1,64);
  mbedtls_mpi_free(&private_key);mbedtls_ecp_point_free(&point);mbedtls_ecp_group_free(&group);
}
static void pair(DirectAuth& peer,DirectAuth& coor,FixtureRng& rng,LinkRole role,uint64_t& now,uint8_t unit=5){
  links={};peer={};coor={};peer.local.identity.id=unit;peer.local.identity.type=2;
  coor.local.identity.id=4;coor.local.identity.type=1;
  const uint8_t c1[]={0x44,0xb1,0x76,0xad,0x06,0x68};const uint8_t c3[]={0x44,0xb1,0x76,0xae,0x20,0x44};
  const uint8_t tc03[]={0x90,0x49,0xec,0xa4,0x30,0x29,0x82,0xe3};
  std::memcpy(peer.local.identity.silicon,unit==2?c1:c3,6);
  std::memcpy(coor.local.identity.silicon,tc03,8);
  peer.local.boot=11;coor.local.boot=12;peer.local.train=21;coor.local.train=22;
  peer.local.challenge=31;coor.local.challenge=32;
  peer.role=coor.role=role;peer.config=coor.config=1;peer.registry_epoch=coor.registry_epoch=1;
  peer.clock=coor.clock=read_clock;peer.clock_context=coor.clock_context=&now;
  peer.incarnation=101;coor.incarnation=201;peer.check_link=coor.check_link=check_link;
  if(role==LinkRole::TimeRelay)peer.local.root_permit=coor.local.root_permit=123;
  key(peer.local,1,rng);key(coor.local,2,rng);
  peer.trusted_peer=coor.local.identity;coor.trusted_peer=peer.local.identity;
}
static void begun(DirectAuth& p,DirectAuth& c,FixtureRng& rng){assert(p.begin(random_fixture,&rng)&&c.begin(random_fixture,&rng));}
static void exchanged(DirectAuth& p,DirectAuth& c,FixtureRng& rng){
  begun(p,c,rng);assert(p.receive_hello(c.local_hello,HelloBytes,random_fixture,&rng));
  assert(c.receive_hello(p.local_hello,HelloBytes,random_fixture,&rng));
}
static void hex(FILE* file,const uint8_t* p,size_t n){for(size_t i=0;i<n;++i)std::fprintf(file,"%02x",p[i]);}
int main(int argc,char** argv){
  FixtureRng rng;uint64_t now=100;rng.clock=&now;DirectAuth peer,coor;
  if(argc==2){FILE* out=std::fopen(argv[1],"w");assert(out);std::fputs("{\"status\":\"PUBLIC_TEST_ONLY_D1_D2_NOT_PRODUCTION\",\"vectors\":[",out);
    for(unsigned vector=0;vector<4;++vector){const LinkRole role=vector<2?LinkRole::GM:LinkRole::TimeRelay;
      const uint8_t unit=(vector&1)?5:2;now=100;rng.counter=0;pair(peer,coor,rng,role,now,unit);begun(peer,coor,rng);
      uint8_t peer_private[32],coor_private[32];std::memcpy(peer_private,peer.ephemeral,32);std::memcpy(coor_private,coor.ephemeral,32);
      assert(peer.receive_hello(coor.local_hello,244,random_fixture,&rng));assert(coor.receive_hello(peer.local_hello,244,random_fixture,&rng));
      uint8_t p[72],c[72];assert(peer.confirmation(p)&&coor.confirmation(c));
      if(vector)std::fputc(',',out);std::fprintf(out,"{\"peer\":\"C-%u\",\"role\":\"%s\",\"peer_HELLO\":\"",unit==2?1:3,role==LinkRole::GM?"GM":"TIME_RELAY");
      hex(out,peer.local_hello,HelloBytes);std::fputs("\",\"TC03_HELLO\":\"",out);hex(out,coor.local_hello,HelloBytes);
      std::fputs("\",\"transcript\":\"",out);hex(out,peer.transcript,32);
      std::fputs("\",\"peer_CONFIRM\":\"",out);hex(out,p,72);std::fputs("\",\"TC03_CONFIRM\":\"",out);hex(out,c,72);
      std::fputs("\",\"peer_sender_key_TEST_ONLY\":\"",out);hex(out,peer.sender_key,16);
      std::fputs("\",\"TC03_sender_key_TEST_ONLY\":\"",out);hex(out,coor.sender_key,16);
      std::fputs("\",\"peer_ECDH_private_TEST_ONLY\":\"",out);hex(out,peer_private,32);
      std::fputs("\",\"TC03_ECDH_private_TEST_ONLY\":\"",out);hex(out,coor_private,32);std::fputs("\"}",out);
    }std::fputs("]}\n",out);std::fclose(out);
  }
  now=100;pair(peer,coor,rng,LinkRole::GM,now);exchanged(peer,coor,rng);
  assert(!std::memcmp(peer.transcript,coor.transcript,32));assert(!std::memcmp(peer.sender_key,coor.peer_key,16));
  uint8_t p[72],c[72];assert(peer.confirmation(p)&&coor.confirmation(c));
  assert(!peer.binding().authenticated);assert(peer.receive_confirmation(c,72));assert(coor.receive_confirmation(p,72));
  assert(peer.binding().authenticated);
  now=3000100;assert(peer.binding().authenticated); // completed session survives2s handshake deadline
  links.encrypted=false;assert(!peer.binding().authenticated);links.encrypted=true;
  links.direct=false;assert(!peer.binding().authenticated);links.direct=true;
  links.peer=102;assert(!peer.binding().authenticated);links.peer=101;
  links.connected=false;assert(!peer.binding().authenticated);links.connected=true;
  assert(!peer.receive_confirmation(c,72));assert(!peer.confirmed);
  const unsigned hello_positions[]={0,1,2,3,4,5,6,7,8,15,16,24,32,40,48,112,176,180,243};
  for(unsigned position:hello_positions){now=100;pair(peer,coor,rng,LinkRole::GM,now);begun(peer,coor,rng);
    coor.local_hello[position]^=1;assert(!peer.receive_hello(coor.local_hello,244,random_fixture,&rng));assert(!peer.hello_ready&&!peer.confirmed);}
  now=100;pair(peer,coor,rng,LinkRole::GM,now);begun(peer,coor,rng);
  assert(!peer.receive_hello(peer.local_hello,244,random_fixture,&rng)); // exact reflection refused
  now=100;pair(peer,coor,rng,LinkRole::GM,now);coor.config=2;begun(peer,coor,rng);
  assert(!peer.receive_hello(coor.local_hello,244,random_fixture,&rng));
  now=100;pair(peer,coor,rng,LinkRole::GM,now);coor.registry_epoch=2;begun(peer,coor,rng);
  assert(!peer.receive_hello(coor.local_hello,244,random_fixture,&rng));
  now=100;pair(peer,coor,rng,LinkRole::TimeRelay,now);begun(peer,coor,rng);now=2000100;
  assert(!peer.receive_hello(coor.local_hello,244,random_fixture,&rng));
  now=100;pair(peer,coor,rng,LinkRole::GM,now);rng.expire=true;
  assert(!peer.begin(random_fixture,&rng)&&!peer.hello_ready&&peer.local.identity.id==255);rng.expire=false;
  now=100;pair(peer,coor,rng,LinkRole::GM,now);begun(peer,coor,rng);rng.expire=true;
  assert(!peer.receive_hello(coor.local_hello,244,random_fixture,&rng)&&!peer.peer_verified);rng.expire=false;
  for(unsigned position:{0u,1u,2u,3u,4u,5u,8u,40u,71u}){now=100;pair(peer,coor,rng,LinkRole::GM,now);exchanged(peer,coor,rng);
    assert(coor.confirmation(c));c[position]^=1;assert(!peer.receive_confirmation(c,72)&&!peer.confirmed);}
  now=100;pair(peer,coor,rng,LinkRole::TimeRelay,now);exchanged(peer,coor,rng);
  assert(coor.confirmation(c)&&peer.receive_confirmation(c,72));ControlState state;assert(state.bind(peer.binding()));
  uint8_t control[110]{};assert(!state.receive(control,110,100,100)); // Time-only never GM.
  peer.clear();assert(!peer.hello_ready&&!peer.confirmed&&peer.local.identity.id==255);
  std::puts("PASS ECDSA/ECDH/HKDF/HMAC:244B HELLO,72B confirmation,registry/silicon/key/role/config/incarnation binding, reflection/tamper/replay, pre/post-crypto deadline and zeroization; public D1/D2 fixtures only");
}
