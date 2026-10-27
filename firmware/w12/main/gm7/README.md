# GM7 receiver control codec snapshot

Copyright (c) 2026 Jonathan Yantis. MIT.

This directory contains the selected GM7 revision-3 codec used by the W12 and
N30 successor-routing consumer. It is a frozen control-only selection from the same repository's
T096 implementation-3 working sources selected on 26 October 2026, based on
commit `ea70b7f5b5bd6e57ceef21b45736fcd806be9059` with the then-uncommitted
revision-3 changes. The upstream selected hashes and resulting control-subset bytes are recorded in
[snapshot.json](snapshot.json). No private history or hardware records are imported.

ROLE/GNSS size, validation and field branches are omitted; control field offsets,
validation and MAC semantics are unchanged. The codec uses the existing public G1 group-auth API and pinned TinyCrypt
dependencies. Revision 2 and the earlier challenge/ANCHOR protocol are not
accepted by this snapshot. It supplies structural/MAC validation only; current
source scope, original age, replay and nomination checks belong to
[the receiver policy](../g1_gm_successor.hpp).

Shared native integration is enabled explicitly by W12 and N30 CMake targets.
Other derived targets retain their existing consumer. Updating this snapshot
requires an explicit wire compatibility review and the successor regressions;
it does not follow unrelated T096 source changes automatically.
