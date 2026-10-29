# GM7 mesh-birth amendment C1

Copyright (c) 2026 Jonathan Yantis. MIT.

**Approved offline engineering interpretation; not flash approval or hardware
acceptance.** C1 explicitly supersedes only the earlier rule projecting a newly
created birth to a future ACTIVE transition. All frozen predecessor bytes remain
unchanged. The manifest binds implementation2, draft1, scheduler A1 and bootstrap
B1. No field, codec, version, domain, key or packet length changes.

## Logical initiation and physical serving

After independent qualification and winning ordinary initial election, the first
GM samples its current functional UTC while creating its first valid bounded
serving reservation. This is logical mesh initiation. Set provisional birth_us
equal to that actual reservation sent_utc_us; sent_mono_us is its corresponding
actual monotonic sample. Never use boot time, dwell start, source generation,
last PPS alone, an identity hash or scheduled future time. Existing finite UTC
range, leap guard and source-map validity requirements still apply. Unknown
calibrated UTC error remains unknown: functional UTC is not an accuracy claim.

The initial reservation requires controller lead time (currently at least3ms).
Keep its lineage provisional, separately from committed serving state. The future
effective opportunity and its deadline are explicit scheduling state; do not call
owner functions with fabricated future now. First ANNOUNCE retains its original
reservation timestamps, sequence, source permission and TTL through queueing.
Source_valid_us is measured/clipped from the actual original sample, not from
future RF start; queue delay consumes that horizon. Intervening source faults,
competing valid authority or revoked takeover conditions cancel positive work.

Before actual dispatch recheck all authority, source and phase conditions.
Commit successful ACTIVE service only after verified complete first ANNOUNCE
within its reserved RF window and applicable takeover bound. No timing output,
normal client source grant or successful ACTIVE UI state before that commit.
Record actual first-start/final-END separately. The local ACTIVE timer starts
when successful service is enabled at commit, never at reservation; birth remains
the earlier logical initiation sample. This does not guarantee anyone received
that transmission.

## Cancellation, exposure and lineage retention

A reservation cancelled before any lineage-bearing ANNOUNCE could have been
transmitted may discard its provisional birth. Conservatively mark possible
exposure at AUX transmission start, before awaiting END/completion. Primary
ADV_EXT_IND alone carries no GM7 lineage. If exposure status is unknown, treat it
as exposed. Do not wait for successful completion before marking exposure.

After possible exposure, retain birth/founder/term even if completion fails or
its callback is missing. Fence that serving scope and use valid WITHDRAW when
possible; otherwise safe silence. A failed first RF event never becomes a
successful-initiation receipt or ACTIVE duration. A subsequent serving attempt
must follow normal election/authority rules, retain exposed lineage, and advance
the terminal term as required. It may not manufacture a younger birth to erase
the exposed claim. Cancelled sequence/ticket allocations remain consumed.
Retained state is RAM lineage state, not a new flash-restoration contract.

Succession within surviving lineage always preserves its original birth/founder;
only term/leader/source/train ownership changes under existing rules. C1 does
not relax source-map/servo continuity, independent GNSS qualification, replay
floors, oldest-lineage merge or conditional HOT authority.

## Preserved rejected approach and examples

Rejected: project birth to a future nominal RF opportunity while retaining a
truthful earlier sent_utc timestamp. It violates unchanged codec birth<=sent_utc.
Also rejected: future-date sent_utc/sent_mono or pass future now to make that
packet validate. C1 resolves the semantic conflict explicitly, not by weakening
validation. The negative test retains this rejected example.

vectors.json contains two public synthetic174-byte PDUs (178-byte AD framing):
initial reservation has birth=sent_utc=1792800000000000 and actual monotonic
sample10000000; an example nominal opportunity is10010000. The inherited example
preserves birth1792799990000000/founder17 and uses successor identity49/boot65,
term5. Neither vector encodes a successful RF result. Both round-trip through
the unchanged implementation2 codec and public fixture key00..1f, which is never
a deployment key.

## Verification and limits

Run `python3 -B protocol/gm7_birth_c1/verify.py`. Six independent policy tests cover
initial prerequisites, cancellation before exposure, exposed failure retention,
valid/invalid completion, inherited lineage and exact unchanged wire vectors.
The small model represents reservation lifecycle only; production owner/source,
nonce/MAC admission, ticker and output-map tests remain required. It cannot prove
radio delivery, precise output, hardware timing or actual oscillator bounds.
The verifier checks hashes and prior identities and never regenerates examples.
