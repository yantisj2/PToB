#include "gnss_pps.hpp"
#include "ptob_gnss_reference.hpp"
#include "ptob_l76k_get.hpp"
#include "driver/mcpwm_cap.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "w12_board_profile.hpp"
#include "w12_device_profile.hpp"
#include "freertos/FreeRTOS.h"
#include "esp_mac.h"
#include <cstdio>
#ifdef PTOB_MIXED_LAB
extern "C" int wc12_v9_observer_printf(const char*,...) __attribute__((format(printf,1,2)));
#define WC12_OBSERVER_PRINTF wc12_v9_observer_printf
#else
#define WC12_OBSERVER_PRINTF std::printf
#endif

#include <cstring>
#include <limits>
#include <initializer_list>

#ifndef W12_GNSS_VERIFIED_INPUTS
#define W12_GNSS_VERIFIED_INPUTS 0
#endif
#ifndef W12_GNSS_RX_PIN
#define W12_GNSS_RX_PIN -1
#endif
#ifndef W12_GNSS_PPS_PIN
#define W12_GNSS_PPS_PIN -1
#endif
#ifndef W12_GNSS_BAUD
#define W12_GNSS_BAUD 9600
#endif
#ifndef W12_GNSS_R8_POWER
#define W12_GNSS_R8_POWER 0
#endif
#ifndef W12_GNSS_W12_POWER
#define W12_GNSS_W12_POWER 0
#endif
namespace gnss_pps {
namespace {
gptimer_handle_t clock=nullptr;Consumer consumer=nullptr;NmeaConsumer nmea_consumer=nullptr;
mcpwm_cap_timer_handle_t cap_timer=nullptr;mcpwm_cap_channel_handle_t pps_channel=nullptr,map_channel=nullptr;
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
struct Raw {uint32_t capture=0,number=0;};
Raw queue[16]{};unsigned head=0,tail=0,count=0;
uint32_t edge_count=0,drops=0,peak=0,map_capture=0,map_serial=0;
uint64_t before=0,after=0,request_tick=0,last_map_request=0,last_status=0,last_edge=0,last_rmc=0;
uint32_t request_serial=0,resolution=0,map_timeout=0,map_failure=0,nmea_bytes=0,nmea_lines=0,nmea_bad=0,nmea_overflow=0;
QueueHandle_t uart_events=nullptr;
uint32_t uart_fifo_overflow=0,uart_buffer_full=0,uart_frame_error=0,uart_parity_error=0,uart_break=0,uart_buffer_peak=0;
int rx_gpio=W12_GNSS_RX_PIN;uint32_t route_epoch=0,uart_baud=W12_GNSS_BAUD;
bool query_used=false,query_active=false;uint64_t query_deadline=0;
bool aid_used=false,aid_possible=false,aid_waiting=false;uint64_t aid_deadline=0;
ptob_l76k::AidAck aid_ack;
struct AidReceipt {ptob_l76k::TimeAid source{};uint64_t begin=0,end=0,ack_tick=0,send_us=0;uint32_t bound=0;int written=0;unsigned state=0;bool done=false,released=false,bounded=false;} aid_receipt;
uint32_t query_bytes=0,query_chunks=0;
ptob_gnss::Rmc rmc{};ptob_gnss::CounterMap mapping;
ptob_gps::Shadow shadow;
bool guarded_map=true,shadow_logs=true;
uint32_t map_epoch=1,shadow_work_max_ticks=0,shadow_log_max_ticks=0;
uint64_t shadow_work_ticks=0,shadow_log_ticks=0;
uint64_t physical_pps_consumed=0,last_packet=0;uint32_t checked_packets=0;
char line[192]{};size_t length=0;bool discarding=false,ready=false,uart_ready=false;
uint64_t now(){uint64_t value=0;return clock && gptimer_get_raw_count(clock,&value)==ESP_OK?value:0;}
bool capture_failed(const char* stage){
 // Release only handles allocated by this observer, including its APB lock.
 for(auto* channel:{&map_channel,&pps_channel})if(*channel){
  (void)mcpwm_capture_channel_disable(*channel);(void)mcpwm_del_capture_channel(*channel);*channel=nullptr;
 }
 if(cap_timer){(void)mcpwm_capture_timer_stop(cap_timer);(void)mcpwm_capture_timer_disable(cap_timer);(void)mcpwm_del_capture_timer(cap_timer);cap_timer=nullptr;}
 ready=false;mapping.reset();
 WC12_OBSERVER_PRINTF("PTOB_GNSS_INIT enabled=0 reason=%s GPS_authority=SEPARATE_GM_E1 BLE_training=NONE qualification=UNKNOWN\n",stage);return false;
}
bool on_pps(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* event,void*) {
 portENTER_CRITICAL_ISR(&lock);
 if(edge_count!=UINT32_MAX)++edge_count;
 if(count==16){if(drops!=UINT32_MAX)++drops;}
 else{queue[head]={event->cap_value,edge_count};head=(head+1)%16;++count;if(count>peak)peak=count;}
 portEXIT_CRITICAL_ISR(&lock);return false;
}
bool on_map(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* event,void*) {
 portENTER_CRITICAL_ISR(&lock);map_capture=event->cap_value;++map_serial;portEXIT_CRITICAL_ISR(&lock);return false;
}
void nmea(uint64_t tick) {
 // Driver errors retain a missing-input denominator; event-queue delivery
 // itself is not guaranteed, so total UART coverage remains UNKNOWN.
 uart_event_t event{};
 for(unsigned budget=0;budget<8 && xQueueReceive(uart_events,&event,0)==pdTRUE;++budget){
  if(event.type==UART_FIFO_OVF)ptob_gnss::increment(uart_fifo_overflow);
  else if(event.type==UART_BUFFER_FULL)ptob_gnss::increment(uart_buffer_full);
  else if(event.type==UART_FRAME_ERR)ptob_gnss::increment(uart_frame_error);
  else if(event.type==UART_PARITY_ERR)ptob_gnss::increment(uart_parity_error);
  else if(event.type==UART_BREAK)ptob_gnss::increment(uart_break);
  if(event.type==UART_FIFO_OVF || event.type==UART_BUFFER_FULL || event.type==UART_FRAME_ERR || event.type==UART_PARITY_ERR){
   length=0;discarding=true; // Never join text across a known loss/corruption.
  }
 }
 size_t buffered=0;if(uart_get_buffered_data_len(UART_NUM_1,&buffered)==ESP_OK && buffered>uart_buffer_peak)uart_buffer_peak=uint32_t(buffered);
 // Bound each existing main-loop turn to 256 UART bytes; no new RT task.
 uint8_t bytes[256];const int n=uart_read_bytes(UART_NUM_1,bytes,sizeof(bytes),0);
 if(n<=0)return;
 if(aid_waiting){
  for(int i=0;i<n;i++){const int ack=aid_ack.feed(bytes[i]);if(ack){
    aid_waiting=false;aid_receipt.ack_tick=now();aid_receipt.state=ack>0&&aid_receipt.ack_tick<aid_deadline?2:3;
    WC12_OBSERVER_PRINTF("PTOB_GNSS_TIME_AID_ACK tick=%llu accepted=%u original_deadline20=%llu provenance=BLE_AIDED_GNSS independent_BLE_reference=0\n",(unsigned long long)tick,unsigned(aid_receipt.state==2),(unsigned long long)aid_deadline);
    break;
  }}
 }
 if(nmea_consumer)nmea_consumer(bytes,unsigned(n));
 nmea_bytes=UINT32_MAX-nmea_bytes<uint32_t(n)?UINT32_MAX:nmea_bytes+uint32_t(n);
 // Parse every byte already delivered in this bounded chunk before any
 // diagnostic formatting. Actual per-RMC consumption times remain causal;
 // no UART arrival timestamp, earlier time or wider association is invented.
 struct EpochLog {uint64_t consumed=0,pps_tick=0;uint32_t second=0,pps_number=0,coherent=0,epoch=0;
   const char* reason=nullptr;uint16_t offset=0;bool accepted=false;};
 EpochLog epochs[16]{};unsigned epoch_count=0;
 // prior partial<=191 + chunk<=256 + at most128 extra line markers<=575.
 char sentences[576]{};size_t sentence_bytes=0;uint32_t diagnostic_drops=0;
 for(int i=0;i<n;++i){const char c=char(bytes[i]);
  if(c=='\r')continue;
  if(c=='\n'){
   if(discarding){discarding=false;length=0;continue;}
   if(length){line[length]=0;ptob_gnss::increment(nmea_lines);
    const bool checked=ptob_gnss::checksum(line,length);if(!checked)ptob_gnss::increment(nmea_bad);
    else {last_packet=now();ptob_gnss::increment(checked_packets);}
    const auto parsed=ptob_gnss::rmc(line,length);if(parsed.valid){
     rmc=parsed;const uint64_t consumed=now();last_rmc=consumed;const bool accepted=shadow.rmc(parsed,consumed);
     if(shadow_logs){
      if(epoch_count<16){auto& e=epochs[epoch_count++];e.consumed=consumed;e.second=parsed.second;
       e.pps_number=shadow.last_number();e.pps_tick=shadow.last_tick();e.accepted=accepted;
       e.coherent=shadow.coherent;e.epoch=shadow.epoch;e.reason=shadow.epoch_reason;e.offset=uint16_t(sentence_bytes);}
      else ptob_gnss::increment(diagnostic_drops);
     }
    }
    if(sentence_bytes+length+2<=sizeof(sentences)){
     sentences[sentence_bytes++]=checked?'1':'0';std::memcpy(sentences+sentence_bytes,line,length+1);
     sentence_bytes+=length+1;
    }else ptob_gnss::increment(diagnostic_drops);
   }length=0;continue;
  }
  if(discarding)continue;
  if(c<32 || c>126 || length==sizeof(line)-1){discarding=true;ptob_gnss::increment(nmea_overflow);length=0;continue;}
  line[length++]=c;
 }
 if(query_active){
  // Preserve every byte delivered by the existing bounded RX driver, including
  // binary/partial frames, as bounded HEX chunks outside interrupts.
  char hex[513];static constexpr char digits[]="0123456789abcdef";
  for(int i=0;i<n;++i){hex[2*i]=digits[bytes[i]>>4];hex[2*i+1]=digits[bytes[i]&15];}hex[2*n]=0;
  query_bytes=UINT32_MAX-query_bytes<uint32_t(n)?UINT32_MAX:query_bytes+uint32_t(n);ptob_gnss::increment(query_chunks);
  WC12_OBSERVER_PRINTF("PTOB_GNSS_CFG_PRT_RX tick=%llu chunk=%lu bytes=%d hex=%s scope=NONSETTING_GET_RESPONSE TX38=HIGH_Z coverage=UART_DRIVER_DELIVERED_ONLY\n",(unsigned long long)tick,(unsigned long)query_chunks,n,hex);
 }
 // Retain source metadata before another RMC changes the shadow epoch.
 unsigned epoch_read=0;
 for(size_t offset=0;offset<sentence_bytes;){
  while(epoch_read<epoch_count && epochs[epoch_read].offset==offset){const auto& e=epochs[epoch_read++];
   WC12_OBSERVER_PRINTF("PTOB_GPS_EPOCH tick=%llu second=%lu pps_number=%lu pps_tick=%llu lag_us=%llu accepted=%u coherent=%lu epoch=%lu reason=%s association=EMPIRICAL_UNCALIBRATED GPS_authority=SEPARATE_GM_E1 BLE_training=NONE logged_tick=%llu deferred=1\n",
    (unsigned long long)e.consumed,(unsigned long)e.second,(unsigned long)e.pps_number,(unsigned long long)e.pps_tick,
    (unsigned long long)(e.pps_tick && e.consumed>=e.pps_tick?(e.consumed-e.pps_tick)/20:UINT64_MAX),unsigned(e.accepted),
    (unsigned long)e.coherent,(unsigned long)e.epoch,e.reason,(unsigned long long)now());
  }
  const bool checked=sentences[offset++]=='1';const char* sentence=sentences+offset;
  WC12_OBSERVER_PRINTF("PTOB_GNSS_NMEA tick=%llu rx_gpio=%d route_epoch=%lu checksum=%u sentence=%s logged_tick=%llu deferred=1 input_scope=UART_DRIVER_DELIVERED_CHUNK\n",
   (unsigned long long)tick,rx_gpio,(unsigned long)route_epoch,unsigned(checked),sentence,(unsigned long long)now());
  offset+=std::strlen(sentence)+1;
 }
 if(diagnostic_drops)WC12_OBSERVER_PRINTF("PTOB_GNSS_LOG_FAULT tick=%llu dropped=%lu coverage=INCOMPLETE scope=BOUNDED_DEFERRED_DIAGNOSTICS source_guard=UNCHANGED\n",
  (unsigned long long)now(),(unsigned long)diagnostic_drops);

}
}
bool init(gptimer_handle_t reference,Consumer callback) {
 clock=reference;consumer=callback;
 if(!W12_GNSS_VERIFIED_INPUTS || W12_GNSS_RX_PIN<0 || W12_GNSS_PPS_PIN<0){
  WC12_OBSERVER_PRINTF("PTOB_GNSS_INIT enabled=0 reason=UNVERIFIED_BOARD_WIRING pins=UNTOUCHED GPS_authority=SEPARATE_GM_E1 BLE_training=NONE PTS=OFF\n");return false;
 }
 uint8_t factory[6]{};
 if(esp_read_mac(factory,ESP_MAC_EFUSE_FACTORY)!=ESP_OK)return capture_failed("FACTORY_ID_UNAVAILABLE");
 const auto* profile=w12_device::find(factory);
 if(!profile || !profile->gps_inputs)return capture_failed("NO_VERIFIED_GPS_INPUT_CAPABILITY");
 uint32_t local_hz=0;
 if(!clock || !consumer || gptimer_get_resolution(clock,&local_hz)!=ESP_OK || local_hz!=20000000)return capture_failed("LOCAL_CLOCK_NOT_20MHz");
 if(W12_GNSS_R8_POWER){
  static_assert(!W12_GNSS_R8_POWER || w12_board::profile==8,"GPS power is R8 only");
  // Official HTIT-WBR8H V4.3.2: GPIO42 drives Q7's PMOS gate, LOW
  // supplies the GNSS connector. No UART TX or module commands are driven.
  // Set the latch before output mode to avoid an unintended power pulse.
  gpio_config_t power{};power.pin_bit_mask=1ULL<<42;power.mode=GPIO_MODE_OUTPUT;
  if(gpio_set_level(GPIO_NUM_42,0)!=ESP_OK || gpio_config(&power)!=ESP_OK)return capture_failed("R8_POWER_FAILURE");
  WC12_OBSERVER_PRINTF("PTOB_GNSS_POWER gpio=42 level=0 route=HTIT_WBR8H_V4_3_2_Q7 authority=OFF TX=UNTOUCHED\n");
 }
 if(W12_GNSS_W12_POWER){
  static_assert(!W12_GNSS_W12_POWER || w12_board::profile==12,"GPS power is Meshnology W12 only");
  static_assert(!(W12_GNSS_W12_POWER && W12_GNSS_R8_POWER),"Incompatible power routes");
  // Meshnology W12 schematic Q5/R45/R46 and vendor Demo_13_GNSS_L76K:
  // GPIO48LOW enables main GNSS supply;40WAKE/42RESET remain high-Z.
  // Release those inputs before supply enable; never pulse reset/wake or TX.
  gpio_config_t passive{};passive.pin_bit_mask=(1ULL<<40)|(1ULL<<42);passive.mode=GPIO_MODE_INPUT;
  gpio_config_t power{};power.pin_bit_mask=1ULL<<48;power.mode=GPIO_MODE_OUTPUT;
  if(gpio_config(&passive)!=ESP_OK || gpio_set_level(GPIO_NUM_48,0)!=ESP_OK || gpio_config(&power)!=ESP_OK)return capture_failed("W12_POWER_FAILURE");
  WC12_OBSERVER_PRINTF("PTOB_GNSS_POWER gpio=48 level=0 route=MESHNOLOGY_W12_Q5 wake_gpio=40 wake=HIGH_Z reset_gpio=42 reset=HIGH_Z authority=OFF TX=UNTOUCHED\n");
  // uart_set_pin(RX) enables input but preserves an inherited matrix output
  // route. Force both documented UART pads' output enables off first.
  gpio_config_t uart_inputs{};uart_inputs.pin_bit_mask=(1ULL<<38)|(1ULL<<39);uart_inputs.mode=GPIO_MODE_INPUT;
  if(gpio_config(&uart_inputs)!=ESP_OK)return capture_failed("W12_UART_INPUT_RELEASE_FAILURE");
 }
 // Software map channel has no GPIO. PPS41/RX use inputs; documented
 // optional power controls above are distinct from capture and UART TX.
 mcpwm_capture_timer_config_t cfg{};cfg.group_id=0;cfg.clk_src=MCPWM_CAPTURE_CLK_SRC_APB;
 if(mcpwm_new_capture_timer(&cfg,&cap_timer)!=ESP_OK || mcpwm_capture_timer_get_resolution(cap_timer,&resolution)!=ESP_OK ||
    resolution!=80000000 || mcpwm_capture_timer_enable(cap_timer)!=ESP_OK || mcpwm_capture_timer_start(cap_timer)!=ESP_OK)return capture_failed("CAPTURE_TIMER_NOT_READY_80MHz");
 mcpwm_capture_channel_config_t ch{};ch.gpio_num=W12_GNSS_PPS_PIN;ch.prescale=1;ch.flags.pos_edge=1;
 const mcpwm_capture_event_callbacks_t pcb={.on_cap=on_pps};
 if(mcpwm_new_capture_channel(cap_timer,&ch,&pps_channel)!=ESP_OK || mcpwm_capture_channel_register_event_callbacks(pps_channel,&pcb,nullptr)!=ESP_OK || mcpwm_capture_channel_enable(pps_channel)!=ESP_OK)return capture_failed("PPS_CAPTURE_CHANNEL_FAILURE");
 ch={};ch.gpio_num=-1;ch.prescale=1;
 const mcpwm_capture_event_callbacks_t mcb={.on_cap=on_map};
 if(mcpwm_new_capture_channel(cap_timer,&ch,&map_channel)!=ESP_OK || mcpwm_capture_channel_register_event_callbacks(map_channel,&mcb,nullptr)!=ESP_OK || mcpwm_capture_channel_enable(map_channel)!=ESP_OK)return capture_failed("SOFTWARE_MAP_CHANNEL_FAILURE");
 ready=true;
 uart_config_t uart{};uart.baud_rate=W12_GNSS_BAUD;uart.data_bits=UART_DATA_8_BITS;uart.parity=UART_PARITY_DISABLE;uart.stop_bits=UART_STOP_BITS_1;uart.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;uart.source_clk=UART_SCLK_DEFAULT;
 uart_ready=uart_param_config(UART_NUM_1,&uart)==ESP_OK && uart_set_pin(UART_NUM_1,UART_PIN_NO_CHANGE,W12_GNSS_RX_PIN,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)==ESP_OK && uart_driver_install(UART_NUM_1,1024,0,16,&uart_events,0)==ESP_OK;
 WC12_OBSERVER_PRINTF("PTOB_GNSS_INIT enabled=1 capture=%u uart=%u rx_gpio=%d pps_gpio=%d baud=%d capture_hz=%lu local_hz=20000000 GPS_authority=SEPARATE_GM_E1 BLE_training=NONE power_gpio=%d power_level=%d qualification=UNKNOWN\n",unsigned(ready),unsigned(uart_ready),W12_GNSS_RX_PIN,W12_GNSS_PPS_PIN,W12_GNSS_BAUD,(unsigned long)resolution,W12_GNSS_W12_POWER?48:W12_GNSS_R8_POWER?42:-1,(W12_GNSS_W12_POWER || W12_GNSS_R8_POWER)?0:-1);return ready;
}
void poll(uint64_t tick) {
 if(!ready)return;
 uint32_t serial,cap;portENTER_CRITICAL(&lock);serial=map_serial;cap=map_capture;portEXIT_CRITICAL(&lock);
 if(request_tick && serial!=request_serial){
  const bool observed=serial==request_serial+1 && after>=before && mapping.observe({before,after,now(),cap});
  const uint64_t available=now();
  if(observed && mapping.release(available)){
   if(shadow_logs)WC12_OBSERVER_PRINTF("PTOB_GPS_MAP tick=%llu epoch=%lu treatment=%s before=%llu after=%llu available=%llu capture=%lu width_ns=%llu offset_capture_ticks=%ld accepted=1 scope=SOFTWARE_READ_ENCLOSURE physical_bias=UNKNOWN\n",(unsigned long long)available,(unsigned long)map_epoch,guarded_map?"GUARDED":"BASELINE",(unsigned long long)before,(unsigned long long)after,(unsigned long long)available,(unsigned long)cap,(unsigned long long)((after-before)*50),(long)int32_t(uint32_t((before+(after-before)/2)*4)-cap));
  }
  else{ptob_gnss::increment(map_failure);mapping.reset();}
  request_tick=0;
 }else if(request_tick && tick>=request_tick && tick-request_tick>2000000){ptob_gnss::increment(map_timeout);mapping.reset();request_tick=0;}
 // Consume original edges before scheduling a new software map capture.
 for(unsigned budget=0;budget<16;++budget){Raw raw{};bool have=false;
  portENTER_CRITICAL(&lock);if(count){raw=queue[tail];tail=(tail+1)%16;--count;have=true;}portEXIT_CRITICAL(&lock);if(!have)break;
  // A physical PPS can enter this queue after poll's initial timestamp. Use
  // actual dequeue time; never make a valid point look future by reusing it.
  const uint64_t consumed=now();
  physical_pps_consumed=consumed;
  const auto edge=mapping.convert(raw.capture,consumed);if(edge.valid)last_edge=edge.tick;
  const uint64_t work_begin=now();
  const auto score=shadow.edge(raw.capture,raw.number,edge.tick,edge.bracket_ns,consumed,edge.valid);
  const uint64_t release=now();shadow.release(release);
  const uint64_t work=release-work_begin;shadow_work_ticks+=work;
  if(work>shadow_work_max_ticks)shadow_work_max_ticks=uint32_t(work);
  const uint64_t log_begin=now();
  if(shadow_logs)WC12_OBSERVER_PRINTF("PTOB_GPS_CLOCK_EDGE tick=%llu number=%lu raw_extended=%llu mapped=%u pps_tick=%llu consumed=%llu poll_start=%llu raw_prediction_valid=%u raw_error_ns=%lld mapped_prediction_valid=%u mapped_error_ns=%lld raw_samples=%u mapped_samples=%u raw_rate_ppb=%ld mapped_rate_ppb=%ld raw_fit_rms_ns=%lu mapped_fit_rms_ns=%lu model_available=%llu map_epoch=%lu treatment=%s scope=CAUSAL_PRIOR_GPS_MODEL total_bound=UNKNOWN GPS_authority=SEPARATE_GM_E1 BLE_training=NONE\n",(unsigned long long)consumed,(unsigned long)raw.number,(unsigned long long)score.raw_extended,unsigned(edge.valid),(unsigned long long)edge.tick,(unsigned long long)consumed,(unsigned long long)tick,unsigned(score.raw_valid),(long long)score.raw_error_ns,unsigned(score.mapped_valid),(long long)score.mapped_error_ns,shadow.raw.samples,shadow.mapped.samples,(long)shadow.raw.rate_ppb(),(long)shadow.mapped.rate_ppb(),(unsigned long)shadow.raw.rms_ns,(unsigned long)shadow.mapped.rms_ns,(unsigned long long)release,(unsigned long)map_epoch,guarded_map?"GUARDED":"BASELINE");
  const uint64_t log_work=now()-log_begin;shadow_log_ticks+=log_work;if(log_work>shadow_log_max_ticks)shadow_log_max_ticks=uint32_t(log_work);
  if(consumer)consumer({edge.tick,edge.anchor_available,raw.capture,raw.number,edge.bracket_ns,edge.valid,score.raw_extended});
 }
 if(uart_ready)nmea(tick);
 if(aid_waiting && tick>=aid_deadline){
  aid_waiting=false;aid_receipt.state=4;WC12_OBSERVER_PRINTF("PTOB_GNSS_TIME_AID_ACK tick=%llu accepted=0 reason=ORIGINAL_RESPONSE_TIMEOUT provenance=BLE_AIDED_GNSS independent_BLE_reference=0\n",(unsigned long long)tick);
 }
 if(query_active && tick>=query_deadline){
  query_active=false;
  WC12_OBSERVER_PRINTF("PTOB_GNSS_CFG_PRT_END tick=%llu bytes=%lu chunks=%lu TX38=HIGH_Z config_written=0 NVM_written=0 reset=0 GPS_authority=SEPARATE_GM_E1 BLE_training=NONE coverage=UART_DRIVER_DELIVERED_ONLY\n",(unsigned long long)tick,(unsigned long)query_bytes,(unsigned long)query_chunks);
 }
 if(!request_tick && (!last_map_request || tick-last_map_request>=5000000)){
  request_tick=tick;last_map_request=tick;
  // A software capture interrupt between the two GPTimer reads adds its
  // execution time to the old bracket. Enclose only these supported driver
  // calls; defer the capture ISR until both reads finish. No controller hook,
  // timer/GPIO rerouting, formatting, allocation or fit is in this section.
  esp_err_t result;
  if(guarded_map){
   portENTER_CRITICAL(&lock);request_serial=map_serial;
   before=now();result=mcpwm_capture_channel_trigger_soft_catch(map_channel);after=now();
   portEXIT_CRITICAL(&lock);
  }else{
   portENTER_CRITICAL(&lock);request_serial=map_serial;portEXIT_CRITICAL(&lock);
   before=now();result=mcpwm_capture_channel_trigger_soft_catch(map_channel);after=now();
  }
  if(result!=ESP_OK){ptob_gnss::increment(map_failure);request_tick=0;mapping.reset();}
 }
 if(!last_status || tick-last_status>=20000000){last_status=tick;status(now());}
}
bool pins(uint64_t tick) {
 if(!W12_GNSS_W12_POWER || !ready || !uart_ready)return false;
 const uint64_t mask=(1ULL<<38)|(1ULL<<40)|(1ULL<<42)|(1ULL<<48)|(1ULL<<39)|(1ULL<<41)|(1ULL<<45)|(1ULL<<47);
 WC12_OBSERVER_PRINTF("PTOB_GNSS_PIN_CONFIG_BEGIN tick=%llu profile=12 scope=READ_ONLY_MCU_REGISTERS outputs_changed=0 rail_voltage=UNMEASURED\n",(unsigned long long)tick);
 const esp_err_t rc=gpio_dump_io_configuration(stdout,mask);
 // gpio_get_level is meaningful only for input-enabled pins. Config dump
 // records actual enable state; never read48 as the module's rail voltage.
 WC12_OBSERVER_PRINTF("PTOB_GNSS_PINS tick=%llu ack=%u profile=12 rx_gpio=%d wake40_input_level=%d reset42_input_level=%d uart39_input_level=%d pps41_input_level=%d validity=REQUIRES_INPUT_ENABLE_DUMP power48_declared_level=0 power48_voltage=UNMEASURED outputs_changed=0 GPS_authority=SEPARATE_GM_E1 BLE_training=NONE\n",(unsigned long long)tick,unsigned(rc==ESP_OK),rx_gpio,gpio_get_level(GPIO_NUM_40),gpio_get_level(GPIO_NUM_42),gpio_get_level(GPIO_NUM_39),gpio_get_level(GPIO_NUM_41));
 WC12_OBSERVER_PRINTF("PTOB_GNSS_PIN_CONFIG_END tick=%llu status=%d outputs_changed=0\n",(unsigned long long)tick,int(rc));return rc==ESP_OK;
}
bool select_rx(int gpio){
 if(!(W12_GNSS_R8_POWER || W12_GNSS_W12_POWER) || !ready || !uart_ready || query_active || (gpio!=38 && gpio!=39))return false;
 // The initial full efuse gate already bound this enabled observer to C-3.
 // Both documented connector GPIOs stay inputs; TX and all output controls
 // are unchanged. Never combine a partial sentence across input treatments.
 rmc={};last_rmc=0;length=0;discarding=false;shadow.route_reset();
 if(W12_GNSS_W12_POWER){
  gpio_config_t input{};input.pin_bit_mask=1ULL<<gpio;input.mode=GPIO_MODE_INPUT;
  if(gpio_config(&input)!=ESP_OK){uart_ready=false;return false;}
 }
 if(uart_set_pin(UART_NUM_1,UART_PIN_NO_CHANGE,gpio,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK || uart_flush_input(UART_NUM_1)!=ESP_OK){uart_ready=false;return false;}
 if(uart_events)xQueueReset(uart_events);
 rx_gpio=gpio;ptob_gnss::increment(route_epoch);last_status=0;
 WC12_OBSERVER_PRINTF("PTOB_GNSS_ROUTE tick=%llu rx_gpio=%d baud=%lu route_epoch=%lu ack=1 output_controls=UNCHANGED TX=UNTOUCHED GPS_authority=SEPARATE_GM_E1 BLE_training=NONE\n",(unsigned long long)now(),rx_gpio,(unsigned long)uart_baud,(unsigned long)route_epoch);
 return true;
}
bool select_baud(uint32_t baud){
 if(!W12_GNSS_W12_POWER || !ready || !uart_ready || query_active ||
    !(baud==4800 || baud==9600 || baud==19200 || baud==38400 || baud==57600 || baud==115200))return false;
 // Quectel L76K Hardware DesignV1.0 section4.1.1.1 lists five rates;
 // L76K GNSS ProtocolSpecificationV1.1 section2.3.1 additionally permits4800.
 // Reconfigure only the existing RX driver. No UART TX, GPS command, power,
 // reset, wake or BLE estimator change; flush partial sentences/events.
 rmc={};last_rmc=0;length=0;discarding=false;shadow.route_reset();
 if(uart_set_baudrate(UART_NUM_1,baud)!=ESP_OK || uart_flush_input(UART_NUM_1)!=ESP_OK){uart_ready=false;return false;}
 if(uart_events)xQueueReset(uart_events);
 uart_baud=baud;ptob_gnss::increment(route_epoch);last_status=0;
 WC12_OBSERVER_PRINTF("PTOB_GNSS_ROUTE tick=%llu rx_gpio=%d baud=%lu route_epoch=%lu ack=1 output_controls=UNCHANGED TX=UNTOUCHED GPS_authority=SEPARATE_GM_E1 BLE_training=NONE\n",(unsigned long long)now(),rx_gpio,(unsigned long)uart_baud,(unsigned long)route_epoch);return true;
}
bool query_port(uint64_t tick){
 if(!W12_GNSS_W12_POWER || !ready || !uart_ready || rx_gpio!=39 || uart_baud!=9600 || query_used || query_active)return false;
 uint8_t factory[6]{};static constexpr uint8_t authorized[6]={0x44,0xb1,0x76,0xae,0x20,0x44};
 if(esp_read_mac(factory,ESP_MAC_EFUSE_FACTORY)!=ESP_OK || std::memcmp(factory,authorized,6))return false;
 query_used=true;rmc={};last_rmc=0;length=0;discarding=false;query_bytes=0;query_chunks=0;
 struct IO {
  bool flush(){if(uart_events)xQueueReset(uart_events);return uart_flush_input(UART_NUM_1)==ESP_OK;}
  bool connect(){return uart_set_pin(UART_NUM_1,38,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)==ESP_OK;}
  int write(const uint8_t* p,size_t n){return uart_tx_chars(UART_NUM_1,reinterpret_cast<const char*>(p),n);}
  bool wait(unsigned ms){return uart_wait_tx_done(UART_NUM_1,pdMS_TO_TICKS(ms))==ESP_OK;}
  bool release(){gpio_config_t passive{};passive.pin_bit_mask=1ULL<<38;passive.mode=GPIO_MODE_INPUT;return gpio_config(&passive)==ESP_OK;}
 } io;
 const auto result=ptob_l76k::get_port(io);const uint64_t end=now();
 if(!result.released){ready=false;uart_ready=false;}
 const bool ok=result.connected && result.written==10 && result.done && result.released;
 if(ok){query_active=true;query_deadline=end+20000000;}
 WC12_OBSERVER_PRINTF("PTOB_GNSS_CFG_PRT_GET tick=%llu end_tick=%llu ack=%u rx_gpio=39 baud=9600 TX_gpio=38 enqueued=%d flushed=%u connected=%u TX_done=%u TX_restored_high_z=%u API_wall_us=%llu physical_TX_duration=UNMEASURED software_timeout_ms=100 scheduler_margin=UNKNOWN response_window_ms=1000 frame_hex=bace0000060000000600 scope=DOCUMENTED_NONSETTING_GET config_written=0 NVM_written=0 reset=0 GPS_authority=SEPARATE_GM_E1 BLE_training=NONE\n",(unsigned long long)tick,(unsigned long long)end,unsigned(ok),result.written,unsigned(result.flushed),unsigned(result.connected),unsigned(result.done),unsigned(result.released),(unsigned long long)(end>=tick?(end-tick)/20:0));return ok;
}
bool ble_aided(){return aid_possible;}
unsigned aid_state(){return aid_receipt.state;}
void aid_time(const ptob_l76k::TimeAid& a,uint64_t now_us){
 // The public GNSS indicator profile observes the receiver without sending
 // module commands or giving local GNSS authority over the BLE clock.
 if(PTOB_GNSS_INDICATORS)return;
 if(aid_used||!W12_GNSS_W12_POWER||!ready||!uart_ready||query_active||rx_gpio!=39||uart_baud!=9600)return;
 // The original capability and board guards exclude C1 and the legacy R8.
 uint8_t factory[6]{};static const uint8_t authorized[6]={0x44,0xb1,0x76,0xae,0x20,0x44};
 if(esp_read_mac(factory,ESP_MAC_EFUSE_FACTORY)!=ESP_OK||std::memcmp(factory,authorized,6))return;
 // Do not disturb an already acquired independent witness.
 if(rmc.fix && last_rmc && now()>=last_rmc && now()-last_rmc<40000000)return;
 uint8_t frame[66]{};uint32_t bound=0;
 if(!ptob_l76k::aid_frame(frame,a,now_us,bound))return;
 aid_used=true;const uint64_t begin=now();
 const bool connected=uart_set_pin(UART_NUM_1,38,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)==ESP_OK;
 const int written=connected?uart_tx_chars(UART_NUM_1,reinterpret_cast<const char*>(frame),sizeof frame):0;
 const bool done=written==int(sizeof frame)&&uart_wait_tx_done(UART_NUM_1,pdMS_TO_TICKS(100))==ESP_OK;
 gpio_config_t passive{};passive.pin_bit_mask=1ULL<<38;passive.mode=GPIO_MODE_INPUT;
 const bool released=gpio_config(&passive)==ESP_OK;const uint64_t end=now();
 // Even a partial transmission can affect the receiver. Never silently return
 // this powered receiver to an independent-reference label after a MCU reboot.
 aid_possible=written>0;
 if(aid_possible){shadow.route_reset();rmc={};last_rmc=0;}
 if(!released){ready=false;uart_ready=false;}
 const bool bounded=end>=begin&&end-begin<=ptob_l76k::AidTxBudgetUs*20;
 aid_waiting=written==int(sizeof frame)&&done&&released&&bounded;
 aid_ack={};aid_deadline=begin+20000000;
 aid_receipt={a,begin,end,0,now_us,bound,written,aid_waiting?1u:5u,done,released,bounded};
 WC12_OBSERVER_PRINTF("PTOB_GNSS_TIME_AID tick=%llu end_tick=%llu root=%016llx boot=%016llx source_epoch=%lu publication=%lu UTC_us=%llu source_age_us=%llu bound_us=%lu bytes=%d TX_done=%u TX38_high_z=%u bounded=%u ACK_pending=%u frame=CASIC_AID_INI time_scale=GPS leap_seconds=18 position_valid=0 frequency_valid=0 NVM_written=0 reset=0 provenance=BLE_AIDED_GNSS independent_BLE_reference=0\n",(unsigned long long)begin,(unsigned long long)end,(unsigned long long)a.root,(unsigned long long)a.boot,(unsigned long)a.source_epoch,(unsigned long)a.publication,(unsigned long long)(a.utc_us+now_us-a.at_us),(unsigned long long)(now_us-a.received_us),(unsigned long)bound,written,unsigned(done),unsigned(released),unsigned(bounded),unsigned(aid_waiting));
}
void status(uint64_t tick) {
 if(aid_used){const auto& a=aid_receipt;
  WC12_OBSERVER_PRINTF("PTOB_GNSS_AID_RECEIPT observed_tick=%llu TX_begin20=%llu TX_end20=%llu ACK_tick20=%llu state=%u root=%016llx root_boot=%016llx source_epoch=%lu publication=%lu UTC_us=%llu source_age_us=%llu bound_us=%lu bytes=%d TX_done=%u TX38_high_z=%u bounded=%u retained=RAM_SINCE_TRANSACTION independent_BLE_reference=0\n",(unsigned long long)tick,(unsigned long long)a.begin,(unsigned long long)a.end,(unsigned long long)a.ack_tick,a.state,(unsigned long long)a.source.root,(unsigned long long)a.source.boot,(unsigned long)a.source.source_epoch,(unsigned long)a.source.publication,(unsigned long long)(a.source.utc_us+a.send_us-a.source.at_us),(unsigned long long)(a.send_us-a.source.received_us),(unsigned long)a.bound,a.written,unsigned(a.done),unsigned(a.released),unsigned(a.bounded));
 }
 uint32_t edges,drop,qpeak;portENTER_CRITICAL(&lock);edges=edge_count;drop=drops;qpeak=peak;portEXIT_CRITICAL(&lock);
 WC12_OBSERVER_PRINTF("PTOB_GNSS_STATUS tick=%llu capture=%u uart=%u rx_gpio=%d baud=%lu route_epoch=%lu PPS_edges=%lu queue_drop=%lu queue_peak=%lu map_accepted=%lu map_rejected=%lu map_timeout=%lu map_failure=%lu PPS_age_ms=%llu UART_bytes=%lu NMEA_lines=%lu NMEA_bad=%lu NMEA_overflow=%lu RMC_valid=%u RMC_fix=%u RMC_second=%lu RMC_age_ms=%llu epoch_association=UNQUALIFIED GPS_authority=SEPARATE_GM_E1 BLE_training=NONE total_bound=UNKNOWN\n",
 (unsigned long long)tick,unsigned(ready),unsigned(uart_ready),rx_gpio,(unsigned long)uart_baud,(unsigned long)route_epoch,(unsigned long)edges,(unsigned long)drop,(unsigned long)qpeak,(unsigned long)mapping.accepted,(unsigned long)mapping.rejected,(unsigned long)map_timeout,(unsigned long)map_failure,(unsigned long long)(last_edge && tick>=last_edge?(tick-last_edge)/20000:UINT64_MAX),(unsigned long)nmea_bytes,(unsigned long)nmea_lines,(unsigned long)nmea_bad,(unsigned long)nmea_overflow,unsigned(rmc.valid),unsigned(rmc.fix),(unsigned long)rmc.second,(unsigned long long)(last_rmc && tick>=last_rmc?(tick-last_rmc)/20000:UINT64_MAX));
 if(uart_ready)WC12_OBSERVER_PRINTF("PTOB_GNSS_UART tick=%llu buffer_peak=%lu FIFO_overflow=%lu buffer_full=%lu frame_error=%lu parity_error=%lu break=%lu event_queue_capacity=16 event_scan_budget=8 byte_scan_budget=256 coverage=UNKNOWN\n",(unsigned long long)tick,(unsigned long)uart_buffer_peak,(unsigned long)uart_fifo_overflow,(unsigned long)uart_buffer_full,(unsigned long)uart_frame_error,(unsigned long)uart_parity_error,(unsigned long)uart_break);
 const auto v=shadow.view(tick);
 WC12_OBSERVER_PRINTF("PTOB_GPS_CLOCK_STATUS tick=%llu valid=%u utc_ns=%lld coherent=%lu epoch=%lu reason=%s raw_samples=%u mapped_samples=%u raw_rate_ppb=%ld mapped_rate_ppb=%ld raw_fit_rms_ns=%lu mapped_fit_rms_ns=%lu model_available=%llu pps_age_us=%llu rmc_age_us=%llu map_bracket_ns=%lu map_epoch=%lu treatment=%s logs=%u rmc_accepted=%lu rmc_rejected=%lu raw_resets=%lu mapped_resets=%lu model_bytes=%u work_total_ticks=%llu work_max_ticks=%lu edge_log_total_ticks=%llu edge_log_max_ticks=%lu clock=%s association=EMPIRICAL_UNCALIBRATED total_bound=UNKNOWN GPS_authority=SEPARATE_GM_E1 BLE_training=NONE CPU=UNMEASURED RF=UNMEASURED PTS=OFF\n",(unsigned long long)tick,unsigned(v.valid),(long long)v.ns,(unsigned long)shadow.coherent,(unsigned long)shadow.epoch,shadow.epoch_reason,shadow.raw.samples,shadow.mapped.samples,(long)shadow.raw.rate_ppb(),(long)shadow.mapped.rate_ppb(),(unsigned long)shadow.raw.rms_ns,(unsigned long)shadow.mapped.rms_ns,(unsigned long long)v.available,(unsigned long long)(v.pps_age_ticks/20),(unsigned long long)(v.rmc_age_ticks/20),(unsigned long)v.map_bracket_ns,(unsigned long)map_epoch,guarded_map?"GUARDED":"BASELINE",unsigned(shadow_logs),(unsigned long)shadow.rmc_accepted,(unsigned long)shadow.rmc_rejected,(unsigned long)shadow.raw.resets,(unsigned long)shadow.mapped.resets,unsigned(sizeof(shadow)),(unsigned long long)shadow_work_ticks,(unsigned long)shadow_work_max_ticks,(unsigned long long)shadow_log_ticks,(unsigned long)shadow_log_max_ticks,aid_possible?"BLE_AIDED_GPS_SHADOW":"GPS_SHADOW_PRIOR_AID_UNKNOWN");
}
void set_nmea_consumer(NmeaConsumer callback){nmea_consumer=callback;}
Activity activity(uint64_t tick){
 return {last_packet,physical_pps_consumed,checked_packets,ready&&rmc.valid&&rmc.fix&&last_rmc&&tick>=last_rmc&&tick-last_rmc<30000000};
}
Reference reference(){return {last_rmc,rmc.second,shadow.epoch,shadow.coherent,ready&&rmc.valid&&rmc.fix};}
ptob_gps::Shadow::View shadow_view(uint64_t tick){return shadow.view(tick);}
bool shadow_enabled(){return ready;}
bool source_models_ready(){return ready && shadow.raw.samples==32 && shadow.mapped.samples==32 && shadow.raw.model.valid && shadow.mapped.model.valid;}
bool pps_recent(uint64_t tick){return ready && physical_pps_consumed && tick>=physical_pps_consumed && tick-physical_pps_consumed<2000000;}
bool select_map(bool guarded){
 if(!W12_GNSS_W12_POWER || !ready || request_tick)return false;
 guarded_map=guarded;mapping.reset();shadow.map_reset();ptob_gnss::increment(map_epoch);last_map_request=0;
 WC12_OBSERVER_PRINTF("PTOB_GPS_MAP_CONFIG tick=%llu ack=1 epoch=%lu treatment=%s outputs_changed=0 BLE_training=NONE GPS_authority=SEPARATE_GM_E1\n",(unsigned long long)now(),(unsigned long)map_epoch,guarded_map?"GUARDED":"BASELINE");return true;
}
bool shadow_logging(bool enabled){
 if(!W12_GNSS_W12_POWER || !ready)return false;
 shadow_logs=enabled;
 WC12_OBSERVER_PRINTF("PTOB_GPS_LOG_CONFIG tick=%llu ack=1 enabled=%u source_changed=0\n",(unsigned long long)now(),unsigned(enabled));return true;
}
}
