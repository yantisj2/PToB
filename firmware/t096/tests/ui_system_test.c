/* Actual System-page helpers, including beyond 32-bit millisecond uptime. */
#include "ui.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
 char text[16];
 r2_ui_uptime_text(text,0);assert(!strcmp(text,"0d00:00:00"));
 r2_ui_uptime_text(text,UINT64_C(4294967296));assert(!strcmp(text,"49d17:02:47"));
 r2_ui_uptime_text(text,UINT64_C(86399999000));assert(!strcmp(text,"999d23:59:59"));
 r2_ui_uptime_text(text,UINT64_C(86400000000));assert(!strcmp(text,"1000d 00h"));
 r2_ui_uptime_text(text,UINT64_MAX);assert(!strcmp(text,">999999d"));
 for(unsigned i=0;i<=R2_UI_RESET_WD_RECOVERY;i++)assert(strlen(r2_ui_reset_text(i))<=12);
 assert(!strcmp(r2_ui_reset_text(R2_UI_RESET_WD_PROFILE),"WD PROFILE"));
 assert(!strcmp(r2_ui_reset_text(R2_UI_RESET_WD_CORE),"WD CPU"));
 assert(!strcmp(r2_ui_reset_text(UINT32_MAX),"UNKNOWN"));
 puts("PASS System UI: 64-bit system uptime, 49-day boundary, bounded long uptime, all reset labels fit 80px");
}
