#pragma once
#include "control_state.hpp"
#include <cstddef>
namespace v12 {
constexpr size_t HelloBytes=244,HelloBody=180,ConfirmBytes=72;
// Candidate R1 private128-bit UUIDs; canonical text, no SIG allocation claimed.
constexpr char GmServiceUuid[]="7e12b294-229f-4ee4-ad28-51812b5dd901";
constexpr char RelayServiceUuid[]="7e12b294-229f-4ee4-ad28-51812b5dd902";
constexpr uint8_t HelloDomain[]={0x50,0x54,0x4f,0x42,0x31,0x32,0x48,0x41,1};
constexpr uint8_t ConfirmDomain[]={0x50,0x54,0x4f,0x42,0x31,0x32,0x48,0x43,1};
struct AuthIdentity {
  uint8_t id=255,type=0,silicon[8]{},public_xy[64]{};
  // IDtype1: eight-byte little-endian NRF DEVICEID registers.
  // IDtype2: six factoryMAC bytes in display order, then two zero bytes.
};
struct AuthLocal {
  AuthIdentity identity;uint8_t private_d[32]{};
  uint64_t boot=0,train=0,challenge=0,root_permit=0;
};
class DirectAuth {
public:
  using Random=int(*)(void*,uint8_t*,size_t);
  using Clock=uint64_t(*)(void*);
  using LinkCheck=bool(*)(void*,uint64_t);
  AuthLocal local;AuthIdentity trusted_peer;LinkRole role=LinkRole::None;
  uint32_t config=0,registry_epoch=0;uint64_t started_us=0,expires_us=0;
  Clock clock=nullptr;void* clock_context=nullptr;uint64_t incarnation=0;
  LinkCheck check_link=nullptr;void* link_context=nullptr;
  uint8_t local_hello[HelloBytes]{},peer_hello[HelloBytes]{};
  uint8_t ephemeral[32]{},transcript[32]{},confirmation_key[32]{};
  uint8_t sender_key[16]{},peer_key[16]{};
  bool hello_ready=false,peer_verified=false,confirmed=false;
  // Crypto runs only in the bounded worker. Clock is mandatory, sampled both
  // before and after expensive operations; caller-supplied arrival times do
  // not prove completion within the handshake deadline.
  bool begin(Random rng,void* context);
  bool receive_hello(const uint8_t* bytes,size_t length,Random rng,void* context);
  bool confirmation(uint8_t out[ConfirmBytes]);
  bool receive_confirmation(const uint8_t* bytes,size_t length);
  // The native owner must check the actual connection descriptor/security and
  // incarnation. Completed sessions outlive the unfinished-handshake deadline.
  DirectBinding binding()const;
  bool live()const;
  void clear();
};
}
