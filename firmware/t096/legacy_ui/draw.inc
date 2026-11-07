static bool watch_panel(uint8_t *,int,int);
#include "legacy.h"
#include "../include/ui_framebuffer.h"
#ifdef T096_V12_CONSOLE_PANEL
#include "console_panel.h"
#endif

#ifndef UI_COLOR
#define UI_COLOR(name) R2_UI_RGB565_##name
#endif
#define C_BG UI_COLOR(BG)
#define C_PANEL UI_COLOR(PANEL)
#define C_WHITE UI_COLOR(WHITE)
#define C_DIM UI_COLOR(DIM)
#define C_CYAN UI_COLOR(CYAN)
#define C_BLUE UI_COLOR(BLUE)
#define C_GREEN UI_COLOR(GREEN)
#define C_AMBER UI_COLOR(AMBER)
#define C_RED UI_COLOR(RED)
#define C_BEZEL UI_COLOR(BEZEL)
#define C_LCD UI_COLOR(LCD)

/* The native owner can cancel pure raster work at bounded pixel checkpoints.
 * Ordinary legacy/full callers retain their original drawing behavior. */
#ifndef R2_UI_DRAW_CALL
#define R2_UI_DRAW_CALL(call) call
#endif
#ifndef R2_UI_PIXEL_CHECKPOINT
#define R2_UI_PIXEL_CHECKPOINT() do { } while (0)
#endif
#ifndef R2_UI_PIXEL_VISIBLE
#define R2_UI_PIXEL_VISIBLE(x,y) true
#endif
#ifndef R2_UI_RECT_BOUNDS
#define R2_UI_RECT_BOUNDS(x,y,w,h) do {}while(0)
#endif
#ifndef R2_UI_OPTIONAL_CALL
#define R2_UI_OPTIONAL_CALL(call) call
#endif

#ifndef R2_UI_BOOL_ABORTED
#define R2_UI_BOOL_ABORTED() false
#endif
#define R2_UI_BOOL_CALL(call) do {if(!(call))return false;}while(0)
#define R2_UI_DRAW_BOOL(call) do {if(!(call))return;}while(0)
#define R2_UI_BOOL_VOID(call) do {call;if(R2_UI_BOOL_ABORTED())return false;}while(0)

/* A compact fixed 5x7 face keeps the renderer freestanding and deterministic. */
static const uint8_t digits[10][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e}
};
static const uint8_t letters[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};
static const uint8_t punctuation[][5] = {
    {0,0,0,0,0},           /* space */
    {0,0x36,0x36,0,0},    /* : */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0,0x60,0x60,0,0},    /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x08,0x08,0x3e,0x08,0x08}, /* + */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x14,0x14,0x7f,0x14,0x14}, /* # */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x7f,0x10,0x28,0x44,0x00}, /* k */
    {0x44,0x64,0x54,0x4c,0x44}  /* z */
};
static const uint8_t lower_a[5]={0x20,0x54,0x54,0x54,0x78};
static const uint8_t lower_d[5]={0x38,0x44,0x44,0x48,0x7f};
static const uint8_t lower_e[5]={0x38,0x54,0x54,0x54,0x18};
static const uint8_t lower_f[5]={0x08,0x7e,0x09,0x01,0x02};
static const uint8_t lower_o[5]={0x38,0x44,0x44,0x44,0x38};
static const uint8_t lower_m[5]={0x7c,0x04,0x78,0x04,0x78};
static const uint8_t lower_r[5]={0x7c,0x08,0x04,0x04,0x08};
static const uint8_t lower_t[5]={0x04,0x3f,0x44,0x40,0x20};
static const uint8_t arrow_glyph[5]={0,0x22,0x14,0x08,0};
static const uint8_t less_glyph[5]={0,0x08,0x14,0x22,0};
static const uint8_t lower_h[5] = {0x7f,0x08,0x04,0x04,0x78};
static const uint8_t lower_n[5] = {0x7c,0x08,0x04,0x04,0x78};
static const uint8_t lower_u[5]={0x3c,0x40,0x40,0x20,0x7c};
static const uint8_t lower_s[5] = {0x48,0x54,0x54,0x54,0x24};

static const uint8_t *glyph(char c) {
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    switch (c) {
        case 'f': return lower_f;case 'o': return lower_o;
        case '>': return arrow_glyph;
        case '<': return less_glyph;
        case 'a': return lower_a;case 'd': return lower_d;case 'e': return lower_e;case 'm': return lower_m;case 'r': return lower_r;case 't': return lower_t;
        case ':': return punctuation[1];
        case '-': return punctuation[2];
        case '.': return punctuation[3];
        case '/': return punctuation[4];
        case '+': return punctuation[5];
        case '?': return punctuation[6];
        case '#': return punctuation[7];
        case '%': return punctuation[8];
        case 'k': return punctuation[9];
        case 'z': return punctuation[10];
        case 'h': return lower_h;
        case 'n': return lower_n;
        case 's': return lower_s;
        case 'u': return lower_u;
        default: return punctuation[0];
    }
}

static bool pixel(uint8_t *fb, int x, int y, uint16_t c) {
    R2_UI_PIXEL_CHECKPOINT();
    if (x < 0 || y < 0 || x >= (int)UI_WIDTH || y >= (int)UI_HEIGHT) return true;
    if (!R2_UI_PIXEL_VISIBLE(x,y)) return true;
    unsigned offset = (unsigned)y * UI_WIDTH + (unsigned)x;
#ifdef UI_PIXEL_WRITE
    UI_PIXEL_WRITE(fb,offset,c);
#else
    offset *= 2u;
    fb[offset] = (uint8_t)(c >> 8);
    fb[offset + 1u] = (uint8_t)c;
#endif

 return true;
}

static bool fill(uint8_t *fb, int x, int y, int w, int h, uint16_t c) {
    R2_UI_RECT_BOUNDS(x,y,w,h);
    for (int row = y; row < y + h; ++row)
#ifdef R2_UI_FILL_ROW
        R2_UI_BOOL_CALL(R2_UI_FILL_ROW(fb,x,row,w,c));
#else
        for (int col = x; col < x + w; ++col) R2_UI_BOOL_CALL(pixel(fb, col, row, c));
#endif

 return true;
}

static bool round_panel(uint8_t *fb, int x, int y, int w, int h,
                        uint16_t inside, uint16_t edge) {
    int left=x,top=y,cw=w,ch=h;R2_UI_RECT_BOUNDS(left,top,cw,ch);
    for (int row = top-y; row < top+ch-y; ++row) {
        int inset = row == 0 || row == h - 1 ? 3 :
                    row == 1 || row == h - 2 ? 1 : 0;
        int first=left-x>inset?left-x:inset,last=left+cw-x<w-inset?left+cw-x:w-inset;
#ifdef R2_UI_FILL_ROW
        if(row==0||row==h-1){
            R2_UI_BOOL_CALL(R2_UI_FILL_ROW(fb,x+first,y+row,last-first,edge));
        }else{
            R2_UI_BOOL_CALL(R2_UI_FILL_ROW(fb,x+first,y+row,last-first,inside));
            if(first<last&&first==inset)R2_UI_BOOL_CALL(pixel(fb,x+first,y+row,edge));
            if(first<last&&last==w-inset)R2_UI_BOOL_CALL(pixel(fb,x+last-1,y+row,edge));
        }
#else
        for (int col = first; col < last; ++col) {
            bool border = row == 0 || row == h - 1 ||
                          col == inset || col == w - inset - 1;
            R2_UI_BOOL_CALL(pixel(fb, x + col, y + row, border ? edge : inside));
        }
#endif
    }

 return true;
}

