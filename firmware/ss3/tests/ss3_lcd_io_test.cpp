// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ss3_lcd_io.hpp"
#include <cassert>
#include <cstdio>
namespace {
spi_transaction_t* outstanding=nullptr;int queue_rc=0,result_rc=0;bool wrong=false;
unsigned queues=0,results=0,levels=0;uint8_t pixels[128]{};
void clear(){outstanding=nullptr;queue_rc=result_rc=0;wrong=false;queues=results=levels=0;}
}
int gpio_set_level(gpio_num_t,unsigned){++levels;return ESP_OK;}
int spi_device_queue_trans(spi_device_handle_t,spi_transaction_t* t,TickType_t wait){
 assert(wait==10&&!outstanding);++queues;if(queue_rc)return queue_rc;outstanding=t;return ESP_OK;
}
int spi_device_get_trans_result(spi_device_handle_t,spi_transaction_t** t,TickType_t wait){
 assert(wait==20&&outstanding);++results;
 if(result_rc==ESP_ERR_TIMEOUT)return result_rc;
 static spi_transaction_t other;*t=wrong?&other:outstanding;outstanding=nullptr;return result_rc;
}
int main(){
 {clear();sticks3::LcdIo io;uint8_t command=0x29;assert(io.send(nullptr,1,false,&command,1,pixels,sizeof pixels));assert(!io.pending()&&queues==1&&results==1);}
 {clear();sticks3::LcdIo io;result_rc=ESP_ERR_TIMEOUT;uint8_t command=0x2a;
  assert(!io.send(nullptr,1,false,&command,1,pixels,sizeof pixels)&&io.pending());command=0;
  assert(outstanding->tx_data[0]==0x2a);const auto queued=queues,gpio=levels;
  assert(!io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels)&&queues==queued&&levels==gpio);
  result_rc=0;assert(io.complete(nullptr)&&!io.pending());assert(io.timeouts()==1);}
 {clear();sticks3::LcdIo io;result_rc=ESP_ERR_TIMEOUT;
  {uint8_t gamma[14];std::memset(gamma,0x71,sizeof gamma);assert(!io.send(nullptr,1,true,gamma,sizeof gamma,pixels,sizeof pixels));std::memset(gamma,0,sizeof gamma);}
  const auto* held=static_cast<const uint8_t*>(outstanding->tx_buffer);for(unsigned i=0;i<14;i++)assert(held[i]==0x71);
  result_rc=0;assert(io.complete(nullptr));}
 {clear();sticks3::LcdIo io;result_rc=ESP_ERR_TIMEOUT;std::memset(pixels,0x32,sizeof pixels);
  assert(!io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels));assert(outstanding->tx_buffer==pixels);
  for(unsigned i=0;i<100;++i)assert(!io.complete(nullptr));assert(io.pending()&&queues==1);
  result_rc=0;assert(io.complete(nullptr));std::memset(pixels,0,sizeof pixels);assert(io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels));}
 {clear();sticks3::LcdIo io;queue_rc=ESP_ERR_TIMEOUT;assert(!io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels)&&!io.pending());queue_rc=0;assert(io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels));}
 {clear();sticks3::LcdIo io;result_rc=ESP_ERR_INVALID_STATE;assert(!io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels)&&!io.pending()&&!io.failed());result_rc=0;assert(io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels));}
 {clear();sticks3::LcdIo io;wrong=true;assert(!io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels)&&io.pending()&&io.failed());assert(!io.complete(nullptr));}
 {clear();sticks3::LcdIo io;queue_rc=99;assert(!io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels)&&io.pending()&&io.failed());assert(!io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels)&&queues==1);}
 {clear();sticks3::LcdIo io;uint8_t stack[64]{};assert(!io.send(nullptr,1,true,stack,sizeof stack,pixels,sizeof pixels)&&!queues);}
 {clear();sticks3::LcdIo io;for(unsigned i=0;i<1000000;++i)assert(io.send(nullptr,1,true,pixels,sizeof pixels,pixels,sizeof pixels));assert(!io.pending()&&!io.failed()&&!io.errors()&&!io.timeouts());}
 puts("PASS LCD actual IO owner: finite waits,persistent command/DMA storage,late completion,no reuse while pending,errors and million transfers");
}
