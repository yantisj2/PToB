#pragma once
#include <cstddef>
#include <cstdint>
namespace ptob_l76k {
// Quectel L76K GNSS ProtocolSpecificationV1.1 section3.2.2.1 GET,
// zero payload. No SET/NVM/reset frame is accepted by this interface.
inline constexpr uint8_t GetPort[10]={0xba,0xce,0,0,6,0,0,0,6,0};
struct Result {bool flushed=false,connected=false,done=false,released=false;int written=0;};
template<class IO> Result get_port(IO& io){
 Result r{};r.flushed=io.flush();
 if(r.flushed){r.connected=io.connect();if(r.connected){r.written=io.write(GetPort,sizeof(GetPort));if(r.written==int(sizeof(GetPort)))r.done=io.wait(100);}}
 // One common restoration path, including failed/partial connect and write.
 r.released=io.release();return r;
}
}
