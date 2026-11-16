#include <cassert>
#include <initializer_list>
#include <cstdint>
uint64_t fake_now=100;
#define PTOB_W12_BLE_RECOVERY 1
#include "../main/g1_hci_trace.cpp"
unsigned cmd_calls=0,rx_calls=0;bool ack_inline=true,stall=false;
const void* expected_command=nullptr;void* expected_response=nullptr;void* expected_arg=nullptr;uint8_t* expected_event=nullptr;
extern "C" int __real_ble_hs_hci_rx_evt(uint8_t* data,void* arg){++rx_calls;assert(data==expected_event&&arg==expected_arg);return 37;}
extern "C" int __real_ble_hs_hci_cmd_tx(uint16_t op,const void* cmd,uint8_t n,void* response,uint8_t size){
  ++cmd_calls;assert(cmd==expected_command&&response==expected_response&&n==3&&size==2);
  if(ack_inline){uint8_t data[]={0x0e,4,1,uint8_t(op),uint8_t(op>>8),0};expected_event=data;assert(__wrap_ble_hs_hci_rx_evt(data,expected_arg)==37);}
  fake_now+=stall?2000000:500;return stall?19:0;
}
os_mbuf* expected_packet=nullptr;unsigned tx_acl_calls=0,rx_acl_calls=0;
extern "C" int __real_ble_transport_to_ll_acl_impl(os_mbuf* m){++tx_acl_calls;assert(m==expected_packet);if(m)m->length=0;fake_now+=75;return 31;}
extern "C" int __real_ble_transport_to_hs_acl_impl(os_mbuf* m){++rx_acl_calls;assert(m==expected_packet);if(m)m->length=0;return 27;}
int main(){
  uint8_t command[3]={},response[2]={};expected_command=command;expected_response=response;
  assert(__wrap_ble_hs_hci_cmd_tx(0x2045,command,3,response,2)==0);
  auto s=v12::g1_hci_status();assert(s.commands==1&&s.acks==1&&!s.pending_opcode&&!s.dropped&&s.max_us==500);
  v12::G1HciTrace r;assert(v12::g1_hci_pop(r)&&r.kind==1&&r.at_us==100&&r.opcode==0x2045);
  assert(v12::g1_hci_pop(r)&&r.kind==3&&r.event==0x0e&&r.result==0);assert(v12::g1_hci_pop(r)&&r.kind==2&&r.duration_us==500);assert(!v12::g1_hci_pop(r));
  ack_inline=false;stall=true;assert(__wrap_ble_hs_hci_cmd_tx(0x2045,command,3,response,2)==19);
  s=v12::g1_hci_status();assert(s.commands==2&&s.acks==1&&s.max_us==2000000);assert(v12::g1_hci_pop(r)&&r.kind==1);assert(v12::g1_hci_pop(r)&&r.kind==2&&r.result==19);
  uint8_t established[]={0x3e,4,0x0e,0,1,0},lost[]={0x3e,3,0x10,1,0},short_ack[]={0x0e,2,1,0};
  for(auto* data:{established,lost,short_ack}){expected_event=data;assert(__wrap_ble_hs_hci_rx_evt(data,expected_arg)==37);}
  assert(v12::g1_hci_status().sync_events==2);assert(v12::g1_hci_pop(r)&&r.kind==4&&r.event==0x0e);assert(v12::g1_hci_pop(r)&&r.event==0x10);assert(!v12::g1_hci_pop(r));
  uint8_t status_ack[]={0x0f,4,0x0c,1,0x45,0x20};expected_event=status_ack;assert(__wrap_ble_hs_hci_rx_evt(status_ack,expected_arg)==37);assert(v12::g1_hci_pop(r)&&r.kind==3&&r.result==0x0c);
  const auto before=v12::g1_hci_status();assert(__wrap_ble_hs_hci_cmd_tx(0x1234,command,3,response,2)==19);assert(v12::g1_hci_status().commands==before.commands&&!v12::g1_hci_pop(r));
  for(unsigned i=0;i<20;i++){expected_event=lost;assert(__wrap_ble_hs_hci_rx_evt(lost,expected_arg)==37);}
  assert(v12::g1_hci_status().dropped==4);unsigned records=0;while(v12::g1_hci_pop(r))++records;assert(records==16&&cmd_calls==3&&rx_calls==25);
  uint8_t credit[]={0x13,5,1,7,0,2,0};expected_event=credit;
  assert(__wrap_ble_hs_hci_rx_evt(credit,expected_arg)==37);
  assert(v12::g1_hci_pop(r)&&r.kind==5&&r.opcode==7&&r.result==2&&r.event==0x13);
  uint8_t short_credit[]={0x13,1,2};expected_event=short_credit;assert(__wrap_ble_hs_hci_rx_evt(short_credit,expected_arg)==37);assert(!v12::g1_hci_pop(r));
  uint8_t packet[]={7,0,7,0,3,0,4,0,0x0a,0x34,0x12};
  os_mbuf tail{packet+5,6,nullptr},first{packet,5,&tail};expected_packet=&first;
  assert(__wrap_ble_transport_to_ll_acl_impl(&first)==31&&tx_acl_calls==1);
  assert(v12::g1_hci_pop(r)&&r.kind==6&&r.opcode==7&&r.event==0x0a&&uint32_t(r.result)==((0x1234u<<16)|11));
  assert(v12::g1_hci_pop(r)&&r.kind==7&&r.result==31&&r.duration_us==75);assert(!v12::g1_hci_pop(r));
  first.length=5;packet[8]=0x52;assert(__wrap_ble_transport_to_hs_acl_impl(&first)==27&&rx_acl_calls==1);
  assert(v12::g1_hci_pop(r)&&r.kind==8&&r.event==0x52&&uint32_t(r.result)==((0x1234u<<16)|11));assert(!v12::g1_hci_pop(r));
  for(unsigned invalid=0;invalid<5;++invalid){
    first.length=5;tail.length=6;packet[1]=0;packet[2]=7;packet[6]=4;
    if(invalid==0)packet[1]=0x10; // Continuation has no new L2CAP/ATT header.
    if(invalid==1)packet[6]=5; // Other L2CAP traffic is forwarded unobserved.
    if(invalid==2)packet[2]=6; // Inconsistent advertised HCI length.
    if(invalid==3){first.length=4;tail.length=4;} // Short advertised header.
    if(invalid==4)packet[1]=0x30; // Reserved BLE packet boundary.
    assert(__wrap_ble_transport_to_ll_acl_impl(&first)==31);assert(!v12::g1_hci_pop(r));
  }
  assert(tx_acl_calls==6&&rx_acl_calls==1&&rx_calls==27);
}
