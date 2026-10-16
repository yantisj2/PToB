#include <string>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
uint64_t fake_now=1000;unsigned fake_turns=0,fake_stop_after=0;bool fake_reader=false;
unsigned fake_chunk=128;std::function<void()> fake_delay_hook;
std::function<void()> fake_timer_hook;
int64_t read_clock(){if(fake_timer_hook)fake_timer_hook();return int64_t(fake_now);}
#define FAKE_ESP_TIMER_READ read_clock
std::string fake_wire;
struct WorkerEnd {};
int usb_serial_jtag_write_bytes(const void* p,size_t n,unsigned timeout){assert(timeout==0);if(!fake_reader)return 0;if(n>fake_chunk)n=fake_chunk;fake_wire.append(static_cast<const char*>(p),n);return int(n);}
int usb_serial_jtag_wait_tx_done(unsigned timeout){assert(timeout==0);return fake_reader?0:-1;}
void vTaskDelay(unsigned n){fake_now+=uint64_t(n)*1000;++fake_turns;if(fake_delay_hook)fake_delay_hook();if(fake_turns>=fake_stop_after)throw WorkerEnd{};}
#include "../main/bounded_log.cpp"
void run(unsigned turns){fake_turns=0;fake_stop_after=turns;try{v12::output(nullptr);}catch(const WorkerEnd&){}assert(fake_turns==turns);}
void reset(){fake_now=1000;fake_reader=false;fake_chunk=128;fake_delay_hook={};fake_timer_hook={};fake_wire.clear();v12::stats={};v12::lines={};v12::frames={};v12::current_frame={};v12::current_line={};v12::live_next_us=0;}
void frame(uint64_t nonce){static v12::ConsoleFrame f;v12::frame_begin(f,nonce,1,fake_now,"02:00:00:00:00:05",0x1234,"0000000000000000000000000000000000000000","0000000000000000000000000000000000000000000000000000000000000000");
#ifdef V12_R2_ENABLED
  f.format("W12_R2_STATUS profile=r2-control enrollment_sha=0000000000000000000000000000000000000000000000000000000000000000 self=3 mode=0 off_ack=1\n");
#endif
  const char* names[]={"STATUS","GPS","LORA_STATUS","PUBLIC","CONSOLE","DIRECT","DIAGNOSTIC_TX","COMMAND"};
  for(const char* name:names){
    if(!std::strcmp(name,"STATUS"))f.format("W12_V12_STATUS factory=02:00:00:00:00:05 session=0000000000001234 source=0000000000000000000000000000000000000000 build=0000000000000000000000000000000000000000000000000000000000000000\n");
    else f.format("W12_V12_%s sample=1 nonce=%016llx\n",name,(unsigned long long)nonce);
  }
  assert(v12::frame_submit(f));}