static bool label(uint8_t *fb, int x, int y, const char *s, unsigned scale,
                  uint16_t color) {
    for (; *s && x < (int)UI_WIDTH; ++s, x += (int)(6u * scale)) {
        const uint8_t *g = glyph(*s);
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (g[col] & (1u << row))
                    R2_UI_BOOL_CALL(fill(fb, x + col * (int)scale, y + row * (int)scale,
                         (int)scale, (int)scale, color));
    }

 return true;
}
static bool __attribute__((noinline)) row_label3(uint8_t *fb,int y,const char *text,uint16_t c){return label(fb,3,y,text,1,c);}
static bool __attribute__((noinline)) row_label4(uint8_t *fb,int y,const char *text,uint16_t c){return label(fb,4,y,text,1,c);}
static bool __attribute__((noinline)) row_label5(uint8_t *fb,int y,const char *text,uint16_t c){return label(fb,5,y,text,1,c);}
static bool __attribute__((noinline)) row_label7(uint8_t *fb,int y,const char *text,uint16_t c){return label(fb,7,y,text,1,c);}
static bool __attribute__((noinline)) row_label8(uint8_t *fb,int y,const char *text,uint16_t c){return label(fb,8,y,text,1,c);}


static bool tight_italic_label(uint8_t *fb, int x, int y, const char *s,
                               uint16_t color) {
    for (; *s; ++s, x += 5) {
        const uint8_t *g = glyph(*s);
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (g[col] & (1u << row))
                    R2_UI_BOOL_CALL(pixel(fb, x + col + (6 - row) / 3, y + row, color));
    }

 return true;
}

static void number(char *out, uint32_t value, unsigned width) {
    out[width] = 0;
    do { out[--width] = (char)('0' + value % 10u); value /= 10u; }
    while (width);
}

static uint64_t divide_count(uint64_t value, uint32_t *remainder_out) {
    if (value <= UINT32_MAX) {
        *remainder_out = (uint32_t)value % 1000u;
        return (uint32_t)value / 1000u;
    }
    uint64_t quotient = 0;
    uint32_t remainder = 0;
    for (int bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1) | (uint32_t)((value >> bit) & 1u);
        if (remainder >= 1000u) {
            remainder -= 1000u;
            quotient |= UINT64_C(1) << bit;
        }
    }
    *remainder_out = remainder;
    return quotient;
}

void ui_format_count(char out[8], uint64_t value) {
    static const char suffix[] = "\0kMGTPE";
    if (value < 1000u) {
        uint32_t small = (uint32_t)value;
        char reverse[3];
        unsigned length = 0;
        do {
            reverse[length++] = (char)('0' + small % 10u);
            small /= 10u;
        } while (small);
        unsigned i = 0;
        while (length) out[i++] = reverse[--length];
        out[i] = 0;
        return;
    }
    uint8_t unit = 0;
    uint32_t remainder = 0;
    while (value >= 1000u && unit < 6u) {
        value = divide_count(value, &remainder);
        ++unit;
    }
    uint32_t whole = (uint32_t)value;
    uint32_t tenth = (remainder + 50u) / 100u;
    if (whole < 10u && tenth == 10u) { ++whole; tenth = 0; }
    if (whole >= 10u) {
        if (remainder >= 500u && tenth != 0u) ++whole;
        if (whole >= 1000u && unit < 6u) {
            whole = 1u;
            ++unit;
            tenth = 0u;
        }
    }
    unsigned i = 0;
    if (whole >= 100u) out[i++] = (char)('0' + whole / 100u);
    if (whole >= 10u) out[i++] = (char)('0' + whole / 10u % 10u);
    out[i++] = (char)('0' + whole % 10u);
    if (whole < 10u) {
        out[i++] = '.';
        out[i++] = (char)('0' + tenth);
    }
    out[i++] = suffix[unit];
    out[i] = 0;
}

static bool metric(uint8_t *fb, int y, const char *name, const char *value,
                   uint16_t color) {
    R2_UI_BOOL_CALL(row_label5(fb,y,name,C_DIM));
    int len = 0;
    while (value[len]) ++len;
    R2_UI_BOOL_CALL(label(fb, 75 - len * 6, y, value, 1, color));

 return true;
}

static bool issue_metric(uint8_t *fb, int y, const char *name,
                         const char *value, uint16_t color) {
    R2_UI_BOOL_CALL(label(fb, 11, y, name, 1, C_DIM));
    int len = 0;
    while (value[len]) ++len;
    R2_UI_BOOL_CALL(label(fb, 79 - len * 6, y, value, 1, color));

 return true;
}

static void signed_ticks(char *out, int32_t value) {
    out[0] = value < 0 ? '-' : '+';
    uint32_t magnitude = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    if (magnitude > 9999u) magnitude = 9999u;
    number(out + 1, magnitude, 4);
}

static void signed_rate(char out[16], int32_t ppb) {
    char magnitude[8];
    ui_format_count(magnitude, ppb < 0 ? (uint32_t)(-(int64_t)ppb) :
                                           (uint32_t)ppb);
    out[0] = ppb < 0 ? '-' : '+';
    unsigned i = 0;
    do { out[i + 1u] = magnitude[i]; } while (magnitude[i++]);
}

static void temperature(char out[16], int16_t whole) {
    uint32_t magnitude = whole < 0 ? (uint32_t)(-(int32_t)whole) :
                                     (uint32_t)whole;
    unsigned i = 0;
    if (whole < 0) out[i++] = '-';
    if (magnitude >= 100u) out[i++] = (char)('0' + magnitude / 100u % 10u);
    if (magnitude >= 10u) out[i++] = (char)('0' + magnitude / 10u % 10u);
    out[i++] = (char)('0' + magnitude % 10u);
    out[i++] = 'C'; out[i] = 0;
}

static void tenths(char out[16], uint16_t value) {
    if (value > 999u) value = 999u;
    unsigned i = 0;
    if (value >= 100u) out[i++] = (char)('0' + value / 100u);
    out[i++] = (char)('0' + value / 10u % 10u);
    out[i++] = '.';
    out[i++] = (char)('0' + value % 10u);
    out[i] = 0;
}

static bool draw_page_dots(uint8_t *fb, unsigned page) {
    for (unsigned i = 0; i < UI_PAGE_COUNT; ++i)
        R2_UI_BOOL_CALL(fill(fb, 6 + (int)i * 8, 153, 4, 4, i == page ? C_CYAN : C_BEZEL));

 return true;
}

/* Seven-segment digits make HH:MM:SS readable on one 80-pixel line. */
static bool digital_digit(uint8_t *fb, int x, int y, char c, uint16_t color) {
    static const uint8_t segments[10] =
        {0x3fu,0x06u,0x5bu,0x4fu,0x66u,0x6du,0x7du,0x07u,0x7fu,0x6fu};
    uint8_t mask = c >= '0' && c <= '9' ? segments[c - '0'] :
                   c == '-' ? 0x40u : 0;
    if (mask & 0x01u) R2_UI_BOOL_CALL(fill(fb, x + 1, y, 6, 2, color));
    if (mask & 0x02u) R2_UI_BOOL_CALL(fill(fb, x + 6, y + 2, 2, 5, color));
    if (mask & 0x04u) R2_UI_BOOL_CALL(fill(fb, x + 6, y + 9, 2, 5, color));
    if (mask & 0x08u) R2_UI_BOOL_CALL(fill(fb, x + 1, y + 14, 6, 2, color));
    if (mask & 0x10u) R2_UI_BOOL_CALL(fill(fb, x, y + 9, 2, 5, color));
    if (mask & 0x20u) R2_UI_BOOL_CALL(fill(fb, x, y + 2, 2, 5, color));
    if (mask & 0x40u) R2_UI_BOOL_CALL(fill(fb, x + 1, y + 7, 6, 2, color));

 return true;
}

