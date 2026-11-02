# Pi 5 target notices

Copyright (c) 2026 Jonathan Yantis. Original code and ordinary documentation use
the repository [MIT license](../../LICENSE).

Raspberry Pi OS, Debian packages, Linux, BlueZ and systemd are acquired separately
and retain their own licenses and notices, including their installed
`/usr/share/doc/*/copyright` files. The smoke-test binary is local development
output; no OS image, linked binary or firmware is distributed here.

Particle CLI 3.52.4 and particle-usb 4.3.1 are separately acquired Apache-2.0
dependencies. Their transitive dependencies retain their own terms. Keep the npm
lockfile and installed notices with local tooling. No Particle firmware, account
credential, factory binding, private key or third-party source snapshot is
included in this target.

Chrony, linuxptp, PTPd and Tailscale are separately installed packages under
their own licenses. Use their installed copyright/source notices for binary
redistribution. Original dashboard and provisioning source remains MIT.

`source-provenance.json` records the selected original PToB probe restored from
its exact image-embedded source. The original code remains MIT. Its existence
and checksum do not qualify hardware accuracy or precision streaming.

Jeff Geerling's [Time Pi](https://github.com/geerlingguy/time-pi) and the Time
Appliances Project's [Switchberry](https://github.com/Time-Appliances-Project/Switchberry)
were consulted as architectural/recovery references. The clock supervisors are
original MIT code; no code, driver patches or bundled assets from these projects
are copied or redistributed. See [clock ownership and recovery](../../docs/pi5/clock-chain.md).
