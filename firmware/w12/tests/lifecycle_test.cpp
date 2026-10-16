#include "client_lifecycle.hpp"
#include "capture_contract.hpp"
#include "public_grants.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace v12;
static void put64(uint8_t* b,uint64_t n){for(unsigned i=0;i<8;++i)b[i]=uint8_t(n>>(8*i));}
int main(){
  for(Mode m:{Mode::Half350,Mode::Half400,Mode::Half450,Mode::Coarse}){
    ClientLifecycle c;assert(c.configure(m));c.requested=true;
    for(uint64_t i=0;i<1000;++i){
      const uint64_t start=i*(m==Mode::Coarse?4000000:SecondUs)+1;
      assert(c.open(start));c.scan=true;c.pending=true;
      assert(!c.open(start+100));assert(!c.should_stop(c.stop_by-1));assert(c.should_stop(c.stop_by));
      c.stop_begin(c.stop_by);c.scan=false;
      c.drained(c.stop_by+1);assert(c.state==Lifecycle::Stopping); // Cancel awaits callback.
      c.pending=false;c.synced=true;assert(!c.accept_report(c.stop_by+2)); // Racing sync success.
      c.synced=false;c.drained(start+window(m));assert(c.state==Lifecycle::Quiet);
      assert(!c.open(c.cycle_end-1));assert(window(m)<=500000);
    }
  }
  for(Mode m:{Mode::Full,Mode::GM,Mode::Relay}){
    ClientLifecycle c;assert(c.configure(m));c.requested=true;assert(c.open(1));
    c.synced=true;c.state=Lifecycle::Following;assert(c.accept_report(10000000));
    assert(!c.should_stop(10000000));c.requested=false;assert(c.should_stop(10000000));
    c.stop_begin(10000000);assert(c.late(10005001));c.fail();assert(!c.open(20000000));
  }
  ClientLifecycle late;late.mode=Mode::Half400;late.requested=true;assert(late.open(1));
  late.stop_begin(late.stop_by);late.drained(400002);assert(late.state==Lifecycle::Fault);
  ClientLifecycle overflow;overflow.requested=true;assert(!overflow.open(UINT64_MAX));
  Ring<uint32_t,8> ring;for(unsigned i=0;i<8;++i)assert(ring.push(i));assert(!ring.push(9));
  for(unsigned i=0;i<8;++i){uint32_t n;assert(ring.pop(n)&&n==i);}uint32_t n;assert(!ring.pop(n));
  assert(ring.drops==1&&ring.peak==8);
  Capture capture;assert(!precision_capture(capture));PhysicalSupport support;assert(!support.admit_publication());
  uint8_t root_bytes[190]{};root_bytes[0]=189;root_bytes[1]=0xff;root_bytes[2]=0xff;root_bytes[3]=0xff;
  root_bytes[4]=12;root_bytes[5]=129;root_bytes[6]=4;root_bytes[7]=root_bytes[8]=0;root_bytes[10]=1;root_bytes[11]=1;
  put64(root_bytes+12,7);put64(root_bytes+20,8);put64(root_bytes+28,9);
  root_bytes[36]=1;put64(root_bytes+40,10);put64(root_bytes+48,3000000000);root_bytes[56]=1;
  root_bytes[60]=0x30;root_bytes[61]=0x75;
  const GrantPolicy policy=CandidatePolicy;RootGrant root;assert(decode_root(root_bytes,190,policy,root));
  for(size_t len:{size_t(0),size_t(158),size_t(189),size_t(191)})assert(!decode_root(root_bytes,len,policy,root));
  RootCache cache;assert(!cache.add_authenticated(root,false,1));RelayGrant relay;
  relay.issuer=4;relay.publisher=5;relay.root=0;relay.coordinator_boot=7;relay.root_permit_id=10;
  relay.expiry_ns=2000000000;relay.duration_ceil100us=20000;relay.flags=1;
  assert(!cache.relay_parent(relay,false,1));assert(!cache.relay_parent(relay,true,1)); // Cold replay cannot refresh.
  RootCache fresh;assert(fresh.add_authenticated(root,true,1));assert(fresh.relay_parent(relay,true,1));
  relay.expiry_ns=4000000000;assert(!fresh.relay_parent(relay,true,1));relay.expiry_ns=2000000000;
  relay.root_permit_id=11;assert(!fresh.relay_parent(relay,true,1));relay.root_permit_id=10;
  root.exact[20]^=1;assert(!fresh.add_authenticated(root,true,1));assert(!fresh.relay_parent(relay,true,1));
  std::puts("PASS lifecycle:4000 client windows,1000ms always-online,late/cancel/fault/overflow; bounded queues;190B grant scope/cold replay/expiry/ambiguity; unsupported capture/publication refuses");
}
