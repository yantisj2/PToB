/* MIT. Copyright (c) 2026 Jonathan Yantis. Host-only composition records. */
#pragma once
struct gm7_progress_record {
  uint64_t now;
  struct r2_service_status status;
  uint32_t expected_stall;
};
#define GM7_PROGRESS_RECORDS 16
