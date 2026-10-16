#define CHM_DRAFT 1
#include "rc7_tx_estimate.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
namespace tx=v12::selected_tx;
namespace est=v12::tx_estimate;
struct Memory {
 static constexpr uintptr_t Env=0x3fc89000,Cs=0x3fc8a000,Tx=0x3fc8b000,Data=0x3fc8c000,Header=0x3fc8d000;
 std::array<uint8_t,84> env{};std::array<uint8_t,900> cs{};std::array<uint8_t,1260> desc{};
 std::array<uint8_t,190> data{};std::array<uint8_t,750> header{};
 unsigned activity=0,data_reads=0,header_reads=0,header_queries=0;
 int mutate_header=-1;bool mutate_descriptor=false,wrong_header_address=false;
 static void p16(uint8_t* p,uint16_t n){p[0]=uint8_t(n);p[1]=uint8_t(n>>8);}
 static void p32(uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
 bool environment_address(uint8_t a,uintptr_t& p){assert(a==activity);p=Env;return true;}
 bool em_address(uint16_t o,uintptr_t& p){
  if(o==0x400){p=Cs;return true;}if(o==0x1400){p=Tx;return true;}
  if(o==0x2c00){p=Data;return true;}
  assert(o==0x2000+75*activity);++header_queries;p=Header+75*activity;
  if(wrong_header_address&&header_queries>1)++p;return true;
 }
 uint8_t read8(uintptr_t p){
  if(p>=Env&&p-Env<env.size())return env[p-Env];
  if(p>=Cs&&p-Cs<cs.size())return cs[p-Cs];
  if(p>=Tx&&p-Tx<desc.size())return desc[p-Tx];
  if(p>=Data&&p-Data<data.size()){
   const auto v=data[p-Data];if(++data_reads==190){
    if(mutate_header>=0)header[75*activity+unsigned(mutate_header)]^=1;
    if(mutate_descriptor)desc[126*activity+6]^=1;
   }return v;
  }
  if(p>=Header&&p-Header<header.size()){++header_reads;return header[p-Header];}
  assert(false&&"read outside finite synthetic memory");return 0;
 }
 uint16_t read16(uintptr_t p){assert(!(p&1));return uint16_t(read8(p))|(uint16_t(read8(p+1))<<8);}
 uint32_t read32(uintptr_t p){assert(!(p&3));uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(read8(p+i))<<(8*i);return n;}
 Memory(unsigned a=0):activity(a){
  p32(env.data(),0x3fcefcf0);p32(env.data()+4,0x12345);p16(env.data()+78,0x2c00);p16(env.data()+80,190);env[82]=uint8_t(a);env[83]=1;
  auto* c=cs.data()+90*a;p16(c,0x406);p16(c+52,uint16_t(0x1400+126*a));p16(c+12,0x5678);p16(c+14,0x1234);
  p16(c+34,65535);p16(c+36,65535);p16(c+38,0x1f);
  auto* d=desc.data()+126*a;p16(d,0x8000);p16(d+2,0xbf27);p16(d+4,0x2c00);p16(d+12,uint16_t(0x2000+75*a));
 }
 void event(uint16_t e,bool chm,uint16_t instant=0){
  p16(env.data()+76,e);p16(cs.data()+90*activity+80,e);
  p16(desc.data()+126*activity+2,chm?0xc927:0xbf27);p16(desc.data()+126*activity+6,chm?10:0);
  auto* h=header.data()+75*activity;h[0]=8;h[1]=0x28;for(unsigned i=2;i<6;++i)h[i]=255;h[6]=31;p16(h+7,instant);
  data_reads=header_reads=header_queries=0;
 }
};
std::array<uint8_t,190> payload(unsigned publication){
 std::array<uint8_t,190> p{};p[0]=189;p[1]=p[2]=p[3]=255;p[4]='R';p[5]='C';p[6]=7;
 for(unsigned at:{8u,12u,20u,28u,88u,98u,106u,114u})p[at]=1;p[96]=p[97]=1;p[172]=5;
 Memory::p32(p.data()+110,publication);for(unsigned i=174;i<190;++i)p[i]=uint8_t(i^publication);assert(est::validate(p.data(),190));return p;
}
bool collect(Memory& m,est::BankPool& p,tx::EventFence& f,tx::Selection& s,uint8_t& slot,bool serial=true){
 tx::KnownExpected views[4];p.expectations(views);tx::Radio radio;
 const tx::Scope scope{0x12345,1,uint8_t(m.activity),0,10,true,serial};
 return tx::collect_known(s,scope,views,4,slot,radio,m,f);
}
void private_zero(const tx::Selection& s){assert(!s.flags);for(auto b:s.data)assert(!b);}
int main(){
 static_assert(sizeof(tx::Selection)==224&&sizeof(est::BankPool)==770);
 unsigned cases=0;auto a=payload(1),b=payload(2),c=payload(3);est::BankPool pool;pool.reset();Memory m;tx::EventFence fence{};tx::Selection s{};uint8_t slot=255;
 assert(pool.prepare(a.data(),190));m.data=a;m.event(100,false);assert(collect(m,pool,fence,s,slot)&&pool.pin(slot)&&pool.verify(slot,s));pool.release(slot);
 assert(pool.prepare(b.data(),190));m.data=b;unsigned refused=0,busy=0;
 for(unsigned e=101;e<=110;++e){m.event(uint16_t(e),true,111);
  const bool ok=collect(m,pool,fence,s,slot);
#if CHM_DRAFT
  assert(ok&&pool.pin(slot)&&pool.verify(slot,s));pool.release(slot);assert(m.header_reads==18);
#else
  assert(!ok&&s.reason==uint8_t(tx::Reason::Descriptor));private_zero(s);assert(!m.data_reads&&!m.header_reads);++refused;
  assert(!pool.prepare(c.data(),190));++busy;
#endif
 }
#if CHM_DRAFT
 assert(pool.prepare(c.data(),190));++cases;
#else
 assert(pool.prepared!=pool.current&&est::le32(pool.bytes[pool.prepared]+110)==2&&est::le32(pool.bytes[pool.current]+110)==1);
#endif
 m.event(111,false);assert(collect(m,pool,fence,s,slot)&&pool.pin(slot)&&pool.verify(slot,s));pool.release(slot);
 assert(pool.prepare(c.data(),190));++cases;
#if CHM_DRAFT
 // The ROM announces at uint16(event+10) and removes at the instant;
 // accepted futures are exactly1..10, including wrap. Zero/older/>10 refuse.
 for(unsigned future=0;future<=11;++future){
  est::BankPool p;p.reset();assert(p.prepare(b.data(),190));Memory x;x.data=b;
  x.event(65530,true,uint16_t(65530+future));tx::EventFence q{};
  const bool ok=collect(x,p,q,s,slot);
  assert(ok==(future>=1&&future<=10));if(!ok)private_zero(s);++cases;
 }
 {est::BankPool p;p.reset();assert(p.prepare(b.data(),190));Memory x;x.data=b;
  x.event(65530,true,65529);tx::EventFence q{};assert(!collect(x,p,q,s,slot));private_zero(s);++cases;}
 // Every advertised byte still matches its immutable prepared bank exactly.
 for(unsigned i=0;i<190;++i){est::BankPool p;p.reset();assert(p.prepare(b.data(),190));Memory x;x.data=b;x.data[i]^=1;x.event(7,true,17);tx::EventFence f{};
  assert(!collect(x,p,f,s,slot));private_zero(s);++cases;}
 // Every new header byte is read twice; any change refuses and zeros payload.
 for(unsigned i=0;i<9;++i){est::BankPool p;p.reset();assert(p.prepare(b.data(),190));Memory x;x.data=b;x.event(7,true,17);x.mutate_header=int(i);tx::EventFence f{};
  assert(!collect(x,p,f,s,slot)&&s.reason==uint8_t(tx::Reason::DataChanged));private_zero(s);++cases;}
 for(unsigned bad=0;bad<11;++bad){est::BankPool p;p.reset();assert(p.prepare(b.data(),190));Memory x;x.data=b;x.event(7,true,17);tx::EventFence f{};
  if(bad==0)x.header[0]=7;if(bad==1)x.header[1]=0x29;if(bad==2)x.header[6]|=0x20;
  if(bad==3)for(unsigned i=2;i<7;++i)x.header[i]=0;
  if(bad==4)Memory::p16(x.header.data()+7,7);if(bad==5)Memory::p16(x.header.data()+7,18);
  if(bad==6)Memory::p16(x.desc.data()+12,0x2001);
  if(bad==7)Memory::p16(x.desc.data()+2,0xc827);
  if(bad==8)Memory::p16(x.desc.data()+6,0x080a); // AuxPtr unsupported.
  if(bad==9)Memory::p16(x.desc.data(),0x8010); // Chained descriptor unsupported.
  if(bad==10)x.wrong_header_address=true;
  assert(!collect(x,p,f,s,slot));private_zero(s);++cases;
 }
 for(unsigned activity=0;activity<10;++activity){est::BankPool p;p.reset();assert(p.prepare(b.data(),190));Memory x(activity);x.data=b;x.event(65532,true,6);tx::EventFence f{};
  assert(collect(x,p,f,s,slot)&&p.pin(slot)&&p.verify(slot,s));p.release(slot);++cases;
 }
 {est::BankPool p;p.reset();assert(p.prepare(b.data(),190));Memory x;x.data=b;x.event(7,true,17);tx::EventFence f{};
  assert(!collect(x,p,f,s,slot,false));private_zero(s);assert(!x.header_reads&&!x.data_reads);++cases;}
 {est::BankPool p;p.reset();assert(p.prepare(b.data(),190));Memory x;x.data=b;x.event(7,true,17);x.mutate_descriptor=true;tx::EventFence f{};
  assert(!collect(x,p,f,s,slot)&&s.reason==uint8_t(tx::Reason::MetadataChanged));private_zero(s);++cases;}
#endif
 printf("{\"status\":\"PASSED\",\"draft\":%s,\"focused_cases\":%u,\"baseline_descriptor_refusals\":%u,\"baseline_pending_bank_busy\":%u,\"events_blocked\":%u,\"period_us\":62500,\"ten_event_span_us\":625000,\"actual_RF\":false}\n",CHM_DRAFT?"true":"false",cases,refused,busy,refused);
}
