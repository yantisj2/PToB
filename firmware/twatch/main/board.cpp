// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Register sequences derived from LILYGO/SensorLib; see NOTICE.md.
#include "board.hpp"
#include "watch.hpp"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <algorithm>
#include <cstring>
namespace watchboard {
namespace {
i2c_master_bus_handle_t bus=nullptr;i2c_master_dev_handle_t pmu=nullptr,io=nullptr,tp=nullptr;
spi_device_handle_t panel=nullptr;bool initialized=false,storage=false,panel_failed=false,in_flight=false;
DMA_ATTR uint16_t strip[watch::Width*8];
bool read(i2c_master_dev_handle_t d,uint8_t reg,uint8_t& out){return i2c_master_transmit_receive(d,&reg,1,&out,1,20)==ESP_OK;}
bool write(i2c_master_dev_handle_t d,uint8_t reg,uint8_t value){uint8_t p[]={reg,value};return i2c_master_transmit(d,p,2,20)==ESP_OK;}
bool bits(i2c_master_dev_handle_t d,uint8_t reg,uint8_t mask,uint8_t value){uint8_t old;return read(d,reg,old)&&write(d,reg,(old&~mask)|(value&mask));}
bool device(uint8_t address,i2c_master_dev_handle_t* out){i2c_device_config_t c{};c.dev_addr_length=I2C_ADDR_BIT_LEN_7;c.device_address=address;c.scl_speed_hz=400000;return i2c_master_probe(bus,address,20)==ESP_OK&&i2c_master_bus_add_device(bus,&c,out)==ESP_OK;}
// Finite queued transfers. A timeout permanently fences this display owner:
// the DMA buffer and transaction stay alive until reset, never reused in flight.
spi_transaction_t transaction{};
bool transfer(uint8_t cmd,uint32_t addr,const void* data,unsigned bytes,uint32_t flags=0){if(panel_failed)return false;transaction={};transaction.cmd=cmd;transaction.addr=addr;transaction.tx_buffer=bytes?data:nullptr;transaction.length=bytes*8;transaction.flags=flags;gpio_set_level(GPIO_NUM_41,0);spi_transaction_t* done=nullptr;if(spi_device_queue_trans(panel,&transaction,pdMS_TO_TICKS(10))!=ESP_OK){panel_failed=true;return false;}in_flight=true;if(spi_device_get_trans_result(panel,&done,pdMS_TO_TICKS(30))!=ESP_OK||done!=&transaction){panel_failed=true;return false;}in_flight=false;gpio_set_level(GPIO_NUM_41,1);return true;}
DMA_ATTR uint8_t parameters[8];
bool command(uint8_t cmd,const uint8_t* data=nullptr,unsigned n=0){if(panel_failed||n>sizeof parameters)return false;if(n)std::memcpy(parameters,data,n);return transfer(0x02,uint32_t(cmd)<<8,parameters,n);}
bool byte(uint8_t cmd,uint8_t value){return command(cmd,&value,1);}
}
bool init(){if(initialized)return true;i2c_master_bus_config_t c{};c.i2c_port=I2C_NUM_0;c.sda_io_num=GPIO_NUM_3;c.scl_io_num=GPIO_NUM_2;c.clk_source=I2C_CLK_SRC_DEFAULT;c.glitch_ignore_cnt=7;c.flags.enable_internal_pullup=true;if(i2c_new_master_bus(&c,&bus)!=ESP_OK||!device(0x34,&pmu)||!device(0x20,&io))return false;uint8_t id;if(!read(pmu,3,id)||id!=0x4a)return false;
 // Preserve charging, CPU and backup configuration. Gate only reviewed peripheral rails.
 if(!bits(pmu,0x93,0x1f,28)||!bits(pmu,0x94,0x1f,28)||!bits(pmu,0x90,0xbf,0x06))return false;
 // XL9555 P0.7 display enable; P0.6 haptics off; P1.0 touch reset; P1.3 internal antenna.
 if(!bits(io,2,0xc0,0x80)||!bits(io,3,0x09,0x08)||!bits(io,6,0xc0,0)||!bits(io,7,0x09,0))return false;
 vTaskDelay(pdMS_TO_TICKS(20));if(!bits(io,3,0x01,0x01))return false;vTaskDelay(pdMS_TO_TICKS(60));if(!device(0x1a,&tp))return false;
 // Deselect the unused devices on the shared radio bus before radio startup.
 gpio_config_t pins{};pins.pin_bit_mask=(1ULL<<4)|(1ULL<<21)|(1ULL<<36);pins.mode=GPIO_MODE_OUTPUT;
 for(auto pin:{GPIO_NUM_4,GPIO_NUM_21,GPIO_NUM_36})if(gpio_set_level(pin,1)!=ESP_OK)return false;
 if(gpio_config(&pins)!=ESP_OK)return false;
 storage=nvs_flash_init()==ESP_OK; // Never erase or migrate unknown factory storage on an error.
 initialized=true;return true;
}
bool idle(){return !in_flight;}
bool display_init(){if(!initialized)return false;gpio_config_t pins{};pins.pin_bit_mask=(1ULL<<37)|(1ULL<<41);pins.mode=GPIO_MODE_OUTPUT;if(gpio_config(&pins)!=ESP_OK)return false;gpio_set_level(GPIO_NUM_41,1);gpio_set_level(GPIO_NUM_37,0);vTaskDelay(pdMS_TO_TICKS(20));gpio_set_level(GPIO_NUM_37,1);vTaskDelay(pdMS_TO_TICKS(120));
 spi_bus_config_t b{};b.sclk_io_num=40;b.data0_io_num=38;b.data1_io_num=39;b.data2_io_num=42;b.data3_io_num=45;b.data4_io_num=b.data5_io_num=b.data6_io_num=b.data7_io_num=-1;b.max_transfer_sz=sizeof strip;
 if(spi_bus_initialize(SPI3_HOST,&b,SPI_DMA_CH_AUTO)!=ESP_OK)return false;
 spi_device_interface_config_t d{};d.command_bits=8;d.address_bits=24;d.clock_speed_hz=40000000;d.spics_io_num=-1;d.flags=SPI_DEVICE_HALFDUPLEX;d.queue_size=1;if(spi_bus_add_device(SPI3_HOST,&d,&panel)!=ESP_OK)return false;
 for(int repeat=0;repeat<2;++repeat){if(!byte(0xfe,0)||!byte(0xc4,0x80)||!byte(0x3a,0x55)||!byte(0x35,0)||!byte(0x53,0x20)||!byte(0x63,0xff)||!byte(0x36,0)||!command(0x11))return false;vTaskDelay(pdMS_TO_TICKS(120));if(!command(0x29))return false;vTaskDelay(pdMS_TO_TICKS(120));}return brightness(0);
}
bool brightness(uint8_t percent){return byte(0x51,uint8_t(unsigned(std::min(percent,uint8_t(100)))*255/100));}
bool sleep(bool asleep){return command(asleep?0x28:0x29);}
bool flush(const uint16_t* frame){if(panel_failed)return false;for(int y=0;y<watch::Height;y+=8){int h=std::min(8,watch::Height-y);uint8_t xs[]={0,22,1,0xaf},ys[]={uint8_t(y>>8),uint8_t(y),uint8_t((y+h-1)>>8),uint8_t(y+h-1)};if(!command(0x2a,xs,4)||!command(0x2b,ys,4))return false;for(int i=0;i<watch::Width*h;++i){auto v=frame[y*watch::Width+i];strip[i]=uint16_t((v>>8)|(v<<8));}if(!transfer(0x32,0x002c00,strip,watch::Width*h*2,SPI_TRANS_MODE_QIO))return false;taskYIELD();}return true;}
bool touch(int& x,int& y){uint8_t reg[]={0xd0,0},buf[30]{},ack[]={0xd0,0,0xab};if(i2c_master_transmit_receive(tp,reg,2,buf,sizeof buf,10)!=ESP_OK||i2c_master_transmit(tp,ack,3,10)!=ESP_OK)return false;if(buf[0]==0xab||buf[6]!=0xab||(buf[0]&15)!=6||(buf[5]&0x7f)==0||(buf[4]&0xf0))return false;x=(buf[1]<<4)|(buf[3]>>4);y=(buf[2]<<4)|(buf[3]&15);return x<watch::Width&&y<watch::Height;}
int battery(){uint8_t b;return read(pmu,0xa4,b)&&b<=100?b:-1;}
bool settings_load(void* data,unsigned size){if(!storage)return false;nvs_handle_t h;if(nvs_open("ptobwatch",NVS_READONLY,&h)!=ESP_OK)return false;size_t n=size;bool ok=nvs_get_blob(h,"settings",data,&n)==ESP_OK&&n==size;nvs_close(h);return ok;}
bool settings_save(const void* data,unsigned size){if(!storage)return false;nvs_handle_t h;if(nvs_open("ptobwatch",NVS_READWRITE,&h)!=ESP_OK)return false;bool ok=nvs_set_blob(h,"settings",data,size)==ESP_OK&&nvs_commit(h)==ESP_OK;nvs_close(h);return ok;}
}