static bool digital_time(uint8_t *fb, const ui_snapshot_t *s) {
    uint32_t sod = s->acquiring_first_lock ? s->boot_elapsed_seconds :
                                              s->utc_second % 86400u;
    if (s->acquiring_first_lock && sod >= 360000u) sod = 359999u;
    uint32_t h = sod / 3600u, m = sod / 60u % 60u, sec = sod % 60u;
    char time[9];
    number(time, h, 2); time[2] = ':';
    number(time + 3, m, 2); time[5] = ':';
    number(time + 6, sec, 2);
    if (!s->acquiring_first_lock && !s->utc_valid && !s->cached_time_valid) {
        for (unsigned i = 0; i < 8u; ++i)
            if (time[i] != ':') time[i] = '-';
    }
    int x = 6, y = 30;
    uint16_t clock_color = s->acquiring_first_lock ? C_AMBER :
                           s->utc_valid ? C_WHITE :
                           s->cached_time_valid ? C_AMBER : C_DIM;
    uint16_t colon_color = s->acquiring_first_lock ? C_CYAN :
                           s->utc_valid ? C_GREEN :
                           s->cached_time_valid ? C_AMBER : C_DIM;
    for (unsigned i = 0; i < 8u; ++i) {
        if (time[i] == ':') {
            R2_UI_BOOL_CALL(fill(fb, x + 1, y + 5, 2, 2, colon_color));
            R2_UI_BOOL_CALL(fill(fb, x + 1, y + 13, 2, 2, colon_color));
            x += 4;
        } else {
            R2_UI_BOOL_CALL(digital_digit(fb, x, y, time[i], clock_color));
            x += 10;
        }
    }

 return true;
}

static const int8_t sine60[60] = {
      0, 13, 26, 39, 52, 63, 75, 85, 94,103,110,116,121,124,126,
    127,126,124,121,116,110,103, 94, 85, 75, 63, 52, 39, 26, 13,
      0,-13,-26,-39,-52,-63,-75,-85,-94,-103,-110,-116,-121,-124,-126,
   -127,-126,-124,-121,-116,-110,-103,-94,-85,-75,-63,-52,-39,-26,-13
};

static int radial_x(unsigned index, unsigned radius) {
    return (int)sine60[index % 60u] * (int)radius / 127;
}

static int radial_y(unsigned index, unsigned radius) {
    return -(int)sine60[(index + 15u) % 60u] * (int)radius / 127;
}

static bool line(uint8_t *fb, int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        R2_UI_BOOL_CALL(pixel(fb, x0, y0, color));
        if (x0 == x1 && y0 == y1) break;
        int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }

 return true;
}

static bool circle(uint8_t *fb, int cx, int cy, int radius, uint16_t color) {
    int x = radius, y = 0, error = 1 - radius;
    while (x >= y) {
        R2_UI_BOOL_CALL(pixel(fb,cx+x,cy+y,color)); R2_UI_BOOL_CALL(pixel(fb,cx+y,cy+x,color));
        R2_UI_BOOL_CALL(pixel(fb,cx-x,cy+y,color)); R2_UI_BOOL_CALL(pixel(fb,cx-y,cy+x,color));
        R2_UI_BOOL_CALL(pixel(fb,cx+x,cy-y,color)); R2_UI_BOOL_CALL(pixel(fb,cx+y,cy-x,color));
        R2_UI_BOOL_CALL(pixel(fb,cx-x,cy-y,color)); R2_UI_BOOL_CALL(pixel(fb,cx-y,cy-x,color));
        ++y;
        if (error < 0) error += 2 * y + 1;
        else { --x; error += 2 * (y - x) + 1; }
    }

 return true;
}

static bool dial_fill(uint8_t *fb, int cx, int cy, int radius,
                      uint16_t color) {
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius)
                R2_UI_BOOL_CALL(pixel(fb, cx + x, cy + y, color));

 return true;
}

/* A polished upper rim and dark lower rim frame a recessed face. */
static bool inset_badge(uint8_t *fb, int x, int y, int w, int h,
                         uint16_t face) {
    R2_UI_BOOL_CALL(fill(fb, x + 1, y + 1, w, h, UI_COLOR(BLACK)));
    R2_UI_BOOL_CALL(fill(fb, x, y, w, h, C_BEZEL));
    R2_UI_BOOL_CALL(fill(fb, x, y, w, 1, UI_COLOR(BUTTON_LIGHT)));
    R2_UI_BOOL_CALL(fill(fb, x, y, 1, h, UI_COLOR(BUTTON_EDGE)));
    R2_UI_BOOL_CALL(fill(fb, x + 1, y + h - 1, w - 1, 1, UI_COLOR(BUTTON_SHADOW)));
    R2_UI_BOOL_CALL(fill(fb, x + w - 1, y + 1, 1, h - 1, UI_COLOR(BUTTON_SHADOW)));
    R2_UI_BOOL_CALL(fill(fb, x + 1, y + 1, w - 2, h - 2, UI_COLOR(BLACK)));
    R2_UI_BOOL_CALL(fill(fb, x + 2, y + 2, w - 4, h - 4, face));

 return true;
}

bool ui_full_frame_phase_safe(bool seen, uint64_t age_ticks, bool diagnostic_second) {
    if (!seen) return true;
    uint32_t end = (diagnostic_second ? UI_DIAGNOSTIC_PULSE_MS : 200u) * 16000u;
    /* PPS can arrive after the main loop's capture check. Keep the old-age
       guard closed across that edge until the next loop consumes the capture.
       A missing PPS can still allow display work after this bounded window. */
    return (age_ticks < end - 800000u || age_ticks >= end) &&
           (age_ticks < 15200000u || age_ticks >= 16800000u);
}

ui_pulse_t ui_pps_pulse(bool seen, uint32_t age_ms, bool diagnostic_second) {
    if (!seen || age_ms >= (diagnostic_second ? UI_DIAGNOSTIC_PULSE_MS : 200u))
        return UI_PULSE_OFF;
    return diagnostic_second ? UI_PULSE_DIAGNOSTIC : UI_PULSE_PPS;
}

static bool draw_pulse_at(uint8_t fb[UI_FRAME_BYTES], ui_pulse_t pulse,
                        unsigned x, unsigned y) {
    R2_UI_BOOL_CALL(inset_badge(fb, (int)x, (int)y, 9, 9,
                pulse == UI_PULSE_DIAGNOSTIC ? C_GREEN :
                pulse == UI_PULSE_PPS ? C_BLUE : C_LCD));

 return true;
}

static bool draw_pulse(uint8_t fb[UI_FRAME_BYTES], ui_pulse_t pulse) {
    R2_UI_BOOL_CALL(draw_pulse_at(fb, pulse, 66, 19));

 return true;
}

static bool draw_rf_lamp(uint8_t fb[UI_FRAME_BYTES], unsigned radio,
                       unsigned kind, uint8_t activity, bool help) {
    if (radio >= UI_RF_RADIOS || kind >= UI_RF_KINDS) return true;
    R2_UI_BOOL_CALL(inset_badge(fb, (int)ui_rf_lamp_x(kind), (int)ui_rf_lamp_y(radio, help),
                9, 9, activity == UI_RF_RX ? C_BLUE :
                      activity == UI_RF_TX ? C_GREEN : C_LCD));

 return true;
}

static bool vertical_label(uint8_t *fb, int x, int y, const char *text) {
    for (; *text; ++text, y += 6) {
        const uint8_t *g = glyph(*text);
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (g[col] & (1u << row)) R2_UI_BOOL_CALL(pixel(fb, x + 6 - row, y + col, C_DIM));
    }

 return true;
}

static bool rf_time(uint8_t *fb, int y, const ui_rf_source_t *source) {
    char time[] = "--:--:--";
    if (source->available) {
        uint32_t sod = source->second % 86400u;
        number(time, sod / 3600u, 2); time[2] = ':';
        number(time + 3, sod / 60u % 60u, 2); time[5] = ':';
        number(time + 6, sod % 60u, 2);
    }
    R2_UI_BOOL_CALL(label(fb, 16, y, time, 1, source->available ? C_WHITE : C_DIM));

 return true;
}

static void rf_signed(char out[16], int64_t value) {
    uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
    out[0] = value < 0 ? '-' : '+';
    if (magnitude < 1000000u) {
        unsigned width = 1; uint32_t n = (uint32_t)magnitude;
        while (n >= 10) { n /= 10; ++width; }
        number(out + 1, (uint32_t)magnitude, width);
    } else ui_format_count(out + 1, magnitude);
}

