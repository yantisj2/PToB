#include "bounded_log.hpp"
#include "client_lifecycle.hpp"
#include "n30_console.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <cinttypes>
namespace v12 {
namespace {
struct Line {uint16_t length=0;uint64_t deadline=0;char bytes[1536]{};};
Ring<Line,ConsoleLines> lines;
Ring<ConsoleFrame,2> frames;
portMUX_TYPE log_mux=portMUX_INITIALIZER_UNLOCKED;
LogStatus stats;
TaskHandle_t log_task=nullptr;
uint64_t live_next_us=0;
uint64_t now_us(){const auto n=esp_timer_get_time();return n>0?uint64_t(n):0;}
void increment(uint32_t& n){if(n!=UINT32_MAX)++n;else stats.counter_exhausted=true;}
void add(uint64_t& n,uint64_t amount){if(amount>UINT64_MAX-n){n=UINT64_MAX;stats.counter_exhausted=true;}else n+=amount;}
void frame_drop(){increment(stats.frame_drops);increment(stats.dropped);}
void line_drop(bool expired=false){increment(stats.dropped);if(expired)increment(stats.expired);}
// Static buffers keep the finite console task's stack independent of frame size.
ConsoleFrame current_frame;
Line current_line;
char trailer[800]{};
void output(void*){
  size_t offset=0,line_offset=0,end_offset=0,end_length=0;bool frame_active=false,ending=false;
  while(true){
    for(unsigned budget=0;budget<4;++budget){
      const uint64_t now=now_us();
      if(frame_active&&now>=current_frame.deadline_us){
        portENTER_CRITICAL(&log_mux);frame_drop();portEXIT_CRITICAL(&log_mux);
        frame_active=ending=false;offset=end_offset=end_length=0;
      }
      if(current_line.length&&line_offset<current_line.length&&now>=current_line.deadline){
        portENTER_CRITICAL(&log_mux);line_drop(true);portEXIT_CRITICAL(&log_mux);current_line.length=0;line_offset=0;
      }
      // Finish a currently admitted line (<=250ms original expiry) before
      // beginning a priority response. Once BEGIN starts no other output runs.
      if(!frame_active&&line_offset==current_line.length){
        portENTER_CRITICAL(&log_mux);frame_active=frames.pop(current_frame);portEXIT_CRITICAL(&log_mux);
        if(frame_active){offset=end_offset=end_length=0;ending=false;
          if(now_us()>=current_frame.deadline_us){portENTER_CRITICAL(&log_mux);frame_drop();portEXIT_CRITICAL(&log_mux);frame_active=false;continue;}
        }
      }
      if(frame_active){
        // No ordinary/vendor line is emitted between BEGIN and END. Before END,
        // use the supported zero-timeout driver drain query; never block callers.
        if(offset==current_frame.length&&!ending){
          if(n30_console::drained()!=ESP_OK)break;
          const auto s=log_status();const uint64_t finished=now_us();
          if(finished>=current_frame.deadline_us)continue;
          const auto& base=current_frame.request_stats;
          const int n=std::snprintf(trailer,sizeof(trailer),
            "W12_V12_END version=3 nonce=%016" PRIx64 " ticket=%016" PRIx64 " rows=%u crc32=%08" PRIx32
            " request_us=%" PRIu64 " completed_us=%" PRIu64 " deadline_us=%" PRIu64 " body_driver_drained=1"
            " capture_id=%016" PRIx64 " capture_active=%u capture_deadline_us=%" PRIu64
            " dropped=%" PRIu32 " truncated=%" PRIu32 " isr_refused=%" PRIu32 " write_errors=%" PRIu32 " expired=%" PRIu32 " frame_drops=%" PRIu32
            " base_dropped=%" PRIu32 " base_truncated=%" PRIu32 " base_isr=%" PRIu32 " base_errors=%" PRIu32 " base_expired=%" PRIu32 " base_frames=%" PRIu32
            " req_dropped=%" PRIu32 " req_truncated=%" PRIu32 " req_isr_refused=%" PRIu32 " req_write_errors=%" PRIu32 " req_expired=%" PRIu32 " req_frame_drops=%" PRIu32
            " counter_exhausted=%u RF_quiet=UNKNOWN",
            current_frame.nonce,current_frame.ticket,unsigned(current_frame.rows),console_crc(current_frame.bytes,current_frame.length),
            current_frame.request_us,finished,current_frame.deadline_us,s.capture_id,unsigned(s.capture_active),s.capture_deadline_us,
            s.dropped,s.truncated,s.isr_refused,s.write_errors,s.expired,s.frame_drops,
            s.base_dropped,s.base_truncated,s.base_isr,s.base_errors,s.base_expired,s.base_frames,base.dropped,base.truncated,base.isr_refused,base.write_errors,base.expired,base.frame_drops,unsigned(s.counter_exhausted));
          if(n<=0||n>=int(sizeof(trailer))){portENTER_CRITICAL(&log_mux);frame_drop();portEXIT_CRITICAL(&log_mux);frame_active=false;break;}
          const uint32_t full_crc=console_crc(trailer,size_t(n),console_crc(current_frame.bytes,current_frame.length));
          const int tail=std::snprintf(trailer+n,sizeof(trailer)-unsigned(n)," end_crc32=%08" PRIx32 "\n",full_crc);
          if(tail<=0||unsigned(tail)>=sizeof(trailer)-unsigned(n)){portENTER_CRITICAL(&log_mux);frame_drop();portEXIT_CRITICAL(&log_mux);frame_active=false;break;}
          end_length=size_t(n+tail);
          if(size_t(current_frame.length)+end_length>ConsoleFrameBytes){portENTER_CRITICAL(&log_mux);frame_drop();portEXIT_CRITICAL(&log_mux);frame_active=false;break;}
          ending=true;
        }
        const char* data=ending?trailer:current_frame.bytes;
        size_t& at=ending?end_offset:offset;const size_t length=ending?end_length:current_frame.length;
        const size_t chunk=length-at>128?128:length-at;
        if(!chunk)break;
        if(now_us()>=current_frame.deadline_us){portENTER_CRITICAL(&log_mux);frame_drop();portEXIT_CRITICAL(&log_mux);frame_active=false;continue;}
        const int written=n30_console::write(data+at,chunk);
        if(written<=0){if(written<0){portENTER_CRITICAL(&log_mux);increment(stats.write_errors);portEXIT_CRITICAL(&log_mux);}break;}
        at+=size_t(written);portENTER_CRITICAL(&log_mux);add(stats.transmitted,unsigned(written));portEXIT_CRITICAL(&log_mux);
        if(ending&&end_offset==end_length){frame_active=ending=false;offset=end_offset=end_length=0;}
        continue;
      }
      if(current_line.length&&line_offset<current_line.length&&now>=current_line.deadline){
        portENTER_CRITICAL(&log_mux);line_drop(true);portEXIT_CRITICAL(&log_mux);current_line.length=0;line_offset=0;
      }
      if(line_offset==current_line.length){
        portENTER_CRITICAL(&log_mux);const bool have=lines.pop(current_line);portEXIT_CRITICAL(&log_mux);
        line_offset=0;if(!have){current_line.length=0;break;}
        if(now>=current_line.deadline)continue;
      }
      const size_t remain=current_line.length-line_offset;const size_t chunk=remain>128?128:remain;
      // Queue/critical work may consume the old loop timestamp. Validate the
      // original enqueue deadline again immediately before the driver call.
      if(now_us()>=current_line.deadline){
        portENTER_CRITICAL(&log_mux);line_drop(true);portEXIT_CRITICAL(&log_mux);current_line.length=0;line_offset=0;continue;
      }
      const int written=n30_console::write(current_line.bytes+line_offset,chunk);
      if(written<=0){if(written<0){portENTER_CRITICAL(&log_mux);increment(stats.write_errors);portEXIT_CRITICAL(&log_mux);}break;}
      line_offset+=size_t(written);portENTER_CRITICAL(&log_mux);add(stats.transmitted,unsigned(written));portEXIT_CRITICAL(&log_mux);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
}
bool ConsoleFrame::format(const char* fmt,...){
  if(failed)return false;
  va_list args;va_start(args,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);
  if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}
  for(int i=0;i<n;++i)if(bytes[length+i]=='\n')++rows;
  length+=uint16_t(n);return true;
}
void frame_begin(ConsoleFrame& f,uint64_t nonce,uint64_t ticket,uint64_t now,const char* factory,uint64_t boot,const char* source,const char* build){
  f.length=f.rows=0;f.failed=false;f.nonce=nonce;f.ticket=ticket;f.request_us=now;f.deadline_us=now+ConsoleFrameUs;f.request_stats=log_status();
  // The leading delimiter discards a prior expired partial wire line.
  f.format("\nW12_V12_BEGIN version=3 nonce=%016" PRIx64 " ticket=%016" PRIx64 " factory=%s session=%016" PRIx64
    " source=%s build=%s request_us=%" PRIu64 " deadline_us=%" PRIu64 " ticket_deadline_us=%" PRIu64 "\n",
    nonce,ticket,factory,boot,source,build,now,f.deadline_us,now+ConsoleTicketUs);
  f.rows=0;
}
bool frame_submit(const ConsoleFrame& f){
  portENTER_CRITICAL(&log_mux);
  const bool accepted=!f.failed&&f.rows==ConsoleResponseRows&&now_us()<f.deadline_us&&frames.push(f);
  if(!accepted)frame_drop();
  portEXIT_CRITICAL(&log_mux);return accepted;
}
static bool capture_admit(uint64_t id,uint32_t duration,uint32_t maximum){
  const auto now=now_us();portENTER_CRITICAL(&log_mux);
  const bool active=stats.capture_active&&now<stats.capture_deadline_us;
  const bool ok=id&&duration>=1000&&duration<=maximum&&!active&&!stats.counter_exhausted&&now<=UINT64_MAX-uint64_t(duration)*1000;
  if(ok){stats.capture_id=id;stats.capture_deadline_us=now+uint64_t(duration)*1000;stats.capture_active=true;
    stats.base_dropped=stats.dropped;stats.base_truncated=stats.truncated;stats.base_isr=stats.isr_refused;
    stats.base_errors=stats.write_errors;stats.base_expired=stats.expired;stats.base_frames=stats.frame_drops;}
  portEXIT_CRITICAL(&log_mux);return ok;
}
bool capture_begin(uint64_t id,uint32_t duration){return capture_admit(id,duration,10000);}
bool capture_rolling_begin(uint64_t id,uint32_t duration){return duration>=10000&&capture_admit(id,duration,600000);}
void capture_stop(){portENTER_CRITICAL(&log_mux);stats.capture_active=false;portEXIT_CRITICAL(&log_mux);}
static int logv_impl(const char* format,va_list args,bool live){
  if(xPortInIsrContext()){portENTER_CRITICAL_ISR(&log_mux);increment(stats.isr_refused);portEXIT_CRITICAL_ISR(&log_mux);return 0;}
  const uint64_t begin=now_us();uint64_t id=0,seq=0,deadline=0;
  portENTER_CRITICAL(&log_mux);
  if(live){if(begin<live_next_us){portEXIT_CRITICAL(&log_mux);return 0;}
    if(begin>UINT64_MAX-10000000){stats.counter_exhausted=true;portEXIT_CRITICAL(&log_mux);return 0;}live_next_us=begin+10000000;}
  if(!stats.capture_active||begin>=stats.capture_deadline_us){
    stats.capture_active=false;
    if(!live){if(stats.suppressed_no_capture!=UINT64_MAX)++stats.suppressed_no_capture;else stats.counter_exhausted=true;
      portEXIT_CRITICAL(&log_mux);return 0;}
    // Capture id0 explicitly denotes a live snapshot, not a retained interval.
    deadline=begin+ConsoleLineUs;
  }else{
    id=stats.capture_id;deadline=stats.capture_deadline_us;
  }
  if(stats.log_sequence==UINT64_MAX){stats.counter_exhausted=true;portEXIT_CRITICAL(&log_mux);return 0;}
  seq=++stats.log_sequence;portEXIT_CRITICAL(&log_mux);
  Line line;line.deadline=begin+ConsoleLineUs<deadline?begin+ConsoleLineUs:deadline;
  const int prefix=std::snprintf(line.bytes,sizeof(line.bytes),"\nW12_V12_LOG capture_id=%016" PRIx64 " log_seq=%" PRIu64 " enqueue_us=%" PRIu64 " record=",id,seq,begin);
  if(prefix<=0||prefix>=int(sizeof(line.bytes)))return 0;
  const int wanted=std::vsnprintf(line.bytes+prefix,sizeof(line.bytes)-unsigned(prefix),format,args);
  if(wanted<=0)return wanted;
  bool malformed=false;
  bool truncated=wanted>=int(sizeof(line.bytes)-unsigned(prefix));
  if(!truncated){line.length=uint16_t(unsigned(prefix)+unsigned(wanted));
    // A normal record occupies one line. Embedded newlines/terminal bytes do
    // not become fabricated control records; reject the complete record.
    for(unsigned i=unsigned(prefix);i<line.length;++i){const unsigned char c=line.bytes[i];if((c=='\n'&&i+1!=line.length)||(c<32&&c!='\n'&&c!='\r'&&c!='\t'))malformed=true;}
    if(malformed){
      // Preserve vendor multiline/control-byte diagnostics as one explicit hex
      // record. Do not let terminal bytes forge a control frame, and do not
      // silently discard the failure message that triggered the observation.
      char header[80];const int n=std::snprintf(header,sizeof header,"W12_V12_ESCAPED encoding=HEX bytes=%d hex=",wanted);
      const size_t length=unsigned(prefix)+unsigned(n)+2*unsigned(wanted)+1;
      if(n<=0||n>=int(sizeof header)||length>=sizeof line.bytes)truncated=true;
      else{
        static constexpr char digits[]="0123456789abcdef";
        for(int i=wanted-1;i>=0;--i){const auto c=static_cast<unsigned char>(line.bytes[prefix+i]);
          line.bytes[prefix+n+2*i]=digits[c>>4];line.bytes[prefix+n+2*i+1]=digits[c&15];}
        std::memcpy(line.bytes+prefix,header,unsigned(n));line.length=uint16_t(length);
        line.bytes[line.length-1]='\n';malformed=false;
      }
    }
    if(!truncated&&line.bytes[line.length-1]!='\n'&&line.length+1<sizeof(line.bytes))line.bytes[line.length++]='\n';
  }
  portENTER_CRITICAL(&log_mux);
  bool accepted=false;
  if(truncated||malformed){increment(stats.truncated);line_drop();}
  else if(now_us()>=line.deadline){line_drop(true);}
  else if(!(accepted=lines.push(line))){line_drop();}
  stats.peak=lines.peak;const uint64_t work=now_us()-begin;add(stats.enqueue_work_us,work);
  if(work>stats.enqueue_max_us)stats.enqueue_max_us=work>UINT32_MAX?UINT32_MAX:uint32_t(work);
  portEXIT_CRITICAL(&log_mux);return accepted?int(line.length):0;
}
int logv(const char* format,va_list args){return logv_impl(format,args,false);}
int logf(const char* format,...){va_list args;va_start(args,format);const int n=logv(format,args);va_end(args);return n;}
static int live_format(const char* format,...){va_list args;va_start(args,format);const int n=logv_impl(format,args,true);va_end(args);return n;}
int log_live(const char* snapshot){
  if(xPortInIsrContext()){portENTER_CRITICAL_ISR(&log_mux);increment(stats.isr_refused);portEXIT_CRITICAL_ISR(&log_mux);return 0;}
  if(!snapshot)return 0;
  unsigned length=0;for(;length<768&&snapshot[length];++length){}
  if(length>=768||length<12||std::strncmp(snapshot,"W12_G1_LIVE ",12))return 0;
  return live_format("%s",snapshot);
}
bool log_start(){esp_log_set_vprintf(logv);return xTaskCreatePinnedToCore(output,"v12_console",4096,nullptr,1,&log_task,1)==pdPASS;}
LogStatus log_status(){portENTER_CRITICAL(&log_mux);const auto s=stats;portEXIT_CRITICAL(&log_mux);auto result=s;
  result.capture_active=s.capture_active&&now_us()<s.capture_deadline_us;return result;}
}
extern "C" int __wrap_printf(const char* format,...){va_list args;va_start(args,format);const int n=v12::logv(format,args);va_end(args);return n;}
extern "C" int __wrap_puts(const char* text){return v12::logf("%s\n",text);}
extern "C" int wc12_v9_observer_printf(const char* format,...){va_list args;va_start(args,format);const int n=v12::logv(format,args);va_end(args);return n;}
