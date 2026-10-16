#include "periodic_rx.hpp"
#include "td_publication.hpp"
#include "ss3_native_display.hpp"
#include "ss3_display_clock.hpp"
#include "../legacy/m5stick_s3/board.hpp"
#include "../legacy/m5stick_s3/ui.hpp"
#include "../legacy/m5stick_s3/dirty_rect.hpp"
#include "../legacy/m5stick_s3/td_controls.hpp"
#include "../legacy/m5stick_s3/td_health.hpp"
#include "../legacy/m5stick_s3/td_relays.hpp"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include <new>
#include "esp_timer.h"
#include "esp_mac.h"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
extern "C" bool clock_reference_read(uint64_t*);
namespace {
using namespace sticks3;
i2c_master_bus_handle_t bus=nullptr;i2c_master_dev_handle_t pm1=nullptr;spi_device_handle_t lcd=nullptr;
uint8_t* frame=nullptr;uint8_t* pixels=nullptr;TaskHandle_t task=nullptr;
std::atomic<bool> awake{true};std::atomic<uint32_t> frames{0},errors{0},max_us{0};
struct Snapshot {View view{};DisplayClock clock{};uint64_t banner_until=0;};
struct UiStorage {Snapshot constructed{},shown{},published[3]{};};
UiStorage* ui_storage=nullptr;v12::TdPublication ui_publication;std::atomic<bool> ui_published{false};
std::atomic<uint32_t> health_word{0},health_changes{0},health_cancelled{0},health_min_us{UINT32_MAX};
std::atomic<uint32_t> fine_frames{0},coarse_frames{0},invalid_frames{0},valid_edges{0},ready_edges{0},painted_strips{0},full_passes{0};
void count(std::atomic<uint32_t>& n){if(n!=UINT32_MAX)++n;}
bool read(uint8_t reg,uint8_t& value){return i2c_master_transmit_receive(pm1,&reg,1,&value,1,20)==ESP_OK;}
bool write(uint8_t reg,uint8_t value){const uint8_t b[]{reg,value};return i2c_master_transmit(pm1,b,2,20)==ESP_OK;}
bool bits(uint8_t reg,uint8_t clear,uint8_t set){uint8_t v=0;return read(reg,v) && write(reg,(v&~clear)|set);}
bool power_init() {
 i2c_master_bus_config_t cfg{};cfg.i2c_port=I2C_NUM_1;cfg.sda_io_num=gpio_num_t(PowerSda);cfg.scl_io_num=gpio_num_t(PowerScl);cfg.clk_source=I2C_CLK_SRC_DEFAULT;cfg.glitch_ignore_cnt=7;cfg.flags.enable_internal_pullup=true;
 i2c_device_config_t dev{};dev.device_address=PowerAddress;dev.scl_speed_hz=100000;
 if(i2c_new_master_bus(&cfg,&bus)!=ESP_OK || i2c_master_bus_add_device(bus,&dev,&pm1)!=ESP_OK)return false;
 // Wake a sleeping PM1 with a harmless register-address transaction, then read
 // its four identity bytes. Read failures never become a fabricated identity.
 const uint8_t reg=0; (void)i2c_master_transmit(pm1,&reg,1,20);vTaskDelay(pdMS_TO_TICKS(10));
 uint8_t id[4]{};if(i2c_master_transmit_receive(pm1,&reg,1,id,4,20)!=ESP_OK || id[0]!=0x50 || id[1]!=0x20 || id[2]!=0x05)return false;
 // Retain charge, low-voltage protection and power-button/reset configuration.
 // LCD uses L3B; shared audio rail is necessary but PG3 keeps its amplifier off.
 if(!write(0x09,0) || !write(0x0a,0) || !bits(0x06,0x08,0x06) ||
    !bits(0x16,0xf0,0) || !bits(0x13,0x0c,0) || !bits(0x11,0x08,0x04) || !bits(0x10,0,0x0c))return false;
 std::printf("STICKS3_PM1 id=%02x model=%02x hw=%02x sw=%02x LCD_L3B=ON amp=OFF EXT_5V=INPUT power_button=UNCHANGED\n",id[0],id[1],id[2],id[3]);
 vTaskDelay(pdMS_TO_TICKS(100));return true;
}
bool transmit(bool data,const void* buffer,size_t bytes) {
 if(!bytes)return true;
 if(gpio_set_level(gpio_num_t(LcdDc),data)!=ESP_OK)return false;
 spi_transaction_t t{};t.length=bytes*8;t.tx_buffer=buffer;
 return spi_device_polling_transmit(lcd,&t)==ESP_OK;
}
bool command(uint8_t cmd,const void* data=nullptr,size_t bytes=0){return transmit(false,&cmd,1) && transmit(true,data,bytes);}
bool rectangle_window(unsigned x,unsigned y,unsigned width,unsigned rows) {
 if(!width || !rows || x>=Width || width>Width-x || y>=Height || rows>Height-y)return false;
 const unsigned x0=XOffset+x,x1=x0+width-1,y0=YOffset+y,y1=y0+rows-1;
 const uint8_t xs[]{uint8_t(x0>>8),uint8_t(x0),uint8_t(x1>>8),uint8_t(x1)};
 const uint8_t ys[]{uint8_t(y0>>8),uint8_t(y0),uint8_t(y1>>8),uint8_t(y1)};
 return command(0x2a,xs,4) && command(0x2b,ys,4) && command(0x2c);
}
bool panel_init() {
 gpio_config_t cfg{};cfg.pin_bit_mask=bit(LcdDc)|bit(LcdReset)|bit(Backlight);cfg.mode=GPIO_MODE_OUTPUT;
 if(gpio_set_level(gpio_num_t(Backlight),0)!=ESP_OK || gpio_config(&cfg)!=ESP_OK)return false;
 spi_bus_config_t spi{};spi.mosi_io_num=LcdMosi;spi.miso_io_num=-1;spi.sclk_io_num=LcdClock;spi.quadwp_io_num=-1;spi.quadhd_io_num=-1;spi.max_transfer_sz=StripBytes;spi.isr_cpu_id=ESP_INTR_CPU_AFFINITY_1;
 spi_device_interface_config_t dev{};dev.clock_speed_hz=24000000;dev.spics_io_num=LcdCs;dev.queue_size=1;
 if(spi_bus_initialize(SPI3_HOST,&spi,SPI_DMA_CH_AUTO)!=ESP_OK || spi_bus_add_device(SPI3_HOST,&dev,&lcd)!=ESP_OK)return false;
 gpio_set_level(gpio_num_t(LcdReset),0);vTaskDelay(pdMS_TO_TICKS(20));gpio_set_level(gpio_num_t(LcdReset),1);vTaskDelay(pdMS_TO_TICKS(120));
 // ST7789 portrait RGB565, official Stick offsets52/40 and inversion ON.
 const uint8_t orient=PanelMadctl,format=0x55,gate=0x35,vcom=0x28,lcm=0x0c,vrhen[]{1,0xff},vrh=0x10,vdv=0x20,power[]{0xa4,0xa1},ram[]{0,0xc0};
 const uint8_t positive[]{0xd0,0,2,7,0x0a,0x28,0x32,0x44,0x42,6,0x0e,0x12,0x14,0x17},negative[]{0xd0,0,2,7,0x0a,0x28,0x31,0x54,0x47,0x0e,0x1c,0x17,0x1b,0x1e};
 if(!command(0x36,&orient,1) || !command(0x3a,&format,1) || !command(0xb7,&gate,1) || !command(0xbb,&vcom,1) || !command(0xc0,&lcm,1) || !command(0xc2,vrhen,2) || !command(0xc3,&vrh,1) || !command(0xc4,&vdv,1) || !command(0xd0,power,2) || !command(0xb0,ram,2) || !command(0xe0,positive,14) || !command(0xe1,negative,14) || !command(0x21) || !command(0x11))return false;
 vTaskDelay(pdMS_TO_TICKS(130));return command(0x38) && command(0x29);
}

void worker(void*) {
 TdHealthDisplay health_display;
 auto& state=ui_storage->shown;bool have=false,observed=false,last_valid=false,last_ready=false;uint16_t valid=0;TickType_t wake=xTaskGetTickCount();
 for(;;){
  if(ui_published.load(std::memory_order_acquire)){const int slot=ui_publication.pin();
   if(slot>=0){state=ui_storage->published[slot];ui_publication.release(unsigned(slot));have=true;}
  }
  if(!have||!awake.load()){gpio_set_level(gpio_num_t(Backlight),0);vTaskDelay(pdMS_TO_TICKS(20));wake=xTaskGetTickCount();continue;}
  const auto begin=esp_timer_get_time();uint64_t tick=0;const bool timer=clock_reference_read(&tick);
  auto view=state.view;int64_t utc=0;
  if(view.td.enabled){const auto now=uint64_t(std::max<int64_t>(0,begin));view.td.owner_stale=now<view.td.now_us||now-view.td.now_us>1000000;view.td.now_us=now;
   const auto raw=TdHealth::evaluate(view.td,view.radio_error);const auto input=TdHealthBanner::from(raw);
   view.td_health_banner=health_display.update(raw,now);
   const auto& label=view.td_health_banner;
   health_changes.store(health_display.changes,std::memory_order_relaxed);health_cancelled.store(health_display.cancelled,std::memory_order_relaxed);health_min_us.store(health_display.min_qualified_us,std::memory_order_relaxed);
   health_word.store(0x80000000u|uint32_t(label.code)|(uint32_t(input.code)<<8)|(uint32_t(label.level)<<16)|(uint32_t(input.level)<<18)|(uint32_t(health_display.pending)<<20),std::memory_order_release);
  }else{health_display={};health_word.store(0,std::memory_order_release);}
  const auto source=state.clock.project(view.time_valid,timer,tick,utc);
  if(source==DisplayClock::Source::Fine)count(fine_frames);
  else if(source==DisplayClock::Source::Coarse){
   count(coarse_frames);view.relay_ready=false;view.native_rms_ns=UINT32_MAX;view.hold_ms=0;
   if(state.clock.model.tick){view.held=true;std::snprintf(view.role,sizeof view.role,"HOLD");}
  }else count(invalid_frames);
  if(!timer||tick>=state.banner_until)view.mode_banner[0]=0;
  view.time_valid=utc>0;view.utc=utc>0?uint32_t(utc/1000000000):0;
  view.phase_explicit=true;view.phase256=utc>0?uint8_t((utc%1000000000)*256/1000000000):0;
  if(!view.time_valid){std::snprintf(view.upstream,sizeof view.upstream,"NONE");std::snprintf(view.role,sizeof view.role,"%s",state.view.time_valid?"STALE":state.view.role);view.relay_ready=false;view.native_rms_ns=UINT32_MAX;view.hold_ms=0;}
  if(observed){if(last_valid!=view.time_valid)count(valid_edges);if(last_ready!=view.relay_ready)count(ready_edges);}
  observed=true;last_valid=view.time_valid;last_ready=view.relay_ready;
  unsigned painted=0;bool ok=true;constexpr unsigned order[]={3,4,0,1,2,5,6,7,8,9,10,11,12,13,14};
  for(const auto index:order){
   render_strip(frame,index*StripRows,view);auto* prior=pixels+index*StripBytes;
   DirtyRect rect{0,0,Width,StripRows};if(valid&(1u<<index))rect=changed_pixels(frame,prior);
   if(!rect.width)continue;
   std::memcpy(prior,frame,StripBytes);const auto bytes=pack_rectangle(frame,rect);
   if(!bytes||!rectangle_window(rect.x,index*StripRows+rect.y,rect.width,rect.height)||!transmit(true,frame,bytes)){valid&=uint16_t(~(1u<<index));ok=false;break;}
   valid|=uint16_t(1u<<index);++painted;count(painted_strips);
  }
  if(painted==15)count(full_passes);
  if(ok){if(frames!=UINT32_MAX)++frames;gpio_set_level(gpio_num_t(Backlight),1);}else if(errors!=UINT32_MAX)++errors;
  const auto elapsed=uint64_t(esp_timer_get_time()-begin);if(elapsed>max_us)max_us=uint32_t(std::min<uint64_t>(elapsed,UINT32_MAX));
  if(xTaskGetTickCount()-wake>=pdMS_TO_TICKS(20))wake=xTaskGetTickCount();
  vTaskDelayUntil(&wake,pdMS_TO_TICKS(20));
 }
}
}
bool display_init(){
 if(!power_init()||!panel_init())return false;
 frame=static_cast<uint8_t*>(heap_caps_malloc(StripBytes,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
 pixels=static_cast<uint8_t*>(heap_caps_malloc(FrameBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 void* memory=heap_caps_calloc(1,sizeof(UiStorage),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!frame||!pixels||!memory||!esp_ptr_external_ram(memory))return false;
 ui_storage=new(memory)UiStorage{};
 return xTaskCreatePinnedToCore(worker,"ss3_rc7_lcd",12288,nullptr,2,&task,1)==pdPASS;
}
void sticks3::present_native(const DisplayState& s,const v12::R2Status& m,uint64_t tick,uint64_t now,const char* banner,uint64_t banner_until20,uint8_t chart){
 if(!ui_storage)return;
 // Sole main-task producer; complete PSRAM values copy outside IRQ masks.
 auto& n=ui_storage->constructed;n={};n.view=make_view(s);auto& v=n.view;v.native_dashboard=true;v.page=s.page==display_pages::Front?0:1;
 v.td=v12::r2_td_status();v.td_chart=chart%3;if(v.td.enabled)v.page=s.page%6;
 std::snprintf(v.header,sizeof v.header,"SS3");std::snprintf(v.upstream,sizeof v.upstream,"%s",s.followed_clock);
 v.relay_only=m.rc7_relay_only;n.banner_until=banner_until20;
 if(banner)std::snprintf(v.mode_banner,sizeof v.mode_banner,"%s",banner);
 v.radio_error=m.api_errors;v.drops=m.drops;v.capture_error=m.rx_capture.missed;v.native_rms_ns=s.rms_ns;v.hold_ms=s.hold_1us_ms;v.lock_seconds=s.r2_lock_seconds;v.accepted=v.td.enabled?m.pa_reports:m.g1_corrected_accepted;
 v.relay_online=(m.rc7_server_flags&8)!=0;v.relay_ready=(m.rc7_server_flags&16)!=0;v.depth=m.rc7_server_depth;
 n.clock.model=m.g1_clock_model;n.clock.tick=tick;
 // r2_status already ages and fences its value-owned canonical clock. Carry
 // that instant to the LCD sample, without renewing source/model permission.
 const auto age=now>=m.follower_snapshot_us?now-m.follower_snapshot_us:UINT64_MAX;
 if(age!=UINT64_MAX&&m.follower_utc_us<=uint64_t(INT64_MAX)/1000&&age<=uint64_t(INT64_MAX)/1000-m.follower_utc_us)
  n.clock.utc_ns=int64_t(m.follower_utc_us+age)*1000;
 n.clock.until=DisplayClock::deadline(tick,now,m.g1_anchor_deadline_us);
 // Same strict 2000ppm coarse guard as r2_status, still below249900us.
 const auto growth=age==UINT64_MAX?UINT64_MAX:age/500+unsigned(age%500!=0);
 if(growth>=249899||m.g1_anchor_guard_us>=249899-growth)n.clock.until=0;
 else {const auto left=(249899-m.g1_anchor_guard_us-growth)*500;
  if(left<UINT64_MAX/20&&tick<=UINT64_MAX-left*20)n.clock.until=std::min(n.clock.until,tick+left*20);else n.clock.until=0;}
 n.clock.model_until=DisplayClock::deadline(tick,now,m.g1_corrected_until_us);
 n.clock.model_until=std::min(n.clock.model_until,n.clock.until);
 const int slot=ui_publication.reserve();
 if(slot>=0){ui_storage->published[slot]=n;ui_publication.commit(unsigned(slot));ui_published.store(true,std::memory_order_release);}
}
void sticks3::display_status(){
 const auto d=v12::r2_td_status();const auto memory=v12::r2_td_memory();
 const TdRelayList relays(d);
 std::printf("SS3_RELAY_LIST visible=%u nearby=%u order=SMOOTHED_RSSI_DESC_ID_ASC pagination=NONE freshness_us=10000000\n",relays.count,relays.total);
 for(unsigned i=0;i<relays.count;++i){const auto& p=*relays.rows[i];
  std::printf("SS3_RELAY_ROW rank=%u id=%016llx RSSI=%d\n",i+1,(unsigned long long)p.id,int(p.signal.fresh(d.now_us)?p.signal.value:127));
 }
 const char* charts[]={"JITTER","LOSS","RSSI"};const auto chart=ui_storage?ui_storage->constructed.view.td_chart:0;const auto page=ui_storage?ui_storage->constructed.view.page:0;
 std::printf("SS3_CHART metric=%s page=%u selected_GM=%016llx selected_relay=%016llx plot_GM=%016llx plot_relay=%016llx switches=%lu dwell_us=30000000 sustain_us=15000000 lost_grace_us=20000000 hold=LAST_DASHED madctl=08 header=BLUE mode_hold=SCREEN1_ONLY\n",charts[chart%3],unsigned(page),(unsigned long long)d.chart_gm,(unsigned long long)d.chart_relay,(unsigned long long)d.plot_gm,(unsigned long long)d.plot_relay,(unsigned long)d.chart_switches);
 for(unsigned kind=0;kind<2;kind++){
  const auto id=kind?d.plot_relay:d.plot_gm;const v12::TdTrace* last=nullptr;
  for(const auto& p:(kind?d.relay:d.gm))if(p.id==id&&p.at_us&&(!last||p.at_us>last->at_us))last=&p;
  if(last)std::printf("SS3_CHART_POINT kind=%s id=%016llx at_us=%llu jitter_ns=%lu delivery=%u rssi=%d\n",kind?"RELAY":"GM",(unsigned long long)id,(unsigned long long)last->at_us,(unsigned long)last->jitter_ns,unsigned(last->delivery),int(last->rssi));
 }
 const auto pipeline=v12::periodic_rx::status();const auto publication=v12::r2_td_publication();
 std::printf("SS3_RX_PIPELINE windows_armed=%lu no_aux=%lu no_candidate=%lu status_rejects=%lu status_mask_or=%lu snapshot_read_misses=%lu snapshot_write_misses=%lu LCD_read_misses=%lu LCD_write_misses=%lu publication_PSRAM_copy_IRQ_masked=0 controller_schedule=UNCHANGED descriptor_status_bits=UNQUALIFIED\n",(unsigned long)pipeline.windows_armed,(unsigned long)pipeline.windows_no_aux,(unsigned long)pipeline.windows_no_candidate,(unsigned long)pipeline.status_rejects,(unsigned long)pipeline.status_mask_or,(unsigned long)publication.read_misses,(unsigned long)publication.write_misses,(unsigned long)ui_publication.missed_reads(),(unsigned long)ui_publication.missed_writes());
 std::printf("SS3_TD_MEMORY diagnostic_bytes=%lu LCD_queue_state_bytes=%lu PSRAM_total=%lu PSRAM_free=%lu internal_free=%lu external=%u IRQ_controller=INTERNAL_DRAM\n",(unsigned long)memory.diagnostic_bytes,(unsigned long)(ui_storage?sizeof(UiStorage):0),(unsigned long)memory.psram_total,(unsigned long)memory.psram_free,(unsigned long)memory.internal_free,unsigned(memory.external&&ui_storage&&esp_ptr_external_ram(ui_storage)));
 std::printf("SS3_TD enabled=%u request=%u pending=%u phase=%u target=%016llx request_target=%016llx request_until_us=%llu now_us=%llu visible=%u switches=%lu sync_failures=%lu unmatched=%lu passive=1 output=NONE precision=UNQUALIFIED\n",unsigned(d.enabled),unsigned(d.request),unsigned(d.pending),unsigned(d.phase),(unsigned long long)d.target,(unsigned long long)d.request_target,(unsigned long long)d.request_until_us,(unsigned long long)d.now_us,unsigned(d.count),(unsigned long)d.switches,(unsigned long)d.sync_failures,(unsigned long)d.unmatched);
 const auto word=health_word.load(std::memory_order_acquire);const auto shown_code=word&255,input_code=(word>>8)&255;
 const auto label_text=[](unsigned code){return code<sizeof(TdHealthBanner::Labels)/sizeof(*TdHealthBanner::Labels)?TdHealthBanner::Labels[code]:"HEALTH UNKNOWN";};
 char shown_text[24],input_text[24];std::snprintf(shown_text,sizeof shown_text,"%s",label_text(shown_code));std::snprintf(input_text,sizeof input_text,"%s",label_text(input_code));
 for(auto& ch:shown_text){if(ch==' ')ch='_';}
 for(auto& ch:input_text){if(ch==' ')ch='_';}
 std::printf("SS3_HEALTH_DISPLAY active=%u status=%s input=%s level=%u input_level=%u pending=%u hold_us=150000 policy=GROUP150_REASON150_V1 changes=%lu cancelled=%lu transition_min_us=%lu raw_health=IMMEDIATE control=UNCHANGED\n",unsigned(word>>31),shown_text,input_text,unsigned((word>>16)&3),unsigned((word>>18)&3),unsigned((word>>20)&1),(unsigned long)health_changes.load(std::memory_order_relaxed),(unsigned long)health_cancelled.load(std::memory_order_relaxed),(unsigned long)health_min_us.load(std::memory_order_relaxed));
 const auto health=TdHealth::evaluate(d,ui_storage?ui_storage->constructed.view.radio_error:0);
 char health_text[24];std::snprintf(health_text,sizeof health_text,"%s",health.status);for(auto& ch:health_text)if(ch==' ')ch='_';
 std::printf("SS3_HEALTH status=%s level=%u delivery=%u sample_age_ms=%lu time_valid_at_TX=%u source_recent=%u source_epoch=%lu GPS_PPS=UNKNOWN UTC_error=UNKNOWN wire_added_bytes=0 pages=6 delivery_warning_permille=850 RSSI_policy=PER_PEER_EWMA1HZ_DEADBAND2_HYST3\n",health_text,unsigned(health.level),unsigned(health.delivery),(unsigned long)health.sample_age_ms,unsigned(health.time_valid),unsigned(health.source_recent),(unsigned long)(health.gm?health.gm->source_epoch:0));
 std::printf("SS3_GM_STATUS known=%u available=%u root=%016llx hot=%016llx boot=%016llx sequence=%lu source_epoch=%lu seen_us=%llu checked_us=%llu next_us=%llu checks=%lu misses=%lu result=%u poll_until_us=%llu poll_us=120000000 authority=NONE chart=LOG10 header=BLUE\n",unsigned(d.role.known),unsigned(d.role.available),(unsigned long long)d.role.root,(unsigned long long)d.role.hot,(unsigned long long)d.role.root_boot,(unsigned long)d.role.sequence,(unsigned long)d.role.source_epoch,(unsigned long long)d.role.seen_us,(unsigned long long)d.role.checked_us,(unsigned long long)d.role.next_us,(unsigned long)d.role.checks,(unsigned long)d.role.misses,unsigned(d.role.result),(unsigned long long)d.role.until_us);
 for(const auto& p:d.peers)if(p.id)std::printf("SS3_TD_PEER id=%016llx root=%016llx relay=%u hop=%u rssi=%d rssi_min=%d rssi_max=%d publishing=%u ready=%u seen_us=%llu sampled_us=%llu adv=%lu PA=%lu matched=%lu rejected=%lu missed=%lu duplicates=%lu jitter_ns=%lu max_ns=%lu RSSI_smooth=%d bars=%u RSSI_seen_us=%llu RSSI_display_us=%llu channels=%010llx age_us=%lu PHY=%u source_epoch=%lu time_known=%u time_valid_at_TX=%u time_seen_us=%llu TX_age_us=%lu TX_ttl_us=%lu\n",(unsigned long long)p.id,(unsigned long long)p.root,unsigned(p.relay),unsigned(p.depth),int(p.rssi),int(p.rssi_min),int(p.rssi_max),unsigned(p.publishing),unsigned(p.ready),(unsigned long long)p.seen_us,(unsigned long long)p.sampled_us,(unsigned long)p.advertisements,(unsigned long)p.packets,(unsigned long)p.matched,(unsigned long)p.rejected,(unsigned long)p.missed,(unsigned long)p.duplicates,(unsigned long)p.jitter_ns,(unsigned long)p.max_ns,int(p.signal.fresh(d.now_us)?p.signal.value:127),unsigned(p.signal.fresh(d.now_us)?p.signal.bars:0),(unsigned long long)p.signal.seen_us,(unsigned long long)p.signal.display_us,(unsigned long long)p.channels,(unsigned long)p.source_age_us,unsigned(p.phy),(unsigned long)p.source_epoch,unsigned(p.time_known),unsigned(p.time_valid),(unsigned long long)p.time_seen_us,(unsigned long)p.origin_age_us,(unsigned long)p.source_ttl_us);
std::printf("SS3_LCD frames=%lu errors=%lu max_us=%lu stack_min=%lu fine=%lu coarse=%lu invalid=%lu valid_edges=%lu ready_edges=%lu strips=%lu full_passes=%lu core=1 snapshot=VALUE_OWNED expiry=CANONICAL_AND_FINE optical_scanout=UNMEASURED\n",(unsigned long)frames.load(),(unsigned long)errors.load(),(unsigned long)max_us.load(),(unsigned long)(task?uxTaskGetStackHighWaterMark(task):0),(unsigned long)fine_frames.load(),(unsigned long)coarse_frames.load(),(unsigned long)invalid_frames.load(),(unsigned long)valid_edges.load(),(unsigned long)ready_edges.load(),(unsigned long)painted_strips.load(),(unsigned long)full_passes.load());}
bool sticks3::power_command(bool download) {
 // M5PM1 SYS_CMD: documented key0xA0 + reset2 / download3. Re-read
 // identity and use the already owned PM1 bus; never alter rails/hold/efuses.
 constexpr uint8_t target[]{0x14,0xc1,0x9f,0xd5,0x60,0xdc};uint8_t mac[6]{},id=0,model=0,hw=0;
 if(esp_read_mac(mac,ESP_MAC_EFUSE_FACTORY)!=ESP_OK || std::memcmp(mac,target,6) ||
    !pm1 || !read(0,id) || !read(1,model) || !read(2,hw) || id!=0x50 || model!=0x20 || hw!=0x05)return false;
 vTaskDelay(pdMS_TO_TICKS(120));
 return write(0x0c,download?0xa3:0xa2);
}

bool display_set_scan(DisplayTiming::Profile){return true;}
void display_show(const DisplayState&){}
void display_update_indicators(bool,bool,bool){}
void display_sleep(){awake.store(false);}
void display_wake(){awake.store(true);}
void display_blank(){awake.store(false);}