static const char *rf_source_state(const ui_rf_source_t *source) {
    return !source->available ? "WAIT" : !source->fresh ? "HOLD" :
           source->utc ? "TRACK" : "REL";
}

static bool role_header(uint8_t *fb,const ui_snapshot_t *s);
static bool rf_live(uint8_t *fb, const ui_snapshot_t *s) {
    if(s->rf_help){R2_UI_BOOL_CALL(watch_panel(fb,1,13));R2_UI_BOOL_CALL(row_label7(fb,4,"RF KEY",C_WHITE));}
    else R2_UI_BOOL_CALL(role_header(fb,s));
    R2_UI_BOOL_CALL(inset_badge(fb, 3, 17, 74, s->rf_help ? 99 : 50, C_LCD));
    for (unsigned radio = 0; radio < UI_RF_RADIOS; ++radio) {
        unsigned y = ui_rf_lamp_y(radio, s->rf_help);
        R2_UI_BOOL_CALL(row_label7(fb,(int)y - 10,radio == UI_RF_BLE ?
              (s->rf_ble_tx_mode ? "BLE RX+TX" : s->ble_mode_known && !s->ble_mode_enabled ? "BLE OFF" : "BLE RX") :
              (s->lora_fault ? "LORA FAULT" : !s->lora_enabled ? "LORA OFF" :
#ifdef PTOB_LORA_LS4
               s->lora_tx_capable && s->lora_tx_enabled ? "LORA TX" : s->lora_active ? "LORA TWO-WAY" : "LORA RX"
#else
               s->lora_tx_mode ? "LORA RX+TX" : "LORA RX"
#endif
              ),C_DIM));
        static const char *const key[UI_RF_KINDS] =
            {"HEL", "SRC", "PRE", "ACK", "CMT", "SRV", "RDY", "ERR"};
        for (unsigned kind = 0; kind < UI_RF_KINDS; ++kind) {
            R2_UI_BOOL_CALL(draw_rf_lamp(fb, radio, kind, s->rf_lamps[radio][kind], s->rf_help));
            if (s->rf_help) {
                const char *legend=key[kind];
#if defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST)
                if(radio==UI_RF_LORA)legend=kind==UI_RF_TIME?"TIM":kind==UI_RF_OTHER?"UNK":"---";
#endif
                R2_UI_BOOL_CALL(vertical_label(fb, (int)ui_rf_lamp_x(kind) + 1,(int)y + 13,legend));
            }
        }
    }
    if (s->rf_help) {
        R2_UI_BOOL_CALL(row_label7(fb,122,"BLUE  RX",C_BLUE));
        R2_UI_BOOL_CALL(row_label7(fb,134,"GREEN TX",C_GREEN));
        R2_UI_BOOL_CALL(row_label7(fb,148,"PRESS:BACK",C_DIM));
        return true;
    }
    const char *ble_state=s->native_hot_standby?"HA-READY":rf_source_state(&s->rf_sources.ble);
    if(s->native_hot_standby){
        R2_UI_BOOL_CALL(row_label5(fb,74,"BLE",C_DIM));R2_UI_BOOL_CALL(label(fb,26,74,ble_state,1,C_GREEN));
    }else R2_UI_BOOL_CALL(metric(fb,74,"BLE",ble_state,C_BLUE));
    R2_UI_BOOL_CALL(rf_time(fb, 85, &s->rf_sources.ble));
    R2_UI_BOOL_CALL(metric(fb, 99, "LORA", s->lora_fault ? "FAULT" : s->lora_tx_mode ? s->lora_locked?"TX ON":"TX WAIT" : s->native_rf_lora_estimated?"EST":rf_source_state(&s->rf_sources.lora), s->lora_fault ? C_RED : (s->lora_tx_mode&&!s->lora_locked)||s->native_rf_lora_estimated?C_AMBER:C_GREEN));
    if(s->lora_tx_mode)R2_UI_BOOL_CALL(label(fb,11,110,"TX NO CLOCK",1,C_DIM));
    else R2_UI_BOOL_CALL(rf_time(fb, 110, &s->rf_sources.lora));
    R2_UI_BOOL_CALL(fill(fb, 5, 122, 70, 1, C_BEZEL));
    R2_UI_BOOL_CALL(row_label5(fb,126,"DELTA ns",C_DIM));
    char value[16];
    R2_UI_OPTIONAL_CALL(rf_signed(value, s->rf_sources.delta_ns));
    unsigned n = 0; while (value[n]) ++n;
    if (s->rf_sources.delta_valid) R2_UI_BOOL_CALL(label(fb, 75 - (int)n * 6, 136, value, 1, s->native_rf_comparison_estimated?C_AMBER:C_CYAN));
    else R2_UI_BOOL_CALL(label(fb, 63, 136, "--", 1, C_DIM));
    R2_UI_OPTIONAL_CALL(rf_signed(value, s->rf_sources.drift_ppb));
    R2_UI_BOOL_CALL(metric(fb, 145, "PPB", s->rf_sources.drift_valid ? value : "--", s->native_rf_comparison_estimated?C_AMBER:C_DIM));
    /* Page number in the header leaves the comparison enough vertical room. */

 return true;
}

static bool analog_clock(uint8_t *fb, const ui_snapshot_t *s, int cy) {
    const int cx = 40;
    uint32_t sod = s->utc_second % 86400u;
    unsigned hour = (sod / 3600u) % 12u;
    unsigned minute = sod / 60u % 60u;
    unsigned second = sod % 60u;
    R2_UI_BOOL_CALL(dial_fill(fb, cx, cy, 27, C_LCD));
    R2_UI_BOOL_CALL(circle(fb, cx, cy, 27, UI_COLOR(DIAL)));
    R2_UI_BOOL_CALL(circle(fb, cx, cy, 28, C_BEZEL));
    for (unsigned i = 0; i < 60u; ++i) {
        int outer = i % 5u == 0u ? 25 : 26;
        int inner = i % 5u == 0u ? 21 : 25;
        R2_UI_BOOL_CALL(line(fb, cx + radial_x(i, inner), cy + radial_y(i, inner),
             cx + radial_x(i, outer), cy + radial_y(i, outer),
             i % 5u == 0u ? C_WHITE : C_DIM));
    }
    /* Two-digit beat frequency, independent of screen/LED refresh cadence.
     * Center the complete marking using actual 5x7 and italic glyph bounds. */
    char rate[4];
    if(s->active_ble_slots&&s->active_ble_slots<=99)R2_UI_BOOL_CALL(snprintf(rate,sizeof rate,"%u",s->active_ble_slots));
    else R2_UI_BOOL_CALL(snprintf(rate,sizeof rate,"--"));
    uint16_t rate_color = !s->die_temperature_valid ? C_DIM :
        s->die_temperature_c < 32 ? C_BLUE :
        s->die_temperature_c <= 39 ? C_GREEN : C_AMBER;
    int rate_width=s->native_lora_source?14:(int)strlen(rate)*6-1;
    int rate_x=cx-(rate_width+1+12)/2;
    R2_UI_BOOL_CALL(inset_badge(fb,cx-14,cy+7,29,13,C_LCD));
    if(s->native_lora_source){
#ifdef R2_LORA_PASSIVE_SLOW
        R2_UI_BOOL_CALL(label(fb,cx-8,cy+10,"30s",1,rate_color));
#else
        /* .25 Hz is the four-second physical timing cadence. Compact only
         * the dot's empty columns to fit the original 29px inset. */
        R2_UI_BOOL_CALL(label(fb,rate_x-1,cy+10,".",1,rate_color));
        R2_UI_BOOL_CALL(label(fb,rate_x+3,cy+10,"25",1,rate_color));
#endif
    }else R2_UI_BOOL_CALL(label(fb,rate_x,cy+10,rate,1,rate_color));
#ifdef R2_LORA_PASSIVE_SLOW
    if(!s->native_lora_source)
#endif
    R2_UI_BOOL_CALL(tight_italic_label(fb,rate_x+rate_width+1,cy+10,"Hz",rate_color));
    unsigned hour_index = hour * 5u + minute / 12u;
    R2_UI_BOOL_CALL(line(fb, cx, cy, cx + radial_x(hour_index, 12),
         cy + radial_y(hour_index, 12), C_WHITE));
    R2_UI_BOOL_CALL(line(fb, cx + 1, cy, cx + 1 + radial_x(hour_index, 12),
         cy + radial_y(hour_index, 12), C_WHITE));
    R2_UI_BOOL_CALL(line(fb, cx, cy, cx + radial_x(minute, 19),
         cy + radial_y(minute, 19), C_CYAN));
    unsigned next = (second + 1u) % 60u;
    unsigned q = s->subsecond_quarter > 3u ? 3u : s->subsecond_quarter;
    int dx = ((int)sine60[second] * (int)(4u - q) +
              (int)sine60[next] * (int)q) / 4;
    int dy = -((int)sine60[(second + 15u) % 60u] * (int)(4u - q) +
               (int)sine60[(next + 15u) % 60u] * (int)q) / 4;
    R2_UI_BOOL_CALL(line(fb, cx - dx * 5 / 127, cy - dy * 5 / 127,
         cx + dx * 23 / 127, cy + dy * 23 / 127, C_GREEN));
    R2_UI_BOOL_CALL(fill(fb, cx - 1, cy - 1, 3, 3, C_GREEN));
    R2_UI_BOOL_CALL(pixel(fb, cx, cy, C_BG));
    if(s->lora_tx_mode
#ifdef PTOB_LORA_LS4
       && s->lora_tx_capable
#endif
      ){
        /* Seven-pixel radio mast, inside the one/two-o'clock gap. Match the
         * watch's one-pixel marks and cyan accents; retain dial/rim/Hz badge. */
        const int x=cx+9,y=cy-14;
        const uint16_t color=s->lora_locked?C_BLUE:C_AMBER;
        R2_UI_BOOL_CALL(fill(fb,x-1,y-1,9,11,C_LCD));
        R2_UI_BOOL_CALL(pixel(fb,x,y+1,color));R2_UI_BOOL_CALL(pixel(fb,x+1,y,color));
        R2_UI_BOOL_CALL(pixel(fb,x+5,y,color));R2_UI_BOOL_CALL(pixel(fb,x+6,y+1,color));
        R2_UI_BOOL_CALL(pixel(fb,x+1,y+3,color));R2_UI_BOOL_CALL(pixel(fb,x+2,y+2,color));
        R2_UI_BOOL_CALL(pixel(fb,x+4,y+2,color));R2_UI_BOOL_CALL(pixel(fb,x+5,y+3,color));
        R2_UI_BOOL_CALL(pixel(fb,x+3,y+4,color));R2_UI_BOOL_CALL(fill(fb,x+3,y+5,1,4,color));
        R2_UI_BOOL_CALL(fill(fb,x+1,y+8,5,1,color));
    }

 return true;
}

