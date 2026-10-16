# T096 local source cutover — 16 October 2026

Copyright (c) 2026 Jonathan Yantis. MIT.

PToB is the source home for normal T096 RC7 development, beginning with the local
0.15.54 migration change. GM and RC are the only public profiles; USB source
streaming is standard. The prior application commit is recorded in
[baseline.json](../../firmware/t096/baseline.json), and the explicit extraction
allowlist, original hashes and transformations are in the
[import receipt](../../firmware/t096/imports/2026-10-16.json).

The former laboratory tree is the historical source reference. Future T096
changes belong here; the laboratory consumes identified PToB commits and returns
reviewed patches and separately curated evidence. Private keys, enrollment,
device inventories and raw captures remain outside public source. W12, SS3 and
macOS client ownership have not moved yet.

This source-authority change is separate from GitHub publication and a public
install-first release. P1 runtime provisioning, generic hardware install/recovery,
clean-host dependency acquisition and wider hardware qualification remain pending.
TC-04 validation is a bounded migration check, not independent UTC metrology.
The later research drafts remain frozen for their separate publication review.
