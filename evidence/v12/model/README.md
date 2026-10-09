# v12 numerical model — synthetic evidence

The need for precision through passive reception drove v12 toward periodic
advertising and informed the T096 R2 implementation. Read the
[design background](../../../docs/V12_TO_R2.md) for that progression and the
planned split between mesh services and phone/OS client services.

This is the frozen **October 6 numerical proposal model**, not a firmware or
Bluetooth-controller emulator. It uses declared synthetic clocks, loss, timing,
source readiness, and scheduling. Its profiles are not current R2 profiles.

[summary.json](summary.json) preserves separate treatment groups, denominators,
soaks, source hashes, and six [experiment receipts](receipts/). The recorded
campaign includes 1,523 sweep cases, nine intended bound counterexamples, and
**108 combined-fault listener numeric failures**. Zero unexpected safety failures
is not a claim of precision or availability under every fault. Missing independent
precision remains null/UNKNOWN; PTS and WiFi remain off in this model record.

The nominal and prescribed 10% loss treatments retain their original windows,
paths, seeds, missed seconds, and unavailable grid cells. Synthetic nanosecond
statistics must not be presented as physical measurements.

## Frozen source and reproduction

[snapshot/](snapshot/) contains exactly the eleven source/configuration files
hashed by the recorded summary, recovered from the frozen proof experiment.
The private workspace's later model files had changed; they were not substituted.
The snapshot's historical rule document is experiment context, not current
hardware instructions or permission to operate devices.

With Python 3.9 or newer, from this directory:

```sh
cd snapshot
python3 -m unittest discover -s tests -p test_v12_model.py -v
```

All **50 tests passed** during this import. The model uses the Python standard
library. A short new synthetic run can be created from `snapshot/` with:

```sh
python3 tools/v12_sim.py run --config config/v12_four_unit_mesh_profile.json --set seconds=30 --out /tmp/ptob-v12-model-review-NEW_ID
```

Use a fresh output directory. A short run does not reproduce the historical
full sweep or long soaks. Full campaign outputs/logs and the older V11 regression
workspace are not included; the retained receipts and summary are historical
records, not newly rerun campaigns. Repository hash verification needs no private
checkout. Snapshot code and ordinary documentation use MIT; authored result
records use CC BY 4.0.