static const char *native_clock_reason(const ui_snapshot_t *s) {
    switch (s->native_reason) {
    case UI_NATIVE_LOCAL_COARSE: return "LOCAL COARSE";
    case UI_NATIVE_AUTH_WAIT: return "AUTH WAIT";
    case UI_NATIVE_NO_GM: return "NO GM";
    case UI_NATIVE_SOURCE_STALE: return "SOURCE STALE";
    case UI_NATIVE_PPS_UNASSOCIATED: return "PPS NOT SYNC";
    case UI_NATIVE_CONTROL_ONLY: return "CONTROL ONLY";
    case UI_NATIVE_STORAGE_PAUSE: return "SOURCE PAUSE";
    case UI_NATIVE_NONE: return "SYNCING";
    default: return "NO TIME";
    }
}
static bool lora_sync_panel(uint8_t *fb,const ui_snapshot_t *s,int top){
    if(s->native_lora_observed){
        const char *heading=s->native_lora_phase==UI_LORA_SYNC_GAP?"HOLD / EST":s->native_lora_phase==UI_LORA_SYNC_STALE?"META STALE":"ACQ / EST";
        char line[16];R2_UI_BOOL_CALL(fill(fb,5,top+1,70,57,C_PANEL));R2_UI_BOOL_CALL(fill(fb,5,top+1,1,57,C_AMBER));
        R2_UI_BOOL_CALL(label(fb,(80-((int)strlen(heading)*6-1))/2,top+7,heading,1,C_AMBER));
        R2_UI_BOOL_CALL(snprintf(line,sizeof line,"N%u SP%us",s->native_lora_samples>99?99:s->native_lora_samples,s->native_lora_fit_s>9999?9999:s->native_lora_fit_s));
        R2_UI_BOOL_CALL(label(fb,(80-((int)strlen(line)*6-1))/2,top+21,line,1,C_DIM));
        R2_UI_BOOL_CALL(snprintf(line,sizeof line,"PAIR %us",s->native_lora_pair_s>9999?9999:s->native_lora_pair_s));
        R2_UI_BOOL_CALL(label(fb,(80-((int)strlen(line)*6-1))/2,top+35,line,1,C_DIM));
        if(s->native_lora_error_s==UINT32_MAX)R2_UI_BOOL_CALL(snprintf(line,sizeof line,"ERR UNKNOWN"));
        else R2_UI_BOOL_CALL(snprintf(line,sizeof line,"ERR %us",s->native_lora_error_s>9999?9999:s->native_lora_error_s));
        R2_UI_BOOL_CALL(label(fb,(80-((int)strlen(line)*6-1))/2,top+49,line,1,C_DIM));return true;
    }
    const char *first="LORA SYNC",*second="LISTENING",*third="LORA RX";
    uint16_t color=C_AMBER;char samples[16];
    switch(s->native_lora_phase){
    case UI_LORA_SYNC_META:second="WAIT META";third="LISTENING";break;
    case UI_LORA_SYNC_PAIR:second="WAIT PAIR";third="30s PAIRS";break;
    case UI_LORA_SYNC_TRAIN:first="LORA RX";second="TRAINING";R2_UI_BOOL_CALL(snprintf(samples,sizeof samples,"%u PAIRS",s->native_lora_samples));third=samples;break;
    case UI_LORA_SYNC_GAP:second="PAIR GAP";R2_UI_BOOL_CALL(snprintf(samples,sizeof samples,"%u PAIRS",s->native_lora_samples));third=samples;break;
    case UI_LORA_SYNC_OFF:first="LORA OFF";second="RX DISABLED";break;
    case UI_LORA_SYNC_FAULT:first="RADIO FAULT";second="CHECK RADIO";color=C_RED;break;
    default:break;
    }
    R2_UI_BOOL_CALL(fill(fb,5,top+1,70,57,C_PANEL));R2_UI_BOOL_CALL(fill(fb,5,top+1,1,57,color));
    R2_UI_BOOL_CALL(label(fb,(80-((int)strlen(first)*6-1))/2,top+8,first,1,color));
    R2_UI_BOOL_CALL(label(fb,(80-((int)strlen(second)*6-1))/2,top+28,second,1,color));
    R2_UI_BOOL_CALL(label(fb,(80-((int)strlen(third)*6-1))/2,top+47,third,1,C_DIM));

 return true;
}
static bool sync_issue(uint8_t *fb, const ui_snapshot_t *s) {
    uint16_t color = s->state == CLOCK_NO_TIME ? C_RED : C_AMBER;
    R2_UI_BOOL_CALL(fill(fb, 5, 55, 70, 57, C_PANEL));
    R2_UI_BOOL_CALL(fill(fb, 5, 55, 2, 57, color));
    if (s->native_lora_source) {
        R2_UI_BOOL_CALL(lora_sync_panel(fb,s,54));
        return true;
    }
    const char *heading = s->gnss_native_health ? native_clock_reason(s) :
                          s->display_aligning && s->state!=CLOCK_HOLDOVER ? "TIME ALIGN" :
                          s->state == CLOCK_HOLDOVER ? "HOLDOVER" :
                          !s->utc_valid && s->cached_time_valid ? "LAST GPS" :
                          !s->utc_valid ? "NO GPS" :
                          !s->pps_seen || s->pps_age_ms >= 1500u ? "PPS STALE" :
                          s->rmc_age_ms >= 2200u ? "GPS STALE" : "SYNCING";
    int heading_length = 0;
    while (heading[heading_length]) ++heading_length;
    R2_UI_BOOL_CALL(label(fb, (80 - heading_length * 6) / 2, 62, heading, 1, color));
    char gps_with_sv[12];
    const char *gps = !s->gnss_bytes_seen ? "NO RX" :
                      s->gnss_rx_age_ms < 2000u ? "RX LIVE" : "RX LOST";
    if (s->gnss_bytes_seen && s->gnss_rx_age_ms < 2000u && s->gnss_signal_age_s < 5u) {
        gps_with_sv[0] = 'R'; gps_with_sv[1] = 'X'; gps_with_sv[2] = ' ';
        uint8_t seen = s->gnss_sv_seen > 99u ? 99u : s->gnss_sv_seen;
        gps_with_sv[3] = (char)('0' + seen / 10u);
        gps_with_sv[4] = (char)('0' + seen % 10u);
        gps_with_sv[5] = 'S'; gps_with_sv[6] = 'V'; gps_with_sv[7] = 0;
        gps = gps_with_sv;
    }
    R2_UI_BOOL_CALL(issue_metric(fb, 77, "GPS", gps,
                 s->gnss_rx_age_ms < 2000u ? C_GREEN : C_WHITE));
    R2_UI_BOOL_CALL(issue_metric(fb, 89, "PPS", !s->pps_seen ? "WAIT" :
           s->pps_age_ms >= 1500u ? "STALE" : "ACTIVE", C_WHITE));
    if (s->gnss_native_health) {
        /* GPIO presence and functional UTC do not provide measured phase. */
        R2_UI_BOOL_CALL(issue_metric(fb, 101, "RMS", s->native_rms, s->native_rms_valid?C_CYAN:C_WHITE));
    } else if (s->display_aligning && s->state!=CLOCK_HOLDOVER) {
        char error[8];R2_UI_OPTIONAL_CALL(signed_ticks(error,s->display_error_ms));
        R2_UI_BOOL_CALL(issue_metric(fb,101,"MS",error,color));
    }
    else if (!s->utc_valid) R2_UI_BOOL_CALL(issue_metric(fb, 101, "ERR", "NO GPS", color));
    else if (!s->pps_seen || s->pps_age_ms >= 1500u)
        R2_UI_BOOL_CALL(issue_metric(fb, 101, "ERR", "PPS LOST", color));
    else if (s->rmc_age_ms >= 2200u)
        R2_UI_BOOL_CALL(issue_metric(fb, 101, "ERR", "GPS LOST", color));
    else {
        char error[8];
        R2_UI_OPTIONAL_CALL(signed_ticks(error, s->last_pps_error_ticks));
        error[5] = 'T'; error[6] = 0;
        R2_UI_BOOL_CALL(issue_metric(fb, 101, "ERR", s->pps_count > 1u ? error : "WAIT", color));
    }

 return true;
}

