# R2 T096 hardware campaign records

Selected dated observations from the TC-01/TC-03/TC-02 cohort are retained here.
Each record identifies its candidate, image/source, checks, and unresolved
outcomes. This is experiment history, not a live status page or current release
acceptance. Dates in filenames do not order every internal follow-up.

| Record | Recorded outcome |
| --- | --- |
| [Clock retest](V12_R2_CLOCK_RETEST_20261008.json) | Repeated three-unit startup passed; GNSS input issue retained; precision UNKNOWN |
| [UI installation](V12_R2_UI_20261008.json) | Three-unit UI checks passed; TC-03 command rejection retained |
| [GM and followers](V12_R2_GM_20261008.json) | Finite GM/two-follower and diagnostic checks passed; continuity remained open |
| [Follower restoration](V12_R2_FOLLOWER_RESTORE_20261008.json) | Installed pair UI passed; service gaps and TC-03 startup uncertainty retained |
| [Precision restoration hold](V12_R2_PRECISION_RESTORE_20261008.json) | Flash held for overnight-freeze investigation; a prepared candidate is not an installed result |
| [Standalone GM](V12_R2_STANDALONE_GM_20261009.json) | Three devices installed and bounded service passed; relative precision capture/correction/RMS integration remained outstanding in this record |

Some records include supporting host tests, simulator checks, and prepared
candidates. Those sections retain their original evidence types and must not be
counted as physical observations. Finite boot/UI/service checks do not establish
continuous reliability, independently measured output, or absolute UTC accuracy.
Later documents or firmware work cannot retroactively clear a recorded failure.

Private silicon identifiers and local checkout prefixes were removed, with every
change recorded in the [import manifest](../../../imports/2026-10-09.json).
Unit aliases, source/image hashes, outcomes, and limitations remain. Raw device
captures, rollback binaries, private enrollment, and deployment tools are excluded.
Historical archive paths are locators only. No hardware was accessed for this import.
