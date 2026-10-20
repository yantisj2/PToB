# N30 source notices

Copyright (c) 2026 Jonathan Yantis. Original target adapters, renderer, tests,
tooling and documentation use MIT. This portrait renderer adapts T096's clock/RF/LoRa
composition, and selects W12 bitmap font data, credited in the import manifest.
The selected W12 entry-point/log adaptations retain compatible diagnostics.

W12 timing sources are compiled directly and retain their existing per-file
notices. Inherited Apache-2.0 notices remain authoritative; the
[full license](../../LICENSES/Apache-2.0.txt) is included. W12's
[g1_hmac.c](../w12/main/g1_hmac.c) retains Intel/TinyCrypt BSD-3-Clause terms.
See [W12 source notices](../w12/NOTICE.md). No SDK or private history is imported.
Pinned public ESP-IDF/NimBLE/TinyCrypt and other components retain their terms.
Any future linked binary redistribution requires its own dependency review.
The previews are original ordinary interface documentation under MIT and are
synthetic renderings, not evidence datasets or accuracy observations.

N30 also compiles selected T096 RC8 wire3 codec, receiver policy and inline
precision helpers directly. Their MIT/Apache-2.0 per-file notices are preserved;
see [the selected-source manifest](imports/2026-10-20-lora.json). T096's Zephyr
hardware driver is a modem reference only; N30 owns its ESP-IDF RX-only driver.
The existing CounterMap helper is used solely to relate hardware counter rates;
no GNSS driver, input or authority is enabled. No RadioLib dependency is added.
