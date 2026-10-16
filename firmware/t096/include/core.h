/* SPDX-License-Identifier: Apache-2.0 */
#ifndef R2_CORE_H
#define R2_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define R2_COMMAND_BYTES 192
#define R2_REPLY_BYTES 3072
#define R2_COMMAND_SLOTS 4
#define R2_TX_SLOTS 2
#define R2_REPLY_MS 2000
#define R2_MAINTENANCE_MS 2000
#define R2_BOOT_MS 2000
#define R2_CLOCK_MS 20
#define R2_TRACE_COUNT 32
enum r2_op { R2_STATUS, R2_TRACE, R2_PREPARE, R2_COMMIT, R2_CLOCK, R2_UI, R2_BLE, R2_NETWORK, R2_DIAG, R2_MARK, R2_PRECISION, R2_RELAY, R2_ROUTE, R2_NAME, R2_JOIN, R2_SHADOW, R2_WITNESS, R2_RADIO_AUDIT, R2_PROFILE
#ifdef R2_RC_PROFILE
 , R2_RC_PARENT
#endif
};
enum r2_maintenance { R2_IDLE, R2_CLOSING, R2_READY, R2_RESET, R2_FAULT };
struct r2_request {
  uint32_t id;
  uint64_t boot, nonce;
  unsigned trace_page;
  uint32_t seconds;
  char silicon[17], source[65],name[25];
  enum r2_op op;
};
struct r2_maint {
  enum r2_maintenance phase;
  uint64_t nonce, deadline;
  int error;
};
int r2_parse(const char *line, struct r2_request *out);
bool r2_bound(const struct r2_request *r, const char *silicon, const char *source, uint64_t boot);
int r2_prepare(struct r2_maint *m, uint64_t nonce, uint64_t arrival, uint64_t now, bool safe);
int r2_closed(struct r2_maint *m, uint64_t now, int result);
int r2_commit(struct r2_maint *m, uint64_t nonce, uint64_t arrival, uint64_t now);
void r2_expire(struct r2_maint *m, uint64_t now);
uint32_t r2_crc32(const void *data, size_t size);
int r2_frame(char *out, size_t capacity, uint32_t request, uint32_t sequence, const char *json);
#endif
