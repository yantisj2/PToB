# SS3 source cutover — 16 October 2026

Copyright (c) 2026 Jonathan Yantis. MIT.

PToB owns normal SS3 development at `firmware/ss3/`, public version 0.1.0.
The selected isolated source is `56740553885420a7f6f301c12de10696a9c8d432`
(V17 health-display hold). Its dated historical closeout identifies application
SHA256 `01892be23e0ea48bb14fd2a32b494b850a7421f777734d1742db26751d1e0ab7`
and ELF SHA256
`3542e0f5c49dc54efe62f0698762b32328ac9e890fce692effd69477782fe296`.
That is an inspected source-selection identity, not a fresh installed-image claim.

This explicit selection supersedes the earlier V16 signal source
`e3432e8d486d192d5ec87876dbac9cadf6e4aea2`. The original V17 record retains
unobserved physical pixels, unmeasured airtime, pending outdoor range and
unqualified precision/reliability. No earlier successful short observation
promotes this public successor to hardware qualification.

The public extraction preserves the selected radio/clock/TD source and replaces
private build paths, factory rosters, enrollment and display tables with pinned
public acquisition, local ignored inputs and the shared display registry.
No private Git history, keys, real registry, images, raw captures or recovery
artifacts were imported. Per-file source/public hashes document transformations.

Offline migration checks on Apple Silicon macOS 26.7:

- SS3 BLE100 firmware build, application slot/layout, runtime input binding,
  compiler pin and forbidden WiFi/NVS symbol checks pass with synthetic enrollment.
- Eight production model/renderer/clock/lifecycle programs pass ASan/UBSan.
- Thirty-six RC7 model cases, nine common native-client cases, compiled vectors
  and independent HMAC verification pass with synthetic controller boundaries.

Early extraction attempts exposed a legacy display-interface mismatch and an
unused inherited source reference; both were corrected before the passing build.
The first SDK configuration attempt hit sandbox process enumeration; disabling
the unneeded component manager supplied the standalone no-managed-dependency
build boundary. Failed attempt logs remain ignored local artifacts.

No hardware operation, chronyd service, OS clock change, push, release or
deployment occurred. Clean-host public network acquisition, general public
installation/recovery, full physical controller/PSRAM/USB coverage, field range,
continuous service reliability, optical scanout and independent UTC/output
precision remain separate review gates.