void ui_format_ppb(char out[16], uint32_t hundredths) {
    if (hundredths < 100u) {
        out[0] = '0'; out[1] = '.';
        out[2] = (char)('0' + hundredths / 10u);
        out[3] = (char)('0' + hundredths % 10u);
        out[4] = 0;
        return;
    }
    uint32_t tenths = hundredths / 10u + (hundredths % 10u >= 5u);
    if (tenths >= 10000u) {
        uint32_t rounded = tenths / 10u + (tenths % 10u >= 5u);
        ui_format_count(out, rounded);
        return;
    }
    char whole[8];
    ui_format_count(whole, tenths / 10u);
    unsigned length = 0;
    for (unsigned i = 0; whole[i]; ++i) out[length++] = whole[i];
    out[length++] = '.';
    out[length++] = (char)('0' + tenths % 10u);
    out[length] = 0;
}

static void format_beat_ppb(char out[16], uint32_t hundredths) {
    char value[16];
    ui_format_ppb(value, hundredths);
    unsigned length = 0;
    out[length++] = '+';
    out[length++] = '/';
    out[length++] = '-';
    for (unsigned i = 0; value[i]; ++i) out[length++] = value[i];
    out[length++] = ' ';
    out[length++] = 'P';
    out[length++] = 'P';
    out[length++] = 'B';
    out[length] = 0;
}

static bool bluetooth_badge_at(uint8_t *fb, int x, int y, bool transmitting) {
    uint16_t background = transmitting ? C_BLUE : UI_COLOR(GREY);
    uint16_t symbol = transmitting ? C_WHITE : C_PANEL;
    R2_UI_BOOL_CALL(round_panel(fb, x, y, 11, 11, background, background));
    R2_UI_BOOL_CALL(line(fb, x+5, y+1, x+5, y+8, symbol));
    R2_UI_BOOL_CALL(line(fb, x+5, y+1, x+8, y+3, symbol));
    R2_UI_BOOL_CALL(line(fb, x+8, y+3, x+3, y+7, symbol));
    R2_UI_BOOL_CALL(line(fb, x+3, y+3, x+8, y+7, symbol));
    R2_UI_BOOL_CALL(line(fb, x+8, y+7, x+5, y+9, symbol));

 return true;
}
static bool bluetooth_badge(uint8_t *fb, bool transmitting) {R2_UI_BOOL_CALL(bluetooth_badge_at(fb,9,121,transmitting));
 return true;
}

