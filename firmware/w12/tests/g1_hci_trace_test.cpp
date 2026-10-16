#include <cassert>
#include <initializer_list>
#include <cstdint>
uint64_t fake_now=100;
#include "../main/g1_hci_trace.cpp"
unsigned cmd_calls=0,rx_calls=0;bool ack_inline=true,stall=false;
const void* expected_command=nullptr;void* expected_response=nullptr;void* expected_arg=nullptr;uint8_t* expected_event=nullptr;
extern "C" int __real_ble_hs_hci_rx_evt(uint8_t* data,void* arg){++rx_calls;assert(data==expected_event&&arg==expected_arg);return 37;}
extern "C" int __real_ble_hs_hci_cmd_tx(uint16_t op,const void* cmd,uint8_t n,void* response,uint8_t size){
  ++cmd_calls;assert(cmd==expected_command&&response==expected_response&&n==3&&size==2);
  if(ack_inline){uint8_t data[]={0x0e,4,1,uint8_t(op),uint8_t(op>>8),0};expected_event=data;assert(__wrap_ble_hs_hci_rx_evt(data,expected_arg)==37);}
  fake_now+=stall?2000000:500;return stall?19:0;
}
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
}
