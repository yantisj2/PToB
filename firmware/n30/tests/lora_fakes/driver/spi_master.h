// Synthetic host API boundary; no hardware access. MIT, Jonathan Yantis.
#pragma once
#include "gpio.h"
#include "freertos/FreeRTOS.h"
#include <cstddef>
using spi_device_handle_t=void*;
constexpr int SPI2_HOST=2,SPI_DMA_CH_AUTO=3;
struct spi_bus_config_t{int mosi_io_num=0,miso_io_num=0,sclk_io_num=0,quadwp_io_num=0,quadhd_io_num=0;size_t max_transfer_sz=0;};
struct spi_device_interface_config_t{int clock_speed_hz=0,spics_io_num=0,queue_size=0;};
struct spi_transaction_t{size_t length=0;const void* tx_buffer=nullptr;void* rx_buffer=nullptr;};
esp_err_t spi_bus_initialize(int,const spi_bus_config_t*,int);
esp_err_t spi_bus_add_device(int,const spi_device_interface_config_t*,spi_device_handle_t*);
esp_err_t spi_device_queue_trans(spi_device_handle_t,spi_transaction_t*,TickType_t);
esp_err_t spi_device_get_trans_result(spi_device_handle_t,spi_transaction_t**,TickType_t);
