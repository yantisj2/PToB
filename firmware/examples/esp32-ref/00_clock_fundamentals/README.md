# 00 — Clock fundamentals

Build a small software clock and learn what its numbers mean before adding a
radio, interrupt or control loop. This example runs entirely on your computer.
All inputs are deterministic simulations. You need no W12 or ESP-IDF SDK.

You will learn to:

1. Extend a wrapping 32-bit counter into a longer tick coordinate.
2. Convert ticks into nanoseconds with explicit rounding.
3. Correct a known frequency error in software.
4. Consume a phase correction gradually while keeping time continuous.
5. Preserve a prediction made before a delayed observation was available.
6. Refuse reads after an expiry or counter discontinuity.

The rate correction is supplied by the lesson. An automatic estimator and a
complete softPLL are future work in 01.

## Run it

Requirements: Python 3.9+ and a C11 compiler, such as Clang or GCC. The runner
uses only Python's standard library and the system C library. It downloads
nothing, reads no device ports and does not consult another firmware target.

From the repository root:

```sh
python3 firmware/examples/esp32-ref/tools/check.py \
  --out artifacts/esp32-ref-00-review
artifacts/esp32-ref-00-review/clock_fundamentals
```

The first command builds and executes the lesson and tests. The second displays
the walkthrough again. Use a new output directory for each build; old output is
preserved. `lesson.log` contains the walkthrough, `tests.log` contains the checks
and host state sizes, and `receipt.json` records commands and hashes.

For additional arithmetic and memory checks:

```sh
python3 firmware/examples/esp32-ref/tools/check.py \
  --sanitize --out artifacts/esp32-ref-00-sanitized
```

Select a compiler with `--cc clang`, `--cc gcc` or the `CC` environment variable.
Sanitizer support depends on your compiler and execution environment. Select
`--sanitize undefined` or `--sanitize address` to run either check separately.
Each compile/run command has a 60-second limit. A failure preserves its logs and
failed receipt. Initial verification is on macOS; other hosts need their own run.

The whole `esp32-ref` directory can also be copied elsewhere. Run its
`tools/check.py` with an output directory outside that copy. No Git checkout,
private credentials or sibling repository is needed.

## Read the code in this order

| File | Question it answers |
| --- | --- |
| [main.c](main.c) | Where does execution start? |
| [lesson.c](lesson.c) | What do the five exercises do? |
| [ref_counter.h](../components/ref_clock/ref_counter.h) and [.c](../components/ref_clock/ref_counter.c) | How can a raw rollover be extended safely? |
| [ref_clock.h](../components/ref_clock/ref_clock.h) | What are the clock's units and caller obligations? |
| [ref_clock.c](../components/ref_clock/ref_clock.c) | How do conversion, rate and phase combine? |
| [core_test.c](../tests/core_test.c) | Which analytic results and failure cases are checked? |

Keep the [timing model](../docs/timing-model.md) nearby for the equations and
rounding details. The code is ordinary C11: no framework, heap allocation or
floating-point epoch arithmetic. `printf` and the Python runner are host teaching
tools, outside the reusable arithmetic core.

## 1. Ticks survive a raw rollover

At a nominal 80 MHz, a counter advances 80 ticks per microsecond. Its 32-bit raw
register wraps every 53.6870912 seconds. Unsigned subtraction can recover a small
forward interval across that wrap:

```text
raw before: 4,294,967,216
raw after:              0
elapsed:               80 ticks = 1,000 ns
```

The extended value remains in one continuous tick coordinate. A separate
monotonic watchdog clock rejects poll gaps at least half a wrap long. At 80 MHz,
26,843,546 whole microseconds is already too late. A raw delta at least half a
wrap, backward watchdog time or extension overflow also invalidates the counter.

After refusal, explicitly begin a new counter lifetime and withdraw any software
clock tied to the old lifetime. The two components do not reset each other.

This exercise assumes the declared counter frequency and a trustworthy watchdog.
It does not detect a stopped oscillator or prove a physical frequency bound.
A hardware adapter must establish those properties and a safe polling margin.

## 2. Rate changes how fast software time advances

A tick is a hardware count; a nanosecond is a time unit. The conversion assumes
a nominal frequency. If the oscillator is actually 20 parts per million fast,
it produces 80,001,600 ticks in one reference second. Nominal conversion then
reports 1,000,020,000 ns: 20,000 ns too much.

