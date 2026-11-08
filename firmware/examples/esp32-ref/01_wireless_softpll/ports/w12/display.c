// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t oled;

static bool command(uint8_t byte)
{
    const uint8_t packet[] = {0, byte};
    return oled && i2c_master_transmit(oled, packet, sizeof(packet), 30) == ESP_OK;
}

bool ref_w12_display_begin(void)
{
    // wake() has staged safe levels and removed sleep holds before this call.
    if (gpio_set_level(45, 0) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
    if (gpio_set_level(47, 1) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_master_bus_config_t config = {0};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = 17; config.scl_io_num = 18;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&config, &bus) != ESP_OK) return false;
    uint16_t address = 0x3c;
    if (i2c_master_probe(bus, address, 30) != ESP_OK) {
        address = 0x3d;
        if (i2c_master_probe(bus, address, 30) != ESP_OK) return false;
    }
    i2c_device_config_t device = {0};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = address; device.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &device, &oled) != ESP_OK) return false;
    // SSD1315: page addressing, 128x64, normal contrast and orientation.
    const uint8_t setup[] = {0xae,0xd5,0xf0,0xa8,0x3f,0xd3,0,0x40,0x8d,0x14,
        0x20,2,0xa1,0xc8,0xda,0x12,0x81,0x60,0xd9,0xf1,0xdb,0x40,0xa4,0xa6,0x2e,0xaf};
    for (unsigned i = 0; i < sizeof(setup); ++i) if (!command(setup[i])) return false;
    return true;
}

bool ref_w12_display_send(const uint8_t fb[1024])
{
    if (!oled) return false;
    uint8_t packet[129]; packet[0] = 0x40;
    for (unsigned page = 0; page < 8; ++page) {
        if (!command((uint8_t)(0xb0 + page)) || !command(0) || !command(0x10)) return false;
        memcpy(packet + 1, fb + page * 128, 128);
        if (i2c_master_transmit(oled, packet, sizeof(packet), 30) != ESP_OK) return false;
    }
    return true;
}

bool ref_w12_display_stop(void)
{
    bool ok = true;
    if (oled) {
        (void)command(0xae);
        if (i2c_master_bus_rm_device(oled) != ESP_OK) ok = false;
        else oled = NULL;
    }
    if (bus && !oled) {
        if (i2c_del_master_bus(bus) != ESP_OK) ok = false;
        else bus = NULL;
    }
    return ok;
}
