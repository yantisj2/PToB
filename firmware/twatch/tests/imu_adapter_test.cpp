// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Runs the production adapter with a deterministic Bosch/board boundary.
#include "imu.hpp"
#include "bhy2.h"
#include "driver/gpio.h"
#include <cassert>
#include <array>
#include <vector>
#include <cstring>
static uint64_t now=1000000;static bool irq=true,bad_orientation=false;static int8_t fifo_error=0;
static bhy2_read_fptr_t transport_read=nullptr;static int transport_error=0;static unsigned transfer_us=30000,transport_calls=0,fail_calls=999;static uint8_t transport_reg=2;
static bhy2_fifo_parse_callback_t callbacks[256]{};static float rates[256]{};
struct Event {uint8_t id;std::array<uint8_t,6> data{};uint64_t sample_us=0;};static std::vector<Event> queue;
int64_t esp_timer_get_time(){return now;}void esp_rom_delay_us(uint32_t us){now+=us;}void vTaskDelay(uint32_t ms){now+=ms*1000;}
int gpio_config(const gpio_config_t*){return 0;}int gpio_get_level(int){return irq;}
namespace watchboard {bool imu_bus_open(){return true;}int imu_read(uint8_t,uint8_t*,unsigned){now+=transfer_us;return ++transport_calls<=fail_calls?transport_error:0;}int imu_write(uint8_t,const uint8_t*,unsigned){return 0;}}
extern "C" {
int8_t bhy2_init(bhy2_intf,bhy2_read_fptr_t read,bhy2_write_fptr_t,bhy2_delay_us_fptr_t,uint32_t,void*,bhy2_dev*){transport_read=read;return 0;}
int8_t bhy2_get_product_id(uint8_t* p,bhy2_dev*){*p=BHY2_PRODUCT_ID;return 0;}
int8_t bhy2_soft_reset(bhy2_dev*){return 0;}
int8_t bhy2_set_host_interrupt_ctrl(uint8_t,bhy2_dev*){return 0;}
int8_t bhy2_get_host_interrupt_ctrl(uint8_t* p,bhy2_dev*){*p=BHY2_ICTL_DISABLE_STATUS_FIFO|BHY2_ICTL_DISABLE_DEBUG;return 0;}
int8_t bhy2_set_host_intf_ctrl(uint8_t,bhy2_dev*){return 0;}
int8_t bhy2_upload_firmware_to_ram(const uint8_t*,uint32_t,bhy2_dev*){return 0;}
int8_t bhy2_boot_from_ram(bhy2_dev*){return 0;}
int8_t bhy2_get_kernel_version(uint16_t* p,bhy2_dev*){*p=123;return 0;}
int8_t bhy2_register_fifo_parse_callback(uint8_t id,bhy2_fifo_parse_callback_t cb,void*,bhy2_dev*){callbacks[id]=cb;return 0;}
int8_t bhy2_update_virtual_sensor_list(bhy2_dev*){return 0;}
uint8_t bhy2_is_sensor_available(uint8_t,const bhy2_dev*){return 1;}
int8_t bhy2_set_orientation_matrix(uint8_t,bhy2_orient_matrix m,bhy2_dev*){assert(m.c[0]==-1&&m.c[4]==-1&&m.c[8]==1);return 0;}
int8_t bhy2_get_orientation_matrix(uint8_t,bhy2_orient_matrix* m,bhy2_dev*){*m={{-1,0,0,0,-1,0,0,0,1}};if(bad_orientation)m->c[0]=1;return 0;}
int8_t bhy2_set_virt_sensor_cfg(uint8_t id,bhy2_float rate,uint32_t,bhy2_dev*){rates[id]=rate;return 0;}
int8_t bhy2_get_virt_sensor_cfg(uint8_t id,bhy2_virt_sensor_conf* c,bhy2_dev*){*c={};c->sample_rate=rates[id];return 0;}
int8_t bhy2_get_and_process_fifo(uint8_t*,uint32_t,bhy2_dev*){
 if(fifo_error)return fifo_error;
 if(transport_error){uint8_t b=0;return transport_read(transport_reg,&b,1,nullptr)?BHY2_E_IO:BHY2_OK;}
 for(auto e:queue){uint64_t stamp=(e.sample_us?e.sample_us:now)*8/125;bhy2_fifo_parse_data_info info{};info.sensor_id=e.id;info.data_ptr=e.data.data();info.time_stamp=&stamp;
 info.data_size=e.id==BHY2_SENSOR_ID_ACC?7:e.id==BHY2_SYS_ID_META_EVENT?4:1;
 if(e.id==BHY2_SENSOR_ID_GLANCE_GESTURE)rates[e.id]=0;
 if(callbacks[e.id])callbacks[e.id](&info,nullptr);}
 queue.clear();return 0;
}
}
static watchimu::Motion poll(bool asleep,bool glance=false,uint64_t dt=100000){now+=dt;return watchimu::poll(now,asleep,glance);}
static watchimu::Motion accel(int16_t x,int16_t y,int16_t z,bool asleep=true,bool glance=false){
 Event e{BHY2_SENSOR_ID_ACC,{}};const int16_t values[]={x,y,z};
 for(unsigned i=0;i<3;++i){e.data[2*i]=uint16_t(values[i])&255;e.data[2*i+1]=uint16_t(values[i])>>8;}
 queue.push_back(e);return poll(asleep,glance,80000);
}
int main(){using watchimu::Motion;
 assert(watchimu::init());auto s=watchimu::status();assert(s.orientation_ok&&s.available==7&&s.accel_millihz==12500);
 poll(false);assert(!watchimu::status().armed);
 // Buffered Bosch tilt is only a witness, never a host raise.
 irq=false;queue.push_back({BHY2_SENSOR_ID_WRIST_TILT_GESTURE,{}});assert(poll(true,false,800000)==Motion::None);
 assert(watchimu::status().tilt_events==1&&watchimu::status().wake_events==0);
 irq=true;poll(true,false,1600000);assert(watchimu::status().armed);
 queue.push_back({BHY2_SENSOR_ID_GLANCE_GESTURE,{}});assert(poll(true)==Motion::None);assert(watchimu::status().events==1);
 // Settled lowered -> viewing direction wakes once without Bosch event.
 for(int i=0;i<12;++i)assert(accel(700,3100,2600)==Motion::None);
 assert(watchimu::status().pose_prepared);
 unsigned raises=0;bool glance=false;
 for(int i=0;i<12;++i){auto m=accel(0,-3700,1800,!glance,glance);if(m==Motion::Raise){++raises;glance=true;}}
 assert(raises==1&&watchimu::status().return_active);
 // Reverse to lowered pose dismisses; a tilt witness cannot wake on lowering.
 queue.push_back({BHY2_SENSOR_ID_WRIST_TILT_GESTURE,{}});
 unsigned lowers=0;for(int i=0;i<12;++i)if(accel(700,3100,2600,false,true)==Motion::Lower)++lowers;
 assert(lowers==1&&watchimu::status().lowered==1);
 assert(rates[BHY2_SENSOR_ID_WRIST_TILT_GESTURE]==1&&rates[BHY2_SENSOR_ID_GLANCE_GESTURE]==0);
 irq=false;queue.push_back({BHY2_SENSOR_ID_ACC,{0,0,0,0,0,4}});assert(poll(false,false,600000)==Motion::None);
 s=watchimu::status();assert(s.z==1024&&s.stale_polls>0&&s.sample_age_ms==0);
 irq=true;queue.push_back({BHY2_SYS_ID_META_EVENT,{BHY2_META_EVENT_RESET,0,0}});assert(poll(true)==Motion::None);assert(!watchimu::status().ready&&watchimu::status().resets==1);
 assert(watchimu::init());poll(true);
 for(int i=0;i<12;++i)assert(accel(700,3100,2600)==Motion::None);
 assert(watchimu::status().pose_prepared);
 // A stalled host cannot reinterpret an old queued viewing sequence as a
 // current raise. The stale service drains with pose recognition disarmed.
 for(int i=1;i<=8;++i){Event e{BHY2_SENSOR_ID_ACC,{0,0,0x8c,0xf1,0x08,0x07},now+uint64_t(i)*80000};queue.push_back(e);}
 assert(poll(true,false,800000)==Motion::None);assert(watchimu::status().wake_events==0&&!watchimu::status().pose_prepared);
 assert(watchimu::init());assert(watchimu::status().events==0&&!watchimu::status().armed);
 fifo_error=-3;assert(poll(true)==Motion::None);assert(!watchimu::status().ready&&watchimu::status().error==-3);fifo_error=0;
 assert(watchimu::init());fifo_error=BHY2_E_INVALID_PARAM;
 assert(poll(true)==Motion::None);assert(!watchimu::status().ready&&watchimu::status().error==BHY2_E_INVALID_PARAM);
 assert(poll(true)==Motion::None&&!watchimu::status().ready);fifo_error=0;
 assert(watchimu::init());transport_error=0x100;transport_calls=0;
 assert(poll(true)==Motion::None);s=watchimu::status();assert(!s.ready&&s.bus_rc==0x100&&s.bus_reg==2&&s.bus_len==1&&s.bus_work_us==30000&&s.bus_errors==1);
 // Only short INVALID_STATE interrupt-status reads are retried once.
 transport_error=0;assert(watchimu::init());transport_error=0x103;transport_reg=0x2d;transfer_us=500;transport_calls=0;fail_calls=1;
 assert(poll(true)==Motion::None);s=watchimu::status();assert(s.ready&&s.bus_retries==1&&s.bus_recovered==1&&s.bus_errors==1&&s.bus_rc==0x103);
 transport_error=0;assert(watchimu::init());transport_error=0x103;transport_calls=0;fail_calls=999;
 poll(true);s=watchimu::status();assert(!s.ready&&transport_calls==2&&s.bus_retries==1&&s.bus_recovered==0);
 transport_error=0;assert(watchimu::init());transport_error=0x103;transport_calls=0;transfer_us=30000;
 poll(true);assert(!watchimu::status().ready&&transport_calls==1&&watchimu::status().bus_retries==0);
 transport_error=0;bad_orientation=true;assert(!watchimu::init());assert(!watchimu::status().orientation_ok);
}
