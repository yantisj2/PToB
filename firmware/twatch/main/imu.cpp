// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Measured-direction settled-pose trial; Bosch tilt is diagnostic only.
#include "imu.hpp"
#include <algorithm>
#include <cstring>
#include "wrist_motion.hpp"
#include "board.hpp"
#include "bhy2.h"
#include "BHI260AP.fw.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace watchimu {
namespace {
watch::WristPose wrist;
bool pose_allow=false,pose_glancing=false,lower_event=false,retried_status=false;
bhy2_dev hub{};Status state{};uint8_t fifo[512];uint64_t deadline=0,last_event=0;bool event=false,armed=false;uint64_t arm_after=0,last_poll=0,last_service=0;
uint64_t clock_us(){return uint64_t(esp_timer_get_time());}
int8_t bus_result(int rc,uint8_t reg,uint32_t n,bool writing,uint64_t start){
 const auto duration=uint32_t(clock_us()-start);state.bus_max_us=std::max(state.bus_max_us,duration);
 if(rc){++state.bus_errors;if(!state.bus_rc){state.bus_rc=rc;state.bus_reg=reg;state.bus_len=n;state.bus_write=writing;state.bus_at_us=start;state.bus_work_us=duration;}}
 return rc?-1:0; // ESP errors are wider than the Bosch int8_t transport result.
}
int8_t read(uint8_t reg,uint8_t* data,uint32_t n,void*){
 const auto start=clock_us();if(start>=deadline){++state.budget_errors;return -1;}
 const int rc=watchboard::imu_read(reg,data,n);
 const auto result=bus_result(rc,reg,n,false,start);
 // Retry only a non-consuming interrupt-status read, once per service, when
 // a complete 30 ms transfer still fits. Never replay a FIFO or command.
 if(rc==0x103&&reg==0x2d&&n==1&&!retried_status&&clock_us()+30000<deadline){
  retried_status=true;++state.bus_retries;const auto again=clock_us();
  const auto retry=bus_result(watchboard::imu_read(reg,data,n),reg,n,false,again);
  if(!retry)++state.bus_recovered;
  return retry;
 }
 return result;
}
int8_t write(uint8_t reg,const uint8_t* data,uint32_t n,void*){
 const auto start=clock_us();if(start>=deadline){++state.budget_errors;return -1;}
 return bus_result(watchboard::imu_write(reg,data,n),reg,n,true,start);
}
void delay(uint32_t us,void*){if(clock_us()>=deadline)return;if(us>=1000)vTaskDelay(pdMS_TO_TICKS((us+999)/1000));else esp_rom_delay_us(us);}
bool check(int8_t result){if(result==BHY2_OK)return true;state.error=result;++state.errors;state.ready=false;return false;}
void gesture(const bhy2_fifo_parse_data_info* info,void*){
 if(info->data_size!=1)return;
 if(info->sensor_id==BHY2_SENSOR_ID_GLANCE_GESTURE){++state.events;return;}
 if(info->sensor_id==BHY2_SENSOR_ID_WRIST_TILT_GESTURE){++state.tilt_events;}
}
void acceleration(const bhy2_fifo_parse_data_info* info,void*){
 if(info->data_size!=7)return;
 const auto* b=info->data_ptr;
 auto axis=[&](unsigned i){return int16_t(uint16_t(b[i])|(uint16_t(b[i+1])<<8));};
 state.x=axis(0);state.y=axis(2);state.z=axis(4);state.sensor_us=*info->time_stamp*125/8;state.last_sample_us=clock_us();
 const auto motion=wrist.sample(state.x,state.y,state.z,state.sensor_us,pose_allow,pose_glancing);++state.samples;
 if(motion==1){++state.wake_events;event=true;state.last_event_us=clock_us();pose_allow=false;pose_glancing=true;}
 if(motion==2)lower_event=true;
}
void meta(const bhy2_fifo_parse_data_info* info,void*){
 if(info->data_size!=4)return;
 state.last_meta=info->data_ptr[0];state.meta_sensor=info->data_ptr[1];state.meta_value=info->data_ptr[2];
 if(state.last_meta==BHY2_META_EVENT_FIFO_OVERFLOW){++state.errors;++state.fifo_overflows;event=false;wrist.reset();}
 if(state.last_meta==BHY2_META_EVENT_SENSOR_ERROR){++state.sensor_errors;++state.errors;state.ready=false;event=false;wrist.reset();}
 if(state.last_meta==BHY2_META_EVENT_RESET){++state.resets;if(state.ready){state.ready=false;++state.errors;event=false;wrist.reset();}}
}
}
bool init(){
 state={};hub={};wrist=watch::WristPose{};pose_allow=pose_glancing=lower_event=retried_status=false;event=false;armed=false;arm_after=last_event=last_poll=last_service=0;state.stage=1;deadline=clock_us()+8000000;
 if(!watchboard::imu_bus_open()){state.error=-1;++state.errors;return false;}
 gpio_config_t pin{};pin.pin_bit_mask=1ULL<<8;pin.mode=GPIO_MODE_INPUT;
 if(gpio_config(&pin)!=ESP_OK)return check(-1);
 if(!check(bhy2_init(BHY2_I2C_INTERFACE,read,write,delay,128,nullptr,&hub)))return false;
 uint8_t id=0;if(!check(bhy2_get_product_id(&id,&hub)))return false;
 if(id!=BHY2_PRODUCT_ID)return check(-1);
 state.stage=2;
 if(!check(bhy2_soft_reset(&hub))||!check(bhy2_set_host_interrupt_ctrl(BHY2_ICTL_DISABLE_STATUS_FIFO|BHY2_ICTL_DISABLE_DEBUG,&hub))||!check(bhy2_set_host_intf_ctrl(0,&hub)))return false;
 state.stage=3;
 // RAM only: do not rewrite the sensor's external flash or require a special factory image.
 if(!check(bhy2_upload_firmware_to_ram(bhy2_firmware_image,sizeof bhy2_firmware_image,&hub)))return false;
 state.stage=4;if(!check(bhy2_boot_from_ram(&hub)))return false;
 if(!check(bhy2_get_kernel_version(&state.kernel,&hub))||!state.kernel)return check(-1);
 if(!check(bhy2_get_host_interrupt_ctrl(&state.interrupt_ctrl,&hub)))return false;
 state.stage=5;
 if(!check(bhy2_register_fifo_parse_callback(BHY2_SYS_ID_META_EVENT,meta,nullptr,&hub))||!check(bhy2_register_fifo_parse_callback(BHY2_SYS_ID_META_EVENT_WU,meta,nullptr,&hub))||!check(bhy2_get_and_process_fifo(fifo,sizeof fifo,&hub))||!check(bhy2_update_virtual_sensor_list(&hub)))return false;
 state.available=(bhy2_is_sensor_available(BHY2_SENSOR_ID_GLANCE_GESTURE,&hub)?1:0)|(bhy2_is_sensor_available(BHY2_SENSOR_ID_ACC,&hub)?2:0)|(bhy2_is_sensor_available(BHY2_SENSOR_ID_WRIST_TILT_GESTURE,&hub)?4:0);
 if((state.available&6)!=6)return check(-1);
 // Ultra production mounting: TOP_LAYER_BOTTOM_RIGHT_CORNER (P2), LILYGO.
 bhy2_orient_matrix orientation{{-1,0,0,0,-1,0,0,0,1}};
 state.stage=6;
 if(!check(bhy2_set_orientation_matrix(BHY2_PHYS_SENSOR_ID_ACCELEROMETER,orientation,&hub))||!check(bhy2_set_orientation_matrix(BHY2_PHYS_SENSOR_ID_GYROSCOPE,orientation,&hub)))return false;
 bhy2_orient_matrix acc{},gyro{};
 if(!check(bhy2_get_orientation_matrix(BHY2_PHYS_SENSOR_ID_ACCELEROMETER,&acc,&hub))||!check(bhy2_get_orientation_matrix(BHY2_PHYS_SENSOR_ID_GYROSCOPE,&gyro,&hub)))return false;
 state.orientation_ok=!std::memcmp(acc.c,orientation.c,9)&&!std::memcmp(gyro.c,orientation.c,9);
 if(!state.orientation_ok)return check(-1);
 if(!check(bhy2_register_fifo_parse_callback(BHY2_SENSOR_ID_GLANCE_GESTURE,gesture,nullptr,&hub))||!check(bhy2_set_virt_sensor_cfg(BHY2_SENSOR_ID_GLANCE_GESTURE,0.0f,0,&hub)))return false;
 if(!check(bhy2_register_fifo_parse_callback(BHY2_SENSOR_ID_ACC,acceleration,nullptr,&hub))||!check(bhy2_set_virt_sensor_cfg(BHY2_SENSOR_ID_ACC,12.5f,0,&hub)))return false;
 // Keep the on-change detector running; gate display wake on the host.
 // Disabling/re-enabling this stream at each glance would reset its history.
 if(state.available&4)if(!check(bhy2_register_fifo_parse_callback(BHY2_SENSOR_ID_WRIST_TILT_GESTURE,gesture,nullptr,&hub))||!check(bhy2_set_virt_sensor_cfg(BHY2_SENSOR_ID_WRIST_TILT_GESTURE,1.0f,0,&hub)))return false;
 bhy2_virt_sensor_conf conf{};
 if(!check(bhy2_get_virt_sensor_cfg(BHY2_SENSOR_ID_ACC,&conf,&hub)))return false;
 ++state.config_reads;state.accel_millihz=uint32_t(conf.sample_rate*1000);
 if(state.accel_millihz!=12500)return check(-1);
 event=false;state.ready=true;state.stage=7;return true;
}
Motion poll(uint64_t now,bool asleep,bool glancing){
 ++state.polls;state.sampled_us=now;state.irq=gpio_get_level(GPIO_NUM_8)!=0;if(state.irq)++state.irq_high;
 if(last_poll&&now>=last_poll)state.max_poll_gap_us=std::max(state.max_poll_gap_us,uint32_t(std::min(uint64_t(UINT32_MAX),now-last_poll)));
 last_poll=now;
 if(!glancing)wrist.cancel_return();
 if(!state.ready)return Motion::None;
 deadline=clock_us()+50000;retried_status=false;
 if(!asleep){
  arm_after=now+700000;
  if(armed){armed=false;++state.disarms;}
 }
 const bool stale=!state.last_sample_us||now-state.last_sample_us>500000;
 event=lower_event=false;
 pose_allow=asleep&&armed&&!stale;pose_glancing=glancing;
 const bool arm_due=asleep&&!armed&&now>=arm_after;
 if(state.irq||arm_due||(stale&&now-last_service>=500000)){
  if(!state.irq&&stale)++state.stale_polls;
  const auto begin=clock_us();last_service=now;++state.services;
  if(!check(bhy2_get_and_process_fifo(fifo,sizeof fifo,&hub)))return Motion::None;
  state.max_service_us=std::max(state.max_service_us,uint32_t(clock_us()-begin));
 }
 if(!state.ready)return Motion::None;
 if(!event&&asleep&&!armed&&now>=arm_after){
  armed=true;++state.arms;
  bhy2_virt_sensor_conf conf{};
  if(!check(bhy2_get_virt_sensor_cfg(BHY2_SENSOR_ID_WRIST_TILT_GESTURE,&conf,&hub)))return Motion::None;
  ++state.config_reads;state.rate_millihz=uint32_t(conf.sample_rate*1000);
 }
 if(glancing&&lower_event){++state.lowered;return Motion::Lower;}
 if(!event)return Motion::None;
 const bool was_armed=armed;armed=false;arm_after=now+1500000;
 if(!asleep){++state.ignored_awake;return Motion::None;}
 if(!was_armed){++state.ignored_disarmed;return Motion::None;}
 if(last_event&&now-last_event<1500000){++state.ignored_refractory;return Motion::None;}
 if(!state.last_sample_us||clock_us()-state.last_sample_us>400000){++state.ignored_stale;return Motion::None;}
 last_event=now;return Motion::Raise;
}
Status status(){state.armed=armed;state.return_active=wrist.returning;state.pose_prepared=wrist.prepared;state.pose_rejected=wrist.rejections;state.sample_age_ms=state.last_sample_us?uint32_t(std::min(uint64_t(UINT32_MAX),(clock_us()-state.last_sample_us)/1000)):UINT32_MAX;return state;} // single UI owner; snapshot copied under display lock
}
