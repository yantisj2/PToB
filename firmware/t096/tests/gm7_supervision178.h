/* MIT. Copyright (c) 2026 Jonathan Yantis.
 * Frozen production 0.15.178 supervisor (d27194e), function name only changed.
 * Historical failing policy reference; never linked into firmware. */
static void old_supervise(void) {
  if (!started)
    return;
  (void)k_mutex_lock(&lock, K_FOREVER);
  uint64_t now = kernel_now();
  bool closed = status.closed;
  if (atomic_get(&stopping) && !closed && now >= close_until && !status.error)
    status.error = -ETIMEDOUT;
  bool expired = status.deadline && now >= status.deadline;
  if (atomic_get(&busy) && now >= status.supervision.submitted &&
      now - status.supervision.submitted >= 100000) {
    if (!status.supervision.fault_site) {
      status.supervision.fault_site = 1;
      status.supervision.fault_at = now;
      status.supervision.fault_state = status.supervision.state;
    }
    if (!status.error)
      status.error = -ETIMEDOUT;
    expired = true;
  }
  bool submit = (!closed || resuming) && atomic_cas(&busy, 0, 1);
  if (submit) {
    status.supervision.submitted = now;
    status.supervision.state = 1;
  }
  (void)k_mutex_unlock(&lock);
  if (expired)
    r2_service_stop();
  if (submit) {
    int rc = k_work_submit_to_queue(&owner_queue, &service_work);
    if (rc < 0) {
      atomic_clear(&busy);
      status.error = rc;
      r2_service_stop();
    }
  }
}