The lesson supplies a **−20,000 ppb** software rate correction. After ten
reference seconds the nominal error is +200,000 ns; corrected error is −4 ns.
That tiny residual comes from a first-order inverse-rate approximation and
integer rounding in this noiseless constructed scenario. The exact inverse is
approximately −19,999.600008 ppb. No physical accuracy is measured.

Positive `rate_ppb` makes software time advance faster. A positive hardware
frequency error therefore needs a negative conversion-rate correction.
The physical oscillator continues running unchanged.

One 80 MHz tick is 12.5 ns. Converting one tick returns 12 whole nanoseconds;
converting two ticks returns 25. Preserve raw ticks and convert the whole
interval once: adding two separately rounded 12 ns values would lose a
nanosecond. Nanosecond representation does not imply nanosecond accuracy.

## 3. Phase correction takes time

Suppose the delivered clock is 2,000 ns behind. Stepping it forward immediately
would change its value at the update instant. Instead, reanchor at the value
already delivered and spend a phase backlog gradually:

```text
elapsed after update     applied correction     remaining correction
0 s                              0 ns                 +2,000 ns
1 s                         +1,000 ns                 +1,000 ns
2 s                         +2,000 ns                      0 ns
3 s                         +2,000 ns                      0 ns
```

A 1,000 ppb slew adds 1,000 ns per nominal second until the backlog is consumed.
A negative backlog slows the clock temporarily. Both directions preserve
forward progress under this lesson's frequency and correction limits.

`ref_clock_adjust()` replaces the total backlog at the update tick. It does not
add the new request to an old backlog. It also does not estimate a rate or renew
the model's deadline. Those decisions belong to a later controller.

## 4. Delayed information cannot improve an earlier prediction

At the event at 2.000 s, save the current predicted timestamp. At 2.250 s, a
correction supplies the reference timestamp for that earlier event. First score
the saved prediction: this example's error is +1,000 ns. Then adjust the clock
from the time the correction becomes available.

```text
event capture           correction available             future reads
save prior prediction -> score it, then activate update -> use new model
```

The updated model refuses reads before its activation anchor. A saved copy of
the prior model can still evaluate its own interval. The recorded +1,000 ns
error remains unchanged. This is the foundation of causal validation: the
observation being scored cannot train the model used to score it.

This scenario deliberately has constant rate during delivery delay. A real
controller must project an observation's phase/rate information to the update
tick, account for intervening updates, and associate it with the exact event.

## 5. Availability is part of a timestamp

The exclusive deadline is in the same extended counter coordinate as the model.
A clock with a 1 s deadline refuses a read exactly at 1 s. Adjusting its rate or
phase at 0.5 s leaves that deadline unchanged.

Every read returns a boolean. Use the output only when it returns `true`.
Refusal leaves the output variable unchanged; a stale value in that variable
must never be mistaken for a new timestamp. Time zero is a valid relative epoch.

There is no source identity, authentication, uncertainty estimator or UTC epoch
in 00. A later adapter must bind a model to its counter lifetime and source
session, invalidate it on changes, and enforce admission and original expiry.

## Try a small experiment

- Change the known `rate_ppb` in `frequency_error()` to zero, then reverse its
  sign. Predict the errors before running the program.
- Change the phase backlog from +2,000 ns to −2,000 ns. Watch the clock's
  value continue forward while the phase backlog shrinks toward zero.
- Double the slew rate and predict when the same backlog will be consumed.
- Move a read to the exact deadline and inspect the returned boolean.
- Save a model copy before an adjustment and compare the two models' supported
  intervals. Keep the original pre-update error in your report.

Keep these as separate experiment builds. The baseline check does not depend on
an exact text transcript, so you can edit the walkthrough without rewriting tests.

## Limits and the next lesson

The clock supports nominal frequencies from 1 Hz through 80 MHz, relative epochs
from zero through `INT64_MAX` nanoseconds, rate corrections within ±50,000 ppb
and phase slew through 50,000 ppb. Reads refuse unrepresentable arithmetic.
The [model contract](../docs/timing-model.md) explains these bounds.

This is an arithmetic foundation. It has no automatic PLL training, hardware
capture, physical output, BLE, GNSS or measured sub-microsecond result.
[01 — Wireless BLE SoftPLL](../01_wireless_softpll/README.md) adds BLE event pairs,
frequency estimation, an offline simulation and experimental native applications
while keeping the clock map and hardware capture separate.

Copyright (c) 2026 Jonathan Yantis. [MIT](../LICENSE).