static bool role_header_at(uint8_t *fb,const ui_snapshot_t *s,int top) {
#ifdef R2_DC_PROFILE
 R2_UI_BOOL_CALL(watch_panel(fb,top,13));R2_UI_BOOL_CALL(label(fb,10,top+3,"DIRECT USB",1,C_WHITE));return true;
#endif
 if(s->ui_role.grandmaster){R2_UI_BOOL_CALL(watch_panel(fb,top,13));R2_UI_BOOL_CALL(row_label7(fb,top+3,"Grandmaster",C_WHITE));return true;}
 const char *name=s->ui_role.header[0]?s->ui_role.header:s->unit_name?s->unit_name:"PTOB CLOCK";
 char compact[12];
 /* Saver prioritizes the actual provider when a pair name cannot retain
  * one blank pixel between glyphs. Dashboard header stays pixel-identical. */
 if(top!=1&&strlen(name)>11){
  const char *provider=strchr(name,'>');if(provider)name=provider;
  size_t n=strlen(name);
  if(n>11){memcpy(compact,name,9);compact[9]='.';compact[10]='.';compact[11]=0;name=compact;}
 }
 unsigned len=0;while(name[len])++len;
 if(len>14)len=14;
 const unsigned spacing=s->ui_role.marker_depth||len>12?5:6;const int width=(int)(len*spacing)-1+(s->ui_role.marker_depth?10:0);
 int x=(80-width)/2;R2_UI_BOOL_CALL(watch_panel(fb,top,13));
 if(s->ui_role.marker_depth){
  if(s->ui_role.marker_solid)R2_UI_BOOL_CALL(fill(fb,x,top+1,9,10,C_CYAN));
  else {R2_UI_BOOL_CALL(fill(fb,x,top+1,9,1,C_CYAN));R2_UI_BOOL_CALL(fill(fb,x,top+10,9,1,C_CYAN));R2_UI_BOOL_CALL(fill(fb,x,top+1,1,10,C_CYAN));R2_UI_BOOL_CALL(fill(fb,x+8,top+1,1,10,C_CYAN));}
  char n[2]={(char)('0'+s->ui_role.marker_depth),0};R2_UI_BOOL_CALL(label(fb,x+2,top+2,n,1,s->ui_role.marker_solid?C_BG:C_CYAN));x+=10;
 }
 for(unsigned i=0;i<len;++i,x+=(int)spacing){char c=name[i];if(top!=1&&c>='a'&&c<='z')c=(char)(c-'a'+'A');const uint8_t *g=glyph(c);for(unsigned col=0;col<5;++col)for(unsigned row=0;row<7;++row)if(g[col]&(1u<<row))R2_UI_BOOL_CALL(pixel(fb,x+(int)col,top+3+(int)row,C_DIM));}

 return true;
}
static bool role_header(uint8_t *fb,const ui_snapshot_t *s){R2_UI_BOOL_CALL(role_header_at(fb,s,1));
 return true;
}
static bool pair_lock(uint8_t *fb,int x,int y,uint16_t color) {
    /* Eight-pixel padlock beside the pair metric, without a font dependency. */
    R2_UI_BOOL_CALL(fill(fb,x+2,y,4,1,color));R2_UI_BOOL_CALL(fill(fb,x+1,y+1,1,3,color));
    R2_UI_BOOL_CALL(fill(fb,x+6,y+1,1,3,color));R2_UI_BOOL_CALL(fill(fb,x,y+4,8,5,color));
    R2_UI_BOOL_CALL(fill(fb,x+3,y+5,2,2,C_PANEL));R2_UI_BOOL_CALL(pixel(fb,x+4,y+7,C_PANEL));

 return true;
}
static bool front_clock_time(uint8_t *fb,const ui_snapshot_t *s,const char *zone_label){
    R2_UI_BOOL_CALL(watch_panel(fb,17,35));
    R2_UI_BOOL_CALL(row_label8(fb,20,zone_label?zone_label:s->gnss_native_health&&s->native_served_time?"UTC":s->lora_only?"UTC":s->source_provisional?"UTC":s->mesh_profile ? (s->mesh_utc?"UTC":"REL") : s->acquiring_first_lock ? "ACQ" : "UTC",C_DIM));
    const char *source = s->native_lora_source?(s->native_lora_acquiring?"ACQ":s->state==CLOCK_HOLDOVER?"HOLD":"LOR"):s->gnss_native_health&&s->native_reason==UI_NATIVE_NONE&&!s->native_analog_eligible?"SYNC":s->gnss_native_health&&s->native_ble_time?(s->native_time_relay_hops==1?"RC1":s->native_time_relay_hops==2?"RC2":"BLE"):s->gnss_native_health&&s->native_analog_eligible?"PPS":s->gnss_native_health&&s->native_served_time?"COARSE":s->gnss_native_health&&s->utc_valid?"HOLD":s->lora_only?"LOR":s->source_provisional?"PROV":s->acquiring_first_lock ?
                             (s->boot_elapsed_seconds >= 360000u ? "99H+" : "WAIT") :
                         s->utc_valid ? (s->state==CLOCK_HOLDOVER ? "HOLD" : s->network_time ? "NET" : "GPS") :
                         s->cached_time_valid ? "MEM" : "WAIT";
    R2_UI_BOOL_CALL(label(fb, s->native_lora_source||(s->gnss_native_health&&s->native_served_time) ? 36 : s->acquiring_first_lock ? 48 :
              s->utc_valid || s->cached_time_valid ? 46 : 48,
          20, source, 1, s->acquiring_first_lock ? C_AMBER :
                          s->utc_valid ? (s->state==CLOCK_LOCKED ? C_GREEN : C_AMBER) :
                          s->cached_time_valid ? C_AMBER : C_DIM));
    if (s->lora_only || (s->pps_seen && s->pps_age_ms < 1500u) ||
        (!s->acquiring_first_lock && (s->utc_valid || s->cached_time_valid)))
        R2_UI_BOOL_CALL(draw_pulse(fb, s->pulse_indicator));
    R2_UI_BOOL_CALL(digital_time(fb, s));

 return true;
}

