# Repository guidance

This repository contains curated publication material. Read README.md and the
relevant collection README before changing it. Do not infer current firmware
acceptance or precision qualification from a historical document or simulator.

## Local work

When present, read `.internal/README.md` and `.internal/PLAN.md` for the active
local migration assignment. `.internal/` is ignored private working material:
never stage, force-add, publish, or copy it into public documentation. Its absence
in a fresh clone is expected. Keep private keys, enrollment, raw lab logs, and
unreviewed imports out of tracked files.

## Publication discipline

- Import explicitly selected files with recorded provenance; no wholesale sync
  from a private workspace and no private Git history import.
- Keep paper editions, wire profiles, source versions, and evidence populations
  distinct. Preserve failures, missing data, denominators, and limitations.
- Separate model, controller simulation, hardware observation, relative timing,
  and independently measured output. Checksums establish identity, not accuracy.
- Use public, obtainable dependencies and reproducible commands. Do not require
  a sibling private checkout or private credentials for ordinary verification.
- Licensing rule: original code and ordinary repository documentation use MIT;
  original papers (PDF and LaTeX) and their figures use CC BY 4.0. Credit Jonathan
  Yantis. Original imported evidence records and derived CSV datasets use CC BY
  4.0; snapshot code uses MIT. Preserve third-party terms. See LICENSING.md.
  Record any future dataset license explicitly.
- Papers remain drafts until explicitly promoted. Develop their sources here,
  preserve prior editions in Git, and commit source/PDF updates with matching
  manifests and a changelog entry. Never silently overwrite a reviewed edition.
- Use evidence/README.md to select the appropriate historical collection. Frozen
  model documents and historical experiment records are context, not current
  operating instructions or hardware authorization. Do not rewrite their hashes.
- Validate changes with relevant checks; document checks that cannot yet run.
- Do not push, publish releases, deploy, operate hardware, or alter the private
  laboratory as part of repository preparation unless separately authorized.
