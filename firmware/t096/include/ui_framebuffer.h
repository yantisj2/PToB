/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. */
#ifndef R2_UI_FRAMEBUFFER_H
#define R2_UI_FRAMEBUFFER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define R2_UI_WIDTH 80
#define R2_UI_HEIGHT 160
#define R2_UI_PIXELS (R2_UI_WIDTH*R2_UI_HEIGHT)
/* Wire bytes and stored bytes have deliberately separate names. */
#define R2_UI_BYTES (R2_UI_PIXELS*2)
#define R2_UI_FRAME_BYTES R2_UI_PIXELS
#define R2_UI_DMA_ROWS 32
#define R2_UI_DMA_PIXELS (R2_UI_WIDTH*R2_UI_DMA_ROWS)
#define R2_UI_DMA_BYTES (R2_UI_DMA_PIXELS*2)

/* Exact existing RGB565 colors, including black and the watch's bevels.
 * Fixed flash palette: no quantization, allocation or runtime registration. */
#define R2_UI_COLORS(X) \
 X(BLACK,0x0000u) X(BG,0x0820u) X(PANEL,0x1042u) X(WHITE,0xFFFFu) \
 X(DIM,0xA534u) X(CYAN,0x37FFu) X(BLUE,0x4D1Fu) X(GREEN,0x4FEAu) \
 X(AMBER,0xFD20u) X(RED,0xF9C7u) X(BEZEL,0x3987u) X(LCD,0x0821u) \
 X(BUTTON_LIGHT,0xBDF7u) X(BUTTON_EDGE,0x8C71u) X(BUTTON_SHADOW,0x2104u) \
 X(DIAL,0x3186u) X(GREY,0x6B4Du)
#define R2_UI_COLOR_ENUM(name,rgb565) R2_UI_COLOR_##name,
enum r2_ui_color { R2_UI_COLORS(R2_UI_COLOR_ENUM) R2_UI_COLOR_COUNT };
#undef R2_UI_COLOR_ENUM
#define R2_UI_RGB565_ENUM(name,rgb565) R2_UI_RGB565_##name=rgb565,
enum r2_ui_rgb565 { R2_UI_COLORS(R2_UI_RGB565_ENUM) };
#undef R2_UI_RGB565_ENUM
_Static_assert(R2_UI_COLOR_COUNT<=UINT8_MAX+1u,"UI colors must fit one byte");
_Static_assert(R2_UI_HEIGHT%R2_UI_DMA_ROWS==0,"DMA batches must cover whole rows");

/* Caller supplies bounded output storage. Invalid input never indexes outside
 * the palette; a partially expanded invalid batch must not be submitted. */
static inline bool r2_ui_expand_rgb565(uint8_t *out,size_t capacity,
                                     const uint8_t *indices,size_t pixels)
{
#define R2_UI_PALETTE_ENTRY(name,rgb565) rgb565,
 static const uint16_t palette[]={R2_UI_COLORS(R2_UI_PALETTE_ENTRY)};
#undef R2_UI_PALETTE_ENTRY
 if(!out||!indices||pixels>R2_UI_PIXELS||pixels>capacity/2)return false;
 for(size_t i=0;i<pixels;++i){
  if(indices[i]>=R2_UI_COLOR_COUNT)return false;
  uint16_t color=palette[indices[i]];
  out[2*i]=(uint8_t)(color>>8);out[2*i+1]=(uint8_t)color;
 }
 return true;
}
#endif
