# R2 controller/PHY simulation

These records describe offline controller/PHY or codec executions. Hardware
accuracy and independent full-suite qualification are not established.

| Record | Recorded outcome and scope |
| --- | --- |
| [Connected coarse control, October 8](V12_R2_CONNECTED_SIM_20261008.json) | Simulator pass, not flashable; stock GATT/PHY with synthetic enrollment, oscillator and GPS/PPS inputs |
| [R2 P1 campaign, October 9](R2_RF_LAB_20261009.json) | **FAILED overall**; normal-service precision-map failures retained separately from passing stall/closure checks |
| [Wire freeze checks, October 9](R2_WIRE_FREEZE_20261009.json) | Codec/vector and provenance checks; no physical precision acceptance |

The connected coarse record retains prior iterations, actual implementation
commit, synthetic metrics, and remaining gates. The P1 record retains normal and
stall populations, setup failures, source bindings and receipt outcomes. A codec
match or successful shutdown case cannot turn the failed service campaign into
a pass. Test enrollment is synthetic, not production provisioning.

The source freeze distinguishes installed 110-byte control from the P1 candidate
with configuration-2 control and a 158-byte precision advertisement. These are
checkpoint-specific descriptions, not a claim about the latest installed build.

The full R2 simulator/build dependencies and traces are not included yet.
Reviewers can inspect and hash these records, but cannot rerun those controller
campaigns from this checkout. A standalone T096 example is a separate migration.
