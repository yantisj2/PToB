#pragma once
#include <cstdint>
#include <cstddef>
namespace v12 {
constexpr uint64_t ConsoleTicketUs=2000000,ConsoleFrameUs=1500000,ConsoleLineUs=250000;
#ifdef V12_R2_ENABLED
#ifdef V12_SS3
constexpr unsigned ConsoleFrameBytes=9216,ConsoleResponseRows=10,ConsoleLines=16;
#else
constexpr unsigned ConsoleFrameBytes=8704,ConsoleResponseRows=9,ConsoleLines=16;
#endif
#else
constexpr unsigned ConsoleFrameBytes=6144,ConsoleResponseRows=8,ConsoleLines=16;
#endif
inline uint32_t console_crc(const void* data,size_t bytes,uint32_t crc=0){
  const auto* p=static_cast<const uint8_t*>(data);crc=~crc;
  for(size_t i=0;i<bytes;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}
  return ~crc;
}
inline bool hex_id(const char* s,uint64_t& id){
  if(!s)return false;
  id=0;
  for(unsigned i=0;i<16;++i){const char c=s[i];if(!c)return false;
    const unsigned n=c>='0'&&c<='9'?unsigned(c-'0'):c>='a'&&c<='f'?unsigned(c-'a'+10):16;
    if(n==16)return false;
    id=(id<<4)|n;}
  return !s[16]&&id;
}
// A readonly request issues one boot-local, one-use ticket. A queued mutation
// cannot start a fresh lease after its original ticket has expired/been used.
struct ConsoleTicket {
  uint64_t generation=0,issued_us=0,deadline_us=0;bool available=false,exhausted=false;
  uint64_t issue(uint64_t now){
    available=false;if(exhausted||generation==UINT64_MAX||now>UINT64_MAX-ConsoleTicketUs){exhausted=true;return 0;}
    ++generation;issued_us=now;deadline_us=now+ConsoleTicketUs;available=true;return generation;
  }
  bool consume(uint64_t ticket,uint64_t now){
    if(!available||exhausted||ticket!=generation||now<issued_us||now>=deadline_us)return false;
    available=false;return true;
  }
};
// Shared executable ledger for original admission, queued owner work and stop.
// It is an application ledger only; controller completion is not RF metrology.
struct OwnerLease {
  uint32_t generation=0,stop_generation=0,stop_completed=0,stop_for_generation=0;
  uint64_t request_id=0,admitted_us=0,deadline_us=0,started_us=0,completed_us=0;
  bool pending=false,busy=false,deadline_exceeded=false,exhausted=false;
  bool admit(uint64_t now,uint32_t ms,uint64_t id){
    if(pending||busy||stop_generation!=stop_completed||exhausted||generation==UINT32_MAX||ms<1000||ms>10000||now>UINT64_MAX-uint64_t(ms)*1000)return false;
    ++generation;request_id=id;admitted_us=now;deadline_us=now+uint64_t(ms)*1000;
    started_us=completed_us=0;pending=true;return true;
  }
  bool start(uint64_t now){
    if(!pending||busy||completed_us)return false;
    pending=false;if(now>=deadline_us||now<admitted_us){deadline_exceeded=true;return false;}
    busy=true;started_us=now;return true;}
  void stop_request(){pending=false;stop_for_generation=generation;if(stop_generation==UINT32_MAX)exhausted=true;else ++stop_generation;}
  bool observe(uint64_t now,uint64_t stop_margin_us=5000){
    const bool active=generation&&!completed_us&&(pending||busy||stop_generation!=stop_completed);
    if(!active)return false;
    if(now>=deadline_us)deadline_exceeded=true;
    if(stop_for_generation!=generation&&now>=deadline_us-stop_margin_us){stop_request();return true;}
    return false;
  }
  void stopped(uint64_t now){
    const bool original=busy||(generation&&!completed_us);busy=pending=false;stop_completed=stop_generation;
    if(original){completed_us=now;if(started_us&&now>deadline_us)deadline_exceeded=true;}
  }
};
}
