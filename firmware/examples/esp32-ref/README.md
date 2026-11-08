# ESP32-REF

Readable reference designs for learning how small devices can share time.
The hardware target is the **Meshnology W12 / ESP32-S3**.

**Start with [00 — Clock fundamentals](00_clock_fundamentals/README.md).**
It runs on your computer, using deterministic simulated observations. You can
study the clock arithmetic without an ESP-IDF installation or attached device.

## Available examples

| Example | Status | What it teaches |
| --- | --- | --- |
| [00 — Clock fundamentals](00_clock_fundamentals/README.md) | Implemented; host only | Counter wrap, ticks, rate error, continuous phase correction, causal prediction and expiry |
| [01 — Wireless BLE SoftPLL](01_wireless_softpll/README.md) | Host simulation and experimental native applications | One free-running leader, multiple followers, BLE event/follow-up discipline and W12 controls |
| [02 — Minimal BLE time receiver](02_minimal_receiver/README.md) | Offline host and native reference | Add BLE time to an existing project and compare descriptor timing profiles |
| 03 — Shared-time extensions | Planned | Study larger groups and source/topology changes |
| 04 — Holdover and recovery | Planned | Continue a learned estimate during source loss and recover gradually |
| [05 — W12 precision receiver](05_w12_precision_receiver/README.md) | Design and bench plan | Capture the W12 radio edge, let the servo settle, and measure accuracy against independent PPS |

00 runs entirely on a host. 01 adds an offline BLE simulation, automatic
frequency estimation, a separate example protocol, native ESP32-S3 applications
and a W12 display/button/power adapter. Its RX descriptor timestamps and
programmed TX estimates have unknown physical bounds. Sub-microsecond output
qualification needs an independent measurement. The offline installation review preparer records the required approval and
device handoff.

02 gets UTC from a serving T096 grandmaster using a short authenticated BLE
exchange, then follows its periodic broadcasts passively. The worker offers
optional system-clock discipline. The W12 demo has a clock, Info
page and hold-to-power-off controls. Its Basic, Refined and Fine descriptor
profiles have separate [bench gates](02_minimal_receiver/validation/README.md).

## Run the checks

From the repository root, with Python 3.9+ and a C11 compiler:

```sh
python3 firmware/examples/esp32-ref/tools/check.py \
  --out artifacts/esp32-ref-00-review
artifacts/esp32-ref-00-review/clock_fundamentals
```

Use a fresh output directory for each check. The runner compiles the lesson and
tests, executes both, and writes logs, a source manifest and `receipt.json`.
It opens no devices and downloads nothing. Add `--sanitize` for AddressSanitizer
and UndefinedBehaviorSanitizer when your compiler supports them.

## Find your way around

```text
00_clock_fundamentals/   Counter and continuous-clock walkthrough
01_wireless_softpll/     BLE simulation, native application and board ports
02_minimal_receiver/    Copyable BLE grandmaster receiver and W12 demo
05_w12_precision_receiver/  W12 edge capture design and bench plan
components/             Independent clock, PLL, protocol, UI and lifecycle code
docs/                   Timing, wire, S3 adapter and W12 display contracts
tests/                  Analytic, refusal, power and preview fixtures
tools/                  Offline checks, native build and review preparation
dependencies.lock.json  Public dependency pins and host requirements
```

Each lesson should explain the concept before introducing its implementation.
Read the [00 README](00_clock_fundamentals/README.md), then `lesson.c`, then the
small component headers and sources. The [timing model](docs/timing-model.md)
defines the contracts behind those functions. The same order works well for
agents helping a human reader.

## Design rules

- Write explicit, ordinary C with named units and short functions. Explain why
  a calculation or rejection exists. Keep hardware details in separate adapters.
- Keep this collection independent of operational target source. There are no
  includes, copied implementations or build dependencies on `firmware/w12/`.
- Keep timing state bounded. The current core allocates no heap memory and uses
  no floating point. Report actual resource costs when hardware builds exist.
- Score a prediction before its observation can influence a new clock model.
  Keep raw events, correction availability and model activation distinct.
- Preserve failures, missing observations and unknown bounds. Simulations,
  relative measurements and independently measured outputs have distinct claims.
- Discover nearby sources at runtime. Keep trust enrollment separate and
  re-establish authenticated scope after context changes. Keep real credentials
  and device bindings local and ignored.

The hardware examples need their own reviewed board routes, capture
semantics and transmit-time characterization. W12's packet-specific empirical
offsets are not defaults for a new example protocol. Native builders must use
the repository's [firmware retention lifecycle](../../../docs/firmware-storage.md).
Hardware installation and operation require separately authorized device scope.

## Research behind the lessons

The [software-clock research](../../../papers/oscillator-pll-research-20261003.pdf)
and [ESP32-S3 implementation study](../../../papers/ptob-esp32s3-low-level-timing.pdf)
explain the research methods. ESP32-REF is a new teaching implementation; those
historical measurements do not qualify it. Each precision claim needs
a named measurement point and fresh hardware evidence.

Original code and ordinary documentation: copyright (c) 2026 Jonathan Yantis,
[MIT](LICENSE). See [NOTICE.md](NOTICE.md) for dependency terms.
