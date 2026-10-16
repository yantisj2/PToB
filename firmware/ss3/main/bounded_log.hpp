#pragma once
#include "console_protocol.hpp"
#include <cstdint>
#include <cstdarg>
namespace v12 {
struct LogStatus {
  uint32_t dropped=0,truncated=0,isr_refused=0,peak=0,write_errors=0,expired=0,frame_drops=0;
  uint64_t transmitted=0,enqueue_work_us=0,suppressed_no_capture=0;uint32_t enqueue_max_us=0;
  uint64_t capture_id=0,capture_deadline_us=0,log_sequence=0;
  uint32_t base_dropped=0,base_truncated=0,base_isr=0,base_errors=0,base_expired=0,base_frames=0;
  bool capture_active=false,counter_exhausted=false;
};
// Only native_main's explicit response builder owns this object. Vendor/other
// threads use logv and cannot append into the control frame.
struct ConsoleFrame {
  // Reserve the fixed END buffer within the whole-frame contract before any
  // wire emission; an over-capacity builder is rejected as one complete job.
  // RC7 adds bounded admission/path counters. The maximum END is 785 bytes;
  // reserve 800 and retain the original whole-frame8192/response1.5s contract.
  char bytes[ConsoleFrameBytes-800]{};uint16_t length=0,rows=0;
  uint64_t nonce=0,ticket=0,request_us=0,deadline_us=0;LogStatus request_stats{};bool failed=false;
  bool format(const char* fmt,...) __attribute__((format(printf,2,3)));
};
void frame_begin(ConsoleFrame&,uint64_t nonce,uint64_t ticket,uint64_t request_us,
                 const char* factory,uint64_t boot,const char* source,const char* build);
bool frame_submit(const ConsoleFrame&);
bool capture_begin(uint64_t id,uint32_t duration_ms);
// Longer explicitly requested observer interval; fixed queues and per-record
// expiry remain unchanged. This grants no radio or clock authority.
bool capture_rolling_begin(uint64_t id,uint32_t duration_ms);
void capture_stop();
int logv(const char* format,va_list args);
int logf(const char* format,...);
// One trusted main-task G1 snapshot/10s may be visible outside capture. It
// shares original fixed queue/250ms expiry and cannot bypass priority frames.
int log_live(const char* snapshot);
bool log_start();
LogStatus log_status();
}
