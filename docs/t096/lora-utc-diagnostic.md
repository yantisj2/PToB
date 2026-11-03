# One-request passive LoRa UTC diagnostic

Copyright (c) 2026 Jonathan Yantis. MIT.

A slow RAM transmitter can accept one explicit lab request for an authenticated
META and physical SYNC/FOLLOW pair. This makes missing metadata easier to test
without selecting a repeating fast profile. It needs a current eligible own-GM
or authenticated locked BLE source. Original source intervals, capture identity,
launch/finish bounds, FEM shutdown and channel cooldowns remain enforced. HOLD
cannot supply TX time. Nonpassive RC8 and fast variants return unsupported.

The operator may explicitly permit this job to exceed the ordinary72s/h budget.
Only this requested reservation uses that override. Its full conservative
META+pair cost remains visible in per-boot RAM accounting, including any amount
over the cap; these values are **reserved/charged**, not measured RF-on time.
Subsequent ordinary work still checks the ordinary cap. Unknown/possible RF
keeps its charge; only an exact proven pre-SetTx/no-RF rejection can refund it.
No state or diagnostic mode is saved across reboot.

The worker owns the request. In a safe gap it inserts one extra operation while
preserving the normal slot, next deadline and owed work. Near a genuine due or
owed periodic event, the requested META/pair can coalesce with that event using
the existing delayed-event cadence. Normal60/300s scheduling is retained; no
extra discovery or reception/ACK retry is added. Source/context change, OFF,
profile change or fault expires the request. A pending job must begin a complete
frame before its deadline. Once admitted physically, original physical/source
bounds finish or drain the pair; the request deadline does not cut FOLLOW.

Native receiver training still requires at least180s, four fit points and at
least20s between stored points. Rapid requests cannot replace that baseline or
demonstrate UTC accuracy. Query the receiver's compact status to distinguish
missing fresh UTC from incomplete training or stale display-only HOLD.

## Command and status

Use ordinary exact factory/source/boot R2 binding. `lora-utc` without arguments
only queries status. The single mutation has exactly two arguments:

```text
lora-utc TOKEN16 OPTIONS8
```

`TOKEN16` is a nonzero monotonically increasing64-bit hexadecimal token within
this physical boot. Repeating the current token returns its existing job and
never renews its deadline. A newer request while busy is refused; its token
floor is retained. The helper uses the queried floor plus one unless a token is
explicitly supplied. Exhausted token/admission counters refuse new work.

`OPTIONS8` is32-bit hexadecimal: bit31 allows this job over the airtime cap,
bits0–15 specify a delay in milliseconds from0 through10000, and bits16–30 must
be zero. The slow admission deadline is delay +two2s preparation leads +15s frame +2s slack (at most31s from acceptance). This permits one no-RF-refunded preparation retry; it is one absolute deadline and never renews. The host bound remains40s. There is one
pending/running job, with no host-controlled periodic queue or persistent mode.

Compact JSON exposes the current token/floor, state, reason, last wait reason,
admitted/completed/refused/expired/deferral counters, deadline, override flag,
reserved microseconds, bound source kind/root boot/epoch/capture epoch and
receiver fit count/span/remaining training time. State values are0 idle,
1 pending,2 reserved,3 completed and4 expired. Reason values are0 ready,
1 role/disabled,2 no usable source,3 busy,4 reserved,5 limit,
6 awaiting periodic slot,7 source headroom,8 deadline,9 context changed,
10 OFF/profile/drain,11 scheduling/physical failure,12 ordinary cap and
13 channel cooldown. The terminal reason and retained last wait reason distinguish
an expired request from the condition that prevented its admission.

`clock_reason` is0 selected LoRa UTC,1 no fresh pair,2 incomplete training,
3 another display eligibility condition, or4 display-only HOLD, or5 nominal-rate acquisition UTC estimate. Training fields
describe the receiver's LoRa fit; they are not a GNSS qualification claim for
a transmitter. No independently measured uncertainty is inferred.

## Finite operator helper

Use a reviewed exact-device configuration from the installed image receipt.
Output must be an ignored repository `artifacts/` path. The helper takes the
same unit lock, fresh USB serial/alias ownership checks and exclusive descriptor
as capture. Stop a conflicting capture through its reviewed procedure before
using it. It never opens a port owned by another process or reconnects.

```sh
python3 firmware/t096/tools/lora_utc.py \
  --config artifacts/local/devices.json --unit SOURCE \
  --out artifacts/utc-query-01
```

The default above is read-only. One explicit request additionally requires the
exact current boot and a new output directory:

```sh
python3 firmware/t096/tools/lora_utc.py \
  --config artifacts/local/devices.json --unit SOURCE \
  --expected-boot CURRENT_BOOT16 --request --delay-ms 500 \
  --allow-over-cap --out artifacts/utc-request-01
```

`CURRENT_BOOT16` is a placeholder that fails validation. Omit `--allow-over-cap`
to retain the ordinary cap for the job. The helper spends at most one mutation,
then queries completion within40s total. A missing response is retained and never
resubmitted automatically. Job outcome and descriptor/exclusivity teardown
errors remain separate; failed teardown cannot report a clean completed run.
All raw logs are private with owner-only permissions.

The autonomous soak remains read-only. Job-capable firmware requires a captured
matching TX and excludes manual influence before starting a new ordinary
600s/three-META qualification window. See the [soak guide](lora-soak.md).
