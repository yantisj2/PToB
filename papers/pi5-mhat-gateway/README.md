# Raspberry Pi / M-HAT timing gateway

Jonathan Yantis. Paper and original figures: CC BY 4.0. Ordinary documentation
and original tooling: MIT.

The [seven-page paper](main.pdf) is an **author-review draft, not approved for
publication**. [main.tex](main.tex) is canonical. The [edition manifest](edition.json)
binds its source, PDF, figure, selected evidence and verification tooling.
No existing approved edition is replaced or promoted.

The paper describes authenticated BLE PToB reception on the B504e/nRF52840,
hardware-timer PPS through the M-HAT bus to Pi GPIO16/chrony, and conservative
NTP/PTP clock ownership. Its complete final 600.026-second window retained
600/600 new pulses with one source cohort and no observed gaps or restarts.
Raw GPIO assertion phase RMS was 872 ns, absolute P99 4.809 µs and maximum
7.017 µs, with every tail retained. These are feedback-path measurements against
the disciplined Pi clock, not independent UTC or electrical accuracy.

Only this final fixed-window population and its separate protocol-health checks
enter the paper and [selected evidence](../../evidence/pi5/2026-11-02/README.md).
Troubleshooting, private identities and raw laboratory logs remain private.
The original preparation receipt preserves the earlier pending-data checkpoint;
the completed edition manifest is the current draft record.

The Linux USB/SOF bridge inspired by the existing macOS client and external
PTP-to-PToB ingress are proposed designs. Neither is implemented or measured on
this Pi. Ethernet was disconnected; loopback NTP and Wi-Fi PTP egress checks do
not establish remote delivery or sub-microsecond output. The paper distinguishes
receiver fit RMS, chrony estimates, raw GPIO phase, availability and unknown bias.

Open this directory in the built-in LaTeX editor. Ordinary offline verification
uses Python 3.9+ and does not access devices, private raw logs or credentials:

```sh
python3 tools/verify_pi5_gateway.py
```

Rebuild the figure using Python 3.9+, Matplotlib 3.9.4 and NumPy 2.0.2 (or a
compatible public installation), then compile with public Tectonic 0.17.0 and its
v33 resource bundle:

```sh
python3 papers/pi5-mhat-gateway/tools/plot_results.py
mkdir -p artifacts/pi5-paper-rebuild
tectonic --keep-logs --outdir artifacts/pi5-paper-rebuild papers/pi5-mhat-gateway/main.tex
```

The compiler may fetch public TeX resources. Rebuilds can differ in PDF metadata
or font versions; the verifier checks the prepared edition's exact bytes.
The figure script uses only selected CSV files. All plots preserve the complete
population and use nanosecond units without treating resolution as accuracy.
