# GM7 protocol review baseline

**GM7-draft1 — REVIEW CANDIDATE / NOT FIRMWARE CLEARED.**

[Draft 1](draft1/SPEC.md) freezes proposed connectionless ANNOUNCE/STATUS layouts,
synthetic known-answer examples, a one-way WITHDRAW event, and a small offline
reference policy. The frozen [diagnostics/UI migration plan](draft1/INTEGRATION_PLAN.md)
is required future implementation scope. A tiny synthetic schedule model tests
three-copy events, guarded RX/TX windows and fixed deadlines; it is not radio
implementation or a delivery guarantee. It is not
implemented firmware, a current wire profile, or hardware acceptance. It changes
no existing G5, G1, RC7, RC8, client, registry or evidence contract.

Run from the repository root, using Python 3.10+ and its standard library only:

```sh
python3 -B protocol/gm7/draft1/verify.py
```

The verifier checks the manifest, exact example bytes/decoded fields and offline
unit tests. It never rewrites frozen files. To inspect regenerated examples
without changing the freeze:

```sh
python3 -B protocol/gm7/draft1/examples.py
```

All content here, including the synthetic vectors, is original reference code or
ordinary documentation under [MIT](../../LICENSE), copyright (c) 2026 Jonathan
Yantis. No imported evidence, private identities or enrollment is included.
The public HMAC key is deliberately unusable as a deployment secret.
