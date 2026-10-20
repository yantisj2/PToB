/* Shared UI command/render page count. */
#ifndef R2_UI_PAGES_H
#define R2_UI_PAGES_H
#define R2_UI_SHADOW_PAGE 6u
#define R2_UI_PRECISION_PAGE 2u
#ifdef R2_DC_PROFILE
#define R2_UI_PAGES 4
#elif defined(R2_LORA)
#define R2_UI_LORA_PAGE 7u
#define R2_UI_PAGES 8
#else
#define R2_UI_PAGES 7
#endif
#endif