static bool front_clock(uint8_t *fb, const ui_snapshot_t *s,const char *zone_label) {
    /* One shared watch face. A follower substitutes only its served-clock
       snapshot, header and existing beat panel; local GPS stays diagnostic. */
    ui_snapshot_t served;
    if(s->lora_only){
        served=*s;served.utc_second=s->lora_second;served.subsecond_quarter=s->lora_quarter;
        served.utc_valid=s->lora_available;served.cached_time_valid=false;
        served.acquiring_first_lock=!s->lora_available && !s->lora_seen;
        served.network_time=true;served.display_aligning=false;
        served.state=s->lora_available?CLOCK_LOCKED:CLOCK_NO_TIME;
        s=&served;
    }else if(s->mesh_profile){
        served=*s;served.utc_second=s->mesh_second;
        if(s->lora_time_override){served.utc_second=s->lora_second;served.subsecond_quarter=s->lora_quarter;}
        served.utc_valid=s->mesh_available;served.cached_time_valid=false;
        served.acquiring_first_lock=!s->mesh_available && !s->mesh_provider;
        served.network_time=true;served.display_aligning=false;
        served.state=s->mesh_available?(s->mesh_fresh?CLOCK_LOCKED:CLOCK_HOLDOVER):CLOCK_NO_TIME;
        s=&served;
    }
    /* GM-E1 provisional service already has an actual coarse UTC epoch.
       Precision remains unknown; never substitute boot uptime as its time. */
    R2_UI_BOOL_CALL(role_header(fb,s));
    R2_UI_BOOL_CALL(front_clock_time(fb,s,zone_label));
    R2_UI_BOOL_CALL(watch_panel(fb,54,62));
    if (s->gnss_native_health ? (s->native_analog_eligible && s->native_served_time && s->utc_valid) :
        ((s->lora_only && s->lora_available) || s->source_provisional || (!s->lora_only && (s->mesh_profile ? s->mesh_available :
        s->state == CLOCK_LOCKED && s->utc_valid && s->pps_seen &&
        s->pps_age_ms < 1500u && s->rmc_age_ms < 2200u))))
        R2_UI_BOOL_CALL(analog_clock(fb, s, 83));
    else if(s->mesh_profile || s->lora_only){
        R2_UI_BOOL_CALL(fill(fb,5,55,70,57,C_PANEL));R2_UI_BOOL_CALL(fill(fb,5,55,2,57,C_AMBER));
        R2_UI_BOOL_CALL(label(fb,13,65,s->lora_only?"LORA WAIT":"MESH WAIT",1,C_AMBER));
        R2_UI_BOOL_CALL(issue_metric(fb,83,"TIME","--",C_DIM));
        R2_UI_BOOL_CALL(issue_metric(fb,101,s->lora_only?"RX":"GPS",s->lora_only?"WAIT":"SHADOW",C_DIM));
    }else R2_UI_BOOL_CALL(sync_issue(fb, s));
    R2_UI_BOOL_CALL(watch_panel(fb,118,39));
#ifdef R2_DC_PROFILE
    R2_UI_BOOL_CALL(row_label7(fb,123,"U",C_CYAN));
#else
    R2_UI_BOOL_CALL(bluetooth_badge(fb, s->ble_tx_active));
#endif
    R2_UI_BOOL_CALL(fill(fb, 22, 122, 1, 9, C_BEZEL));
    if(!s->ui_role.grandmaster && (s->lora_only || s->mesh_profile || s->pair_active)){
        bool valid=s->lora_only?s->lora_metric_valid:s->pair_active?s->pair_metric_valid:s->mesh_metric_valid;
        bool pll=!s->lora_only && s->pair_active && valid && s->pair_locked && s->pair_rms_ns<200;
        if(pll)R2_UI_BOOL_CALL(label(fb,24,122,"PLL LOCK",1,C_DIM));
        else if(!s->lora_only && s->pair_active){R2_UI_BOOL_CALL(label(fb,24,122,"PAIR",1,C_DIM));R2_UI_BOOL_CALL(label(fb,49,122,"RMS",1,C_DIM));}
        else R2_UI_BOOL_CALL(label(fb,26,122,"RAW RMS",1,C_DIM));
        char value[16],raw[8];R2_UI_OPTIONAL_CALL(ui_format_count(raw,s->lora_only?s->lora_rms_ns:s->pair_active?s->pair_rms_ns:s->mesh_rms_ns));
        if(pll){
            bool settled=s->pair_correction_valid && s->pair_zero_settled && !s->pair_correction_ns;
            unsigned i=0;
            if(settled){const char *word="LOCKED";while(word[i]){value[i]=word[i];++i;}}
            else {const char *word="CORR ";while(word[i]){value[i]=word[i];++i;}
                if(s->pair_correction_valid){R2_UI_OPTIONAL_CALL(ui_format_count(raw,s->pair_correction_ns));for(unsigned k=0;raw[k];++k)value[i++]=raw[k];}
                else {value[i++]='-';value[i++]='-';}
                value[i++]='n';value[i++]='s';
            }
            value[i]=0;int x=(80-((int)i*6+11))/2;
            R2_UI_BOOL_CALL(label(fb,x,134,value,1,settled?C_GREEN:C_BLUE));
            R2_UI_BOOL_CALL(pair_lock(fb,x+(int)i*6+3,132,settled?C_GREEN:C_BLUE));
        }else{
            unsigned i=0;if(valid){for(;raw[i];++i)value[i]=raw[i];}
            else {value[i++]='-';value[i++]='-';}
            value[i++]=' ';value[i++]='n';value[i++]='s';value[i]=0;
            R2_UI_BOOL_CALL(label(fb,(80-(int)i*6)/2,133,value,1,valid?C_CYAN:C_DIM));
        }
        R2_UI_BOOL_CALL(fill(fb,8,143,64,1,C_BEZEL));R2_UI_BOOL_CALL(row_label8(fb,147,"RSSI",C_DIM));
        const bool rssi_valid=s->lora_only?s->lora_rssi_valid:s->mesh_rssi_valid;
        if(rssi_valid){R2_UI_OPTIONAL_CALL(signed_rate(value,s->lora_only?s->lora_rssi_dbm:s->mesh_rssi_dbm));}
        else {value[0]='-';value[1]='-';value[2]=0;}
        unsigned n=0;while(value[n])++n;
        value[n++]='d';value[n++]='B';value[n++]='m';value[n]=0;
        R2_UI_BOOL_CALL(label(fb,72-(int)n*6,147,value,1,rssi_valid?C_WHITE:C_DIM));
        return true;
    }
    R2_UI_BOOL_CALL(label(fb,26,122,s->gnss_native_health?(
#ifdef R2_DC_PROFILE
      "USB RMS"
#else
      s->native_lora_source?(s->native_lora_observed?"PRED RMS":s->native_lora_acquiring?"SYNCING":s->state==CLOCK_HOLDOVER?"REF AGE":"FIT RMS"):s->ui_role.grandmaster?"BEAT RMS":"SYNC RMS"
#endif
      ):s->copy_profile?"PPS PPB":s->basic_profile?"BASIC":"BEAT FRQ",1,C_DIM));
    char rate[16];
    if(s->gnss_native_health){const char *word=s->native_rms;unsigned i=0;while(word[i]){rate[i]=word[i];i++;}rate[i]=0;}
    else if (s->copy_profile) {
        if (s->clock_fit_valid) R2_UI_OPTIONAL_CALL(signed_rate(rate, s->clock_drift_ppb));
        else {rate[0]='N';rate[1]='O';rate[2]=' ';rate[3]='F';rate[4]='I';rate[5]='T';rate[6]=0;}
    } else if (s->basic_profile) {
        rate[0]='1';rate[1]=' ';rate[2]='H';rate[3]='z';rate[4]=0;
    } else if (s->beat_ppb_samples)
        R2_UI_OPTIONAL_CALL(format_beat_ppb(rate, s->beat_hundredth_ppb_average));
    else { rate[0] = '+'; rate[1] = '/'; rate[2] = '-';
           rate[3] = '-'; rate[4] = '-'; rate[5] = 0; }
    int length = 0;
    while (rate[length]) ++length;
    /* Native PLL icon follows live lock, without the face loss grace. */
    bool native_lock=s->gnss_native_health&&s->native_pll_locked;
    int rate_x=(80-length*6-(native_lock?11:0))/2;
    if(native_lock){R2_UI_BOOL_CALL(pair_lock(fb,rate_x,132,C_GREEN));rate_x+=11;}
    R2_UI_BOOL_CALL(label(fb, rate_x, 133, rate, 1,
          s->gnss_native_health ? (s->native_lora_acquiring?C_AMBER:s->native_rms_valid?C_CYAN:C_DIM) :
          s->beat_ppb_samples && s->state == CLOCK_LOCKED &&
          s->beat_hundredth_ppb_average < 100u ? C_GREEN : C_DIM));
    R2_UI_BOOL_CALL(fill(fb, 8, 143, 64, 1, C_BEZEL));
#ifdef R2_DC_PROFILE
    R2_UI_BOOL_CALL(label(fb,10,147,"DC",1,C_GREEN));
#else
    if(s->native_profile_supported)R2_UI_BOOL_CALL(label(fb,10,147,s->native_profile_rc?"RC":s->native_hot_standby?"HOT":"GM",1,(s->native_profile_rc?s->native_rc_serving_clients:s->native_hot_standby)?C_GREEN:C_DIM));
    else if(s->unit_name && s->battery_present) {
      char bat[8]; unsigned n=0;
      if(s->battery_valid) {unsigned pct=s->battery_percent;
        if(pct>=100){bat[n++]='1';}
        if(pct>=10){bat[n++]=(char)('0'+(pct/10)%10);}
        bat[n++]=(char)('0'+pct%10);bat[n++]='%';}
      else {bat[n++]='-';bat[n++]='-';}
      bat[n]=0;R2_UI_BOOL_CALL(row_label8(fb,147,bat,s->battery_valid && s->battery_percent<10?C_AMBER:C_DIM));
    } else R2_UI_BOOL_CALL(tight_italic_label(fb, 10, 147, "32kHz", C_DIM));
#endif
    R2_UI_BOOL_CALL(fill(fb, 41, 147, 1, 7, C_BEZEL));
#ifndef R2_DC_PROFILE
    if(s->gnss_native_health&&!s->ui_role.grandmaster){
        /* Four delivery bars for actual selected-parent timing reception.
         * Unknown/stale input leaves four empty outlines; TX is no reception. */
        for(unsigned i=0;i<4;i++){
            int x=49+(int)i*6,h=2+(int)i*2,y=154-h;
            if(s->native_rx_known&&i<s->native_rx_bars)R2_UI_BOOL_CALL(fill(fb,x,y,4,h,C_BLUE));
            else {uint16_t c=s->native_rx_known?C_BEZEL:C_DIM;
                R2_UI_BOOL_CALL(fill(fb,x,y,4,1,c));R2_UI_BOOL_CALL(fill(fb,x,153,4,1,c));
                R2_UI_BOOL_CALL(fill(fb,x,y,1,h,c));R2_UI_BOOL_CALL(fill(fb,x+3,y,1,h,c));}
        }
        return true;
    }
#endif
    char die[16];
    if (s->unit_name && s->usb_power) {die[0]='U';die[1]='S';die[2]='B';die[3]=0;}
    else if (s->unit_name && s->battery_valid) {die[0]='B';die[1]='A';die[2]='T';die[3]=0;}
    else if (s->die_temperature_valid) R2_UI_OPTIONAL_CALL(temperature(die, s->die_temperature_c));
    else { die[0] = '-'; die[1] = '-'; die[2] = 'C'; die[3] = 0; }
    unsigned die_length = 0;
    while (die[die_length]) ++die_length;
    R2_UI_BOOL_CALL(label(fb, 72 - (int)die_length * 6, 147, die, 1, C_DIM));

 return true;
}

void ui_render_pulse_at(uint8_t fb[UI_FRAME_BYTES],ui_pulse_t p,unsigned x,unsigned y){(void)draw_pulse_at(fb,p,x,y);}
void ui_render_pulse(uint8_t fb[UI_FRAME_BYTES],ui_pulse_t p){(void)draw_pulse(fb,p);}
void ui_render_rf_lamp(uint8_t fb[UI_FRAME_BYTES],unsigned r,unsigned k,uint8_t a,bool h){(void)draw_rf_lamp(fb,r,k,a,h);}
