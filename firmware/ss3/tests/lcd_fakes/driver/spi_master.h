#pragma once
#include <cstddef>
#include <cstdint>
using spi_device_handle_t=void*;
using TickType_t=uint32_t;
constexpr int ESP_OK=0,ESP_ERR_TIMEOUT=1,ESP_ERR_NO_MEM=2,ESP_ERR_INVALID_STATE=3;
constexpr uint32_t SPI_TRANS_USE_TXDATA=4;
constexpr TickType_t pdMS_TO_TICKS(unsigned ms){return ms;}
struct spi_transaction_t {uint32_t flags=0;size_t length=0;union {const void* tx_buffer=nullptr;uint8_t tx_data[4];};};
int spi_device_queue_trans(spi_device_handle_t,spi_transaction_t*,TickType_t);
int spi_device_get_trans_result(spi_device_handle_t,spi_transaction_t**,TickType_t);
