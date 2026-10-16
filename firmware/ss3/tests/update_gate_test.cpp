#include "update_gate.hpp"
#include "lora_owner_gate.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
int main(){
  for(unsigned bits=1;bits<256;++bits){UpdateGate gate;assert(gate.begin(100));Services s;
    s.scan=bits&1;s.pending=bits&2;s.sync=bits&4;s.periodic=bits&8;s.discovery=bits&16;s.acl=bits&32;s.lora=bits&64;s.host=bits&128;
    gate.poll(101,s);assert(gate.stage==UpdateStage::Draining&&!gate.begin(102));
    s={};gate.poll(200,s);assert(gate.stage==UpdateStage::Ready&&gate.latched()&&!gate.begin(300));}
  UpdateGate g;assert(g.begin(100));g.poll(2000100,{});assert(g.stage==UpdateStage::Fault&&g.timeouts==1);
  g.poll(2000101,{});assert(g.timeouts==1&&!g.begin(3000100));
  g={};assert(g.begin(100));Services s;s.fault=true;g.poll(101,s);assert(g.stage==UpdateStage::Fault);
  g={};assert(!g.begin(UINT64_MAX));
  LoRaOwnerGate owner;assert(owner.drained()&&owner.idle_uninitialized(false)&&!owner.idle_uninitialized(true));
  assert(owner.request(false));assert(!owner.idle_uninitialized(false));
  owner.finish(owner.begin(),true,true);assert(owner.idle_uninitialized(false));
  assert(owner.request(true)&&!owner.idle_uninitialized(false));const auto consumed=owner.begin();
  assert(owner.request(false));owner.finish(consumed,true,true);assert(!owner.drained());
  g={};assert(g.begin(100));s={};s.lora=!owner.drained();g.poll(101,s);assert(g.stage==UpdateStage::Draining);
  owner.finish(owner.begin(),true,true);assert(owner.drained());s.lora=!owner.drained();g.poll(102,s);assert(g.stage==UpdateStage::Ready);
  owner={};owner.request(true);const auto stuck=owner.begin();owner.request(false);owner.finish(stuck,true,false);
  assert(!owner.drained()&&owner.busy&&owner.stuck);owner.finish(owner.begin(),true,true);assert(!owner.drained());
  std::puts("PASS update gate:255 active/pending service combinations,2s original deadline, nonrestartable latch, fault/timeout/overflow refusal; application ledger only, RF quiet UNKNOWN");
}
