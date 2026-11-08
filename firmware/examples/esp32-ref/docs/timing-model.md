# Example 00 timing model

This contract belongs to the new host-only teaching implementation. Its inputs
are simulated. It establishes arithmetic behavior; it supplies no physical
accuracy bound.

## Coordinates and units

| Name | Unit | Meaning |
| --- | --- | --- |
| `raw_ticks` | ticks | Wrapping 32-bit sample from one counter |
| `extended_ticks` | ticks | 64-bit extension of that same counter |
| `poll_us` | microseconds | Separate monotonic watchdog time |
| `frequency_hz` | ticks/second | Declared nominal counter frequency |
| `anchor_tick` | ticks | Local tick where the model becomes available |
| `anchor_ns` | nanoseconds | Delivered relative time at that anchor |
| `rate_ppb` | parts per billion | Persistent correction to nominal elapsed time |
| `phase_remaining_ns` | nanoseconds | Phase backlog at the anchor |
| `slew_ppb` | parts per billion | Maximum rate for consuming that backlog |
| `valid_until_tick` | ticks | Exclusive deadline in the same counter lifetime |

The epoch is arbitrary and nonnegative. There is no UTC translation. Source
session, counter incarnation and admission metadata are responsibilities of a
future adapter. All structs have one owner; they provide no internal locking.

## Counter extension

For successive raw values, compute the unsigned modular difference:

```text
delta = (raw_now - raw_previous) mod 2^32
extended_now = extended_previous + delta
```

Require `delta < 2^31`. Also require a nondecreasing watchdog time with a gap
strictly smaller than `2^31 / frequency_hz` seconds. Integer comparisons preserve
that strict boundary. The frequency and watchdog assumptions must hold; a real
adapter needs validated clock-rate margins and progress checks. A stopped timer
is outside this component's detection contract.

The initial extended value equals the initial raw value. A long gap, backward
watchdog, large raw delta or overflow invalidates the extension until an explicit
`ref_counter_begin()`. No attempt is made to guess missed wraps. Restarting the
extension requires separately invalidating any clock tied to its previous lifetime.

## Tick conversion

For elapsed ticks `d` and frequency `F`:

```text
whole_seconds = d / F
remainder_ticks = d % F
elapsed_ns = whole_seconds * 1,000,000,000
           + floor(remainder_ticks * 1,000,000,000 / F)
```

`F` fits in uint32, so the remainder product fits in uint64. Whole seconds and
the final sum are checked for overflow. The standalone conversion helper accepts
any nonzero uint32 frequency. Software-clock models use the narrower 1..80 MHz
teaching range described below.

Keep original ticks; converting separate subintervals and adding rounded values
can differ from converting the complete interval.

## Read a model

For a read at tick `c`, first require:

```text
model.valid && anchor_tick <= c && c < valid_until_tick
```

Let `n` be nominal elapsed nanoseconds since the anchor. Before rounding, the
intended map is:

```text
rate_correction = n * rate_ppb / 1,000,000,000
slew_budget = n * slew_ppb / 1,000,000,000
slew_used = sign(phase_remaining_ns)
          * min(abs(phase_remaining_ns), slew_budget)
time_ns = anchor_ns + n + rate_correction + slew_used
```

The implementation splits whole seconds from fractional nanoseconds before
multiplication. It rounds each correction magnitude down, then restores its sign
(equivalent to rounding a signed correction toward zero). Phase is clamped to
the backlog, so consuming it cannot overshoot. Each new anchor starts a new
rounding interval; repeated reanchoring can accumulate small quantization changes.

The model supports nominal frequency at most 80 MHz: adjacent ticks add at least
12 nominal whole nanoseconds. With rate magnitude and slew each limited to
50,000 ppb, corrections cannot erase that progress. At a rounding boundary,
two separately negative corrections can each decrement by one nanosecond;
the nominal increment still exceeds both. This simple frequency bound keeps the
integer map monotonic without fractional state. Raising it needs a new rounding
analysis and tests.

Elapsed nanoseconds must fit `int64_t`. Signed additions are checked individually;
an unrepresentable intermediate is refused even if a later subtraction could
bring the mathematical result back into range. Final time must be nonnegative.
No signed overflow, floating-point epoch or saturation is used to invent a value.

## Update without stepping

At update tick `u`, read the old model first:

```text
old_value = old_model(u)
new.anchor_tick = u
new.anchor_ns = old_value
new.rate_ppb = requested_rate
new.phase_remaining_ns = requested_total_phase
new.slew_ppb = requested_slew
new.valid_until_tick = old.valid_until_tick
```

At elapsed zero both new corrections are zero, so the update preserves the
delivered value exactly. The requested phase replaces the total backlog; it is
not added to the old backlog. A nonzero backlog requires a nonzero slew rate.
`INT64_MIN` phase is refused because its positive magnitude is not representable
as an `int64_t`.

An adjustment is prospective and cannot extend a deadline. Failure leaves the
model unchanged. `begin()` explicitly discards a lifetime, and a failed begin
leaves it invalid. `invalidate()` withdraws availability. A read failure leaves
its output unchanged and never grants permission to use that old output.

## Causal scoring

Save the model's prediction when the original event is captured. When the
matching reference timestamp arrives, score that saved prediction before making
an adjustment. A copy of the old model can also be retained. The new model's
anchor refuses earlier event ticks, preventing its accidental use for that score.

Example 00 supplies a constant-rate delayed observation. A real controller must
project the information to update time, preserve event association and bind it
to the same source/counter session. No such controller or BLE wire is implemented.

Copyright (c) 2026 Jonathan Yantis. [MIT](../LICENSE).
