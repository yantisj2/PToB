/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Existing pixel assertions/previews inspect the actual indexed renderer after
 * the same checked conversion used by the production display owner. Host only. */
#ifndef R2_UI_RGB565_FIXTURE_H
#define R2_UI_RGB565_FIXTURE_H
#include "ui.h"
#include <assert.h>
static void r2_test_render_rgb565(uint8_t *rgb565,const struct r2_ui_model *m,
                                const struct r2_ui_status *s,uint64_t now)
{
 uint8_t indices[R2_UI_FRAME_BYTES];
 r2_ui_render(indices,m,s,now);
 assert(r2_ui_expand_rgb565(rgb565,R2_UI_BYTES,indices,R2_UI_PIXELS));
}
#endif
