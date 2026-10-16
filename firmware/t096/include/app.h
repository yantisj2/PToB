/* SPDX-License-Identifier: Apache-2.0 */
#ifndef R2_APP_H
#define R2_APP_H
#include "core.h"
#include <zephyr/kernel.h>
struct r2_identity { char silicon[17], unit[6]; uint64_t boot; int battery, result; uint32_t reset_cause; };
struct r2_command { uint64_t arrival; char text[R2_COMMAND_BYTES]; };
struct r2_io { uint32_t rx_bytes, tx_bytes, rx_overflow, command_drops, tx_drops, tx_expired, fifo_errors, usb_events, dtr_changes,note_drops; int usb_rc; bool dtr; };
struct r2_platform { int clock_request, clock_result, clock_release, clock_status, bt_result, bt_submit, close_result; uint64_t clock_start, clock_end, bt_start, bt_end, close_end; bool clock_done, bt_done, close_done; };
int r2_board_safe(void);
void r2_identify(struct r2_identity *id);
int r2_bootloader_request(void);
int r2_board_poweroff(void);
void r2_platform_start(void);
int r2_platform_close(void);
void r2_platform_snapshot(struct r2_platform *out);
void r2_console_start(void);
bool r2_console_get(struct r2_command *out);
int r2_console_reply(const char *bytes, size_t size, uint64_t expiry);
/* Optional diagnostics: nonblocking, DTR-only, empty queue; never a reply. */
int r2_console_note(const char *bytes,size_t size,uint64_t expiry);
void r2_console_snapshot(struct r2_io *out);
#endif
