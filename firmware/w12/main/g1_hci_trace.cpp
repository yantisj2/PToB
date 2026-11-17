// Read-only ordinary HCI boundary observations. Original SDK functions run
// exactly once with identical arguments/return. No HCI injection, owner change,
// timer change, controller access, formatting or allocation in these wrappers.
#include "g1_hci_trace.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#if !defined(PTOB_N30) && defined(PTOB_W12_BLE_RECOVERY)
#include "os/os_mbuf.h"
#endif
namespace v12 {
namespace {
portMUX_TYPE hci_mux=portMUX_INITIALIZER_UNLOCKED;
G1HciTrace rows[16]{};unsigned head=0,tail=0;uint32_t sequence=0;
G1HciStatus status;
uint64_t now(){const auto n=esp_timer_get_time();return n>0?uint64_t(n):0;}
void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
bool selected(uint16_t op){return (op>=0x2044&&op<=0x2046)
#if !defined(PTOB_N30) && defined(PTOB_W12_BLE_RECOVERY)
  ||op==0x2037 // CAP advertising data: header/status only.
#endif
  ;}
#if !defined(PTOB_N30) && defined(PTOB_W12_BLE_RECOVERY)
struct AttHeader {uint16_t handle=0;uint8_t op=0;uint32_t detail=0;bool valid=false;};
AttHeader att_header(const os_mbuf* packet){
  AttHeader h;uint8_t b[11]{};const unsigned n=packet?OS_MBUF_PKTLEN(packet):0;
  if(n<9||os_mbuf_copydata(packet,0,n<sizeof b?n:sizeof b,b))return h;
  const uint16_t flags=uint16_t(b[0])|uint16_t(b[1])<<8;
  const unsigned payload=unsigned(b[2])|unsigned(b[3])<<8;
  const unsigned l2cap=unsigned(b[4])|unsigned(b[5])<<8;
  if((flags&0xc000)||(((flags>>12)&3)==1||((flags>>12)&3)==3)||payload!=n-4||payload<5||
     b[6]!=4||b[7]||!l2cap||l2cap>payload-4)return h;
  h.handle=flags&0x0fff;h.op=b[8];h.detail=n;
  // Never retain values, JOIN data or keys. Only relevant ATT attribute ids.
  if(n>=11&&(h.op==0x08||h.op==0x0a||h.op==0x12||h.op==0x52))
    h.detail|=(uint32_t(b[9])|uint32_t(b[10])<<8)<<16;
  h.valid=true;return h;
}
#endif
void lock(bool isr){if(isr)portENTER_CRITICAL_ISR(&hci_mux);else portENTER_CRITICAL(&hci_mux);}
void unlock(bool isr){if(isr)portEXIT_CRITICAL_ISR(&hci_mux);else portEXIT_CRITICAL(&hci_mux);}
void append(uint8_t kind,uint16_t op,int32_t result,uint8_t event,uint64_t at,uint32_t duration=0){
  const bool isr=xPortInIsrContext();lock(isr);inc(sequence);
  if(head-tail>=16)inc(status.dropped);else{rows[head%16]={at,sequence,duration,result,op,kind,event};++head;}
  if(kind==1){inc(status.commands);status.pending_opcode=op;status.pending_at_us=at;}
  if(kind==2){status.pending_opcode=0;status.pending_at_us=0;if(duration>status.max_us)status.max_us=duration;}
  if(kind==3){inc(status.acks);}
  if(kind==4){inc(status.sync_events);}
  unlock(isr);
}
}
bool g1_hci_pop(G1HciTrace& out){const bool isr=xPortInIsrContext();lock(isr);const bool have=head!=tail;if(have)out=rows[(tail++)%16];unlock(isr);return have;}
G1HciStatus g1_hci_status(){const bool isr=xPortInIsrContext();lock(isr);const auto result=status;unlock(isr);return result;}
}
extern "C" int __real_ble_hs_hci_cmd_tx(uint16_t,const void*,uint8_t,void*,uint8_t);
extern "C" int __real_ble_hs_hci_rx_evt(uint8_t*,void*);
extern "C" int __wrap_ble_hs_hci_cmd_tx(uint16_t op,const void* cmd,uint8_t length,void* response,uint8_t response_length){
  const bool observe=v12::selected(op);const auto at=observe?v12::now():0;
  if(observe)v12::append(1,op,0,0,at);
  const int rc=__real_ble_hs_hci_cmd_tx(op,cmd,length,response,response_length);
  if(observe){const auto done=v12::now();const auto span=done>=at?done-at:UINT64_MAX;v12::append(2,op,rc,0,done,span>UINT32_MAX?UINT32_MAX:uint32_t(span));}
  return rc;
}
extern "C" int __wrap_ble_hs_hci_rx_evt(uint8_t* data,void* arg){
  // SDK supplies its validated allocated HCI event header plus length bytes.
  // Do not inspect bytes beyond that advertised length or mutate/free data.
  if(data){const unsigned len=data[1];uint16_t op=0;int result=0;bool ack=false;
    if(data[0]==0x0e&&len>=4){op=uint16_t(data[3])|uint16_t(data[4])<<8;result=data[5];ack=true;}
    else if(data[0]==0x0f&&len>=4){op=uint16_t(data[4])|uint16_t(data[5])<<8;result=data[2];ack=true;}
    if(ack&&v12::selected(op))v12::append(3,op,result,data[0],v12::now());
    else if(data[0]==0x3e&&len>=3&&((data[2]==0x0e&&len>=4)||data[2]==0x10))
      v12::append(4,0,data[2]==0x0e?data[3]:0,data[2],v12::now());
  }
#if !defined(PTOB_N30) && defined(PTOB_W12_BLE_RECOVERY)
  if(data&&data[0]==0x13&&data[1]>=1&&unsigned(data[2])*4+1<=data[1]){
    // Connection budget is four. Bound observations, including malformed SDK
    // event tests; forwarding remains exactly once for the original event.
    for(unsigned i=0;i<data[2]&&i<4;++i){const auto* p=data+3+4*i;
      v12::append(5,uint16_t(p[0])|uint16_t(p[1])<<8,int(p[2])|(int(p[3])<<8),0x13,v12::now());}
  }
#endif
  return __real_ble_hs_hci_rx_evt(data,arg);
}

#if !defined(PTOB_N30) && defined(PTOB_W12_BLE_RECOVERY)
extern "C" int __real_ble_transport_to_ll_acl_impl(os_mbuf*);
extern "C" int __real_ble_transport_to_hs_acl_impl(os_mbuf*);
extern "C" int __wrap_ble_transport_to_ll_acl_impl(os_mbuf* packet){
  const auto h=v12::att_header(packet);const auto at=h.valid?v12::now():0;
  if(h.valid)v12::append(6,h.handle,int32_t(h.detail),h.op,at);
  const int rc=__real_ble_transport_to_ll_acl_impl(packet);
  // SDK owns the mbuf now; use only the frozen header after forwarding.
  if(h.valid){const auto span=v12::now()-at;v12::append(7,h.handle,rc,h.op,v12::now(),span>UINT32_MAX?UINT32_MAX:uint32_t(span));}
  return rc;
}
extern "C" int __wrap_ble_transport_to_hs_acl_impl(os_mbuf* packet){
  const auto h=v12::att_header(packet);
  if(h.valid)v12::append(8,h.handle,int32_t(h.detail),h.op,v12::now());
  return __real_ble_transport_to_hs_acl_impl(packet);
}
#endif
