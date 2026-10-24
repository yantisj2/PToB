// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// UI preparation only. No BLE, synthetic timing, enrollment or radio ownership.
#include "ui.hpp"
#include <M5Unified.h>
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_clk_tree.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdlib>
#include <cstring>
namespace {
constexpr char Tag[]="CORESE_UI";
void fail(const char* reason){ESP_LOGE(Tag,"%s",reason);std::abort();}
}
extern "C" void app_main(){
 if(!esp_psram_is_initialized()||esp_psram_get_size()<8*1024*1024)fail("8MiB PSRAM required; no internal-heap framebuffer fallback");
 auto config=M5.config();
 config.internal_imu=false;config.external_imu=false;config.internal_rtc=false;
 config.internal_mic=false;config.internal_spk=false;config.external_rtc=false;
 config.output_power=false;config.clear_display=true;
 // Autodetect must positively identify SE, rather than falling back to another board.
 config.fallback_board=m5::board_t::board_unknown;
 M5.begin(config);
 if(M5.getBoard()!=m5::board_t::board_M5StackCoreS3SE)fail("Detected hardware is not CoreS3-SE");
 M5.Display.setRotation(1);
 if(M5.Display.width()!=corese::Width||M5.Display.height()!=corese::Height)M5.Display.setRotation(0);
 if(M5.Display.width()!=corese::Width||M5.Display.height()!=corese::Height)fail("Unexpected LCD geometry");
 M5.Display.setBrightness(150);M5.Display.setSwapBytes(true);
 auto* frame=static_cast<uint16_t*>(heap_caps_malloc(corese::FrameBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 auto* last=static_cast<uint16_t*>(heap_caps_malloc(corese::FrameBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!frame||!last)fail("External framebuffer allocation failed");
 std::memset(last,0xff,corese::FrameBytes);
 corese::View view;corese::Touch touch;corese::Model model;
 model.psram_total=uint32_t(esp_psram_get_size());
 // Report hardware clock-tree rates; never label the CPU/XTAL as a fine counter.
 esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_XTAL,ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,&model.xtal_hz);
 esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU,ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,&model.cpu_hz);
 ESP_LOGI(Tag,"UI_PREPARATION_ONLY psram=%lu framebuffers=%u BLE=ABSENT TIMING=UNAVAILABLE",static_cast<unsigned long>(model.psram_total),unsigned(corese::FrameBytes*2));
 for(;;){
  M5.update();const auto detail=M5.Touch.getDetail();
  const bool down=M5.Touch.getCount()>0;
  const auto action=touch.update(view,down,detail.x,detail.y);
  if(action!=corese::Action::None)corese::apply(view,action);
  const auto now=uint64_t(esp_timer_get_time());model.sampled_us=now;
  model.psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  corese::render(frame,model,view,now);
  if(std::memcmp(frame,last,corese::FrameBytes)){
   // Synchronous transfer owns the source until it returns. No transient DMA buffer.
   M5.Display.pushImage(0,0,corese::Width,corese::Height,frame);
   std::memcpy(last,frame,corese::FrameBytes);
  }
  vTaskDelay(pdMS_TO_TICKS(20));
 }
}
