# ESP32-REF notices

Copyright (c) 2026 Jonathan Yantis.

Original code, scripts and ordinary documentation in this collection use the
included [MIT license](LICENSE). This collection is newly written;
it does not import source from the operational W12 firmware or a private checkout.

Example 00 uses the host C standard library, a C11 compiler and Python's standard
library. Those tools and libraries retain their own terms. Nothing is vendored.

Examples 01 and 02 acquire the public ESP-IDF pin in `dependencies.lock.json` separately;
nothing from that SDK is vendored here. ESP-IDF, FreeRTOS, NimBLE, Mbed TLS and
their components retain their upstream license notices. A future binary
distribution needs its own complete dependency-license review. Secret-bearing
local test images are not public releases.

The linked research papers and their figures use CC BY 4.0. Future original
measurement datasets must record CC BY 4.0 explicitly. Synthetic walkthrough
text produced by the program is a software example, not imported hardware data.
