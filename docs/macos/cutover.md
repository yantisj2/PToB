# macOS source cutover — 16 October 2026

Copyright (c) 2026 Jonathan Yantis. MIT.

PToB owns the selected USB/chrony client at `clients/macos/`. The source version
is `ptob-mac-cli/0.21-sof-map-gap`, selected by exact per-file hashes from the
latest working source. Its containing laboratory base commit is
`2d1b0b2699bb299ea82647d43f9142b7d0f0f3e7`; many selected files were uncommitted
or untracked, so that commit alone does not identify their bytes. The import
manifest makes this distinction explicit and records all transformations.

The inspected October 15 closeout reports a separate 300.064995 s capture with
source-build hash
`94f74a1facc6b66132a53d70e51739bf0769551bd2d0623300d9a44c7cf6a196`.
Its settled 60–300 s estimator and direct shared-source comparison are different
populations. Startup, withheld observations, source gaps, earlier failed runs and
absence of natural missing-SOF-map branch coverage remain limitations. Those
historical results are not a new test of the migrated bytes or independent UTC
accuracy. Paper/evidence editions are curated separately.

The public extraction includes only the current USB client, selected native and
synthetic host checks, strict USB helper, chrony build/foreground wrapper and
precision-analysis tools. Archived direct-BLE client paths, trial packages,
private captures and live deployment configuration are excluded. Laboratory
identity fixtures were replaced consistently with synthetic IDs; shared registry
aliases now supply presentation names. Protocol identity and permission remain
based on full IDs and provenance.

Offline checks on macOS 26.7 arm64, Apple Swift 6.2 (language mode 5): native
compile; 321 source/clock/permission/instrument/chrony checks; ObjC strict decoder
self-test; 23 CLI checks; 47 synthetic chronyc terminal/replay checks; 10 terminal
hold/expiry checks; eight precision-analysis tests. Chrony 4.9 builds from the
hash-verified public archive; only its version command was executed. The actual
private daemon, USB discovery and live clock were not started during migration.

No system clock/control setting, background service, hardware, push, release or
deployment changed. Fresh network acquisition and clean-host setup, full live
private-daemon lifecycle and physical USB behavior require separate acceptance.
Default fine UTC remains an unqualified estimate; stratum and low estimator RMS
do not establish absolute UTC, independent precision or continuous availability.
