# M-HAT firmware changelog

Copyright (c) 2026 Jonathan Yantis. MIT.

## 0.5.5 — 2026-11-02

Fix the PPS planner rejecting an otherwise admitted imminent pulse when the
one-second search endpoint lies beyond the projected source lease. Search for
the target or the first invalid tick, then retain the existing valid-crossing,
minimum-lead and complete-pulse expiry checks. No admission horizon is extended.

Production-projection tests reproduce the valid-rise/valid-fall case and reject
expired rising edges and pulses whose falling edge would outlive permission.
These are offline checks; hardware installation, availability and timing
qualification require separate evidence.