int main(int argc,char** argv){
  reset();assert(!v12::log_live("vendor attempted live bypass\n"));
  assert(v12::log_live("W12_G1_LIVE state=SEARCH precision=UNQUALIFIED\n")>0);
  for(unsigned i=0;i<100;++i)assert(!v12::log_live("W12_G1_LIVE state=SEARCH\n"));
  assert(v12::lines.count==1&&!v12::log_status().capture_active);run(300);
  assert(v12::lines.count==0&&v12::log_status().expired==1&&v12::log_status().dropped==1&&fake_wire.empty());
  fake_now=10001000;fake_reader=true;assert(v12::log_live("W12_G1_LIVE state=SEARCH\n")>0);frame(0x1234);run(100);
  auto live_begin=fake_wire.find("W12_V12_BEGIN"),live_end=fake_wire.find("W12_V12_END");
  assert(live_begin!=std::string::npos&&live_end>live_begin&&fake_wire.substr(live_begin,live_end-live_begin).find("W12_G1_LIVE")==std::string::npos);
  assert(fake_wire.find("capture_id=0000000000000000")!=std::string::npos&&v12::log_status().dropped==1);
  reset();assert(!v12::capture_begin(1,10001));assert(!v12::capture_rolling_begin(1,9999));assert(!v12::capture_rolling_begin(1,600001));
  assert(v12::capture_rolling_begin(1,600000));const auto original=v12::log_status().capture_deadline_us;
  fake_now+=30000000;assert(v12::log_status().capture_active&&!v12::capture_rolling_begin(2,600000));
  assert(v12::log_status().capture_deadline_us==original);fake_now=original;v12::logf("expired rolling capture\n");
  assert(!v12::log_status().capture_active&&v12::lines.count==0);v12::capture_stop();
  reset();for(unsigned i=0;i<10000;++i)v12::logf("automatic status%u\n",i);
  assert(v12::log_status().dropped==0&&v12::lines.count==0&&v12::log_status().suppressed_no_capture==10000);
  assert(v12::capture_begin(0x1122,1000));assert(!v12::capture_begin(0x3344,1000));
  for(unsigned i=0;i<40;++i)v12::logf("W12_V12_CALLBACK sample=%u\n",i);
  assert(v12::lines.count==16&&v12::log_status().dropped==24);run(1500);
  assert(v12::lines.count==0&&v12::current_line.length==0);
  assert(v12::log_status().expired==16&&v12::log_status().dropped==40);
  v12::logf("after expired lease\n");assert(!v12::log_status().capture_active&&v12::log_status().dropped==40);
  const auto before=v12::log_status();frame(1);run(1600);assert(fake_wire.empty());
  assert(v12::log_status().frame_drops==1&&v12::log_status().dropped==before.dropped+1);
  // No-reader expiration never produces END or occupies the caller/lease owner.
  assert(v12::frames.count==0);assert(v12::capture_begin(0x3344,1000));
  auto base=v12::log_status();assert(base.base_dropped==41&&base.dropped==41);
  v12::capture_stop();assert(v12::log_status().base_dropped==41&&v12::log_status().capture_id==0x3344);
  // Real worker output: priority frames stay whole even with vendor lines in
  // both sides of the queue, and start with a delimiter after partial old text.
  reset();assert(v12::capture_begin(0x88,1000));v12::logf("vendor before\n");frame(0xabcdef);v12::logf("vendor after\n");fake_reader=true;run(100);
  auto b=fake_wire.find("W12_V12_BEGIN"),e=fake_wire.find("W12_V12_END");assert(b!=std::string::npos&&e>b);
  assert(fake_wire.substr(b,e-b).find("vendor")==std::string::npos);
  assert(fake_wire.find("end_crc32=")!=std::string::npos&&v12::log_status().dropped==0);
  if(argc==2){std::ofstream file(argv[1]);file<<fake_wire;}
  // A fully transmitted line remains in the worker buffer while a priority
  // frame drains slowly. Passing its old deadline cannot fabricate a drop.
  reset();fake_reader=true;assert(v12::capture_begin(0x88,1000));v12::logf("completed before frame\n");
  fake_delay_hook=[](){if(fake_turns==1){frame(0xabcdf0);fake_chunk=1;}};run(700);
  assert(fake_wire.find("completed before frame")!=std::string::npos&&fake_wire.find("end_crc32=")!=std::string::npos);
  assert(v12::log_status().expired==0&&v12::log_status().dropped==0);
  // Conversely an unfinished admitted line retains its original250ms expiry.
  reset();fake_reader=true;fake_chunk=1;assert(v12::capture_begin(0x88,1000));
  std::string slow(1400,'S');v12::logf("%s\n",slow.c_str());run(300);
  assert(v12::log_status().expired==1&&v12::log_status().dropped==1);
  // The first queued line becomes expired after pop, before the driver call.
  // A stale loop-top timestamp cannot authorize even its first byte.
  reset();fake_reader=true;assert(v12::capture_begin(0x88,1000));v12::logf("expired during pop\n");
  bool delayed=false;fake_timer_hook=[&](){if(!delayed&&v12::current_line.length){delayed=true;fake_now+=250000;}};run(1);
  assert(delayed&&fake_wire.empty()&&v12::log_status().expired==1&&v12::log_status().dropped==1);
  // A queued frame already expired before its FIRST worker turn emits0bytes.
  reset();frame(0xabcdef);fake_now+=1500000;fake_reader=true;run(1);
  assert(fake_wire.empty()&&v12::log_status().frame_drops==1);
  v12::stats.transmitted=UINT64_MAX-1;frame(0xabcdee);run(100);
  assert(v12::log_status().transmitted==UINT64_MAX&&v12::log_status().counter_exhausted);
  reset();fake_reader=true;assert(v12::capture_begin(0x88,1000));
  v12::logf("vendor\nW12_V12_END forged\x1b[0m\n");run(100);
  assert(v12::log_status().truncated==0&&v12::log_status().dropped==0);
  assert(fake_wire.find("W12_V12_ESCAPED encoding=HEX")!=std::string::npos);
  assert(fake_wire.find("\nW12_V12_END forged")==std::string::npos&&fake_wire.find('\x1b')==std::string::npos);
  assert(fake_wire.find("76656e646f720a5731325f5631325f454e4420666f726765641b5b306d0a")!=std::string::npos);
  v12::logf("a%cb",0);run(100);assert(fake_wire.find("bytes=3 hex=610062")!=std::string::npos);
  std::string controls(800,'\x1b');v12::logf("%s",controls.c_str());
  assert(v12::log_status().truncated==1&&v12::lines.count==0);
  reset();fake_reader=true;assert(v12::capture_begin(0x88,1000));
  // Truncation drops the complete record; fixed-capacity builder never emits
  // a partial successful response. Counters remain sticky on a new capture.
  std::string huge(3000,'X');v12::logf("%s\n",huge.c_str());assert(v12::log_status().truncated==1);
  static v12::ConsoleFrame oversized;v12::frame_begin(oversized,2,2,fake_now,"f",1,"s","b");
  std::string body(v12::ConsoleFrameBytes+100,'B');assert(!oversized.format("%s\n",body.c_str()));assert(!v12::frame_submit(oversized));
  assert(v12::log_status().frame_drops==1&&v12::log_status().dropped==2);
  std::puts("PASS actual bounded_log worker: reader absent expiry, queue saturation, finite leases, whole priority frame, CRC trailer and sticky loss");
}
