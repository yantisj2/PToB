// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#ifdef ESP_PLATFORM
#include "esp_memory_utils.h"
#endif
namespace sticks3 {
// One LCD owner. Descriptor/control bytes stay in internal DRAM. A timed-out
// DMA transaction retains its descriptor and pixel buffer until actual return.
class LcdIo {
  spi_transaction_t transaction{};
  alignas(4) uint8_t control[16]{};
  std::atomic<bool> owned{false},fault{false};
  std::atomic<uint32_t> timeout_count{0},error_count{0};
  static void count(std::atomic<uint32_t>& n){if(n!=UINT32_MAX)++n;}
public:
  bool internal_storage()const{
#ifdef ESP_PLATFORM
    return esp_ptr_internal(&transaction)&&esp_ptr_dma_capable(control);
#else
    return true;
#endif
  }
  bool pending()const{return owned.load();}
  bool failed()const{return fault.load();}
  uint32_t timeouts()const{return timeout_count.load();}
  uint32_t errors()const{return error_count.load();}
  bool complete(spi_device_handle_t device){
    if(fault)return false;
    if(!owned)return true;
    spi_transaction_t* done=nullptr;
    const int rc=spi_device_get_trans_result(device,&done,pdMS_TO_TICKS(20));
    if(rc==ESP_ERR_TIMEOUT){count(timeout_count);return false;}
    // The pinned SDK returns INVALID_STATE after retiring a DMA-error result.
    if(done==&transaction&&(rc==ESP_OK||rc==ESP_ERR_INVALID_STATE)){
      owned=false;if(rc!=ESP_OK){count(error_count);return false;}return true;
    }
    // Unknown completion/ownership: never reuse buffers or tear down the bus.
    fault=true;count(error_count);return false;
  }
  bool send(spi_device_handle_t device,gpio_num_t dc,bool data,const void* buffer,size_t bytes,
            const uint8_t* pixels,size_t pixel_capacity){
    if(!bytes)return true;
    if(owned||fault||!buffer)return false;
    if(bytes>sizeof control&&(buffer!=pixels||bytes>pixel_capacity))return false;
    if(gpio_set_level(dc,data)!=ESP_OK){count(error_count);return false;}
    transaction={};transaction.length=bytes*8;
    if(bytes<=sizeof transaction.tx_data){transaction.flags=SPI_TRANS_USE_TXDATA;std::memcpy(transaction.tx_data,buffer,bytes);}
    else if(bytes<=sizeof control){std::memcpy(control,buffer,bytes);transaction.tx_buffer=control;}
    else transaction.tx_buffer=buffer;
    const int rc=spi_device_queue_trans(device,&transaction,pdMS_TO_TICKS(10));
    if(rc!=ESP_OK){
      count(error_count);
      if(rc==ESP_ERR_TIMEOUT)count(timeout_count);
      // TIMEOUT/NO_MEM precede admission in the pinned SDK. An unexpected
      // error may follow queue admission: retain all storage conservatively.
      else if(rc!=ESP_ERR_NO_MEM){owned=true;fault=true;}
      return false;
    }
    owned=true;return complete(device);
  }
};
}
