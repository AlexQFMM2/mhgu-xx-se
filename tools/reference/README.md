# Save-data crosswalk

`mhxx_save_data_crosswalk.json` contains reviewed relationships between MHGU
on-disk IDs and MHXX Dex rows, plus Palico legality facts that are not present
in Dex.

The crosswalk was generated with `import_reference.py` and checked against
`Gicotto/Cotto-MHGU-Editor` commit
`3e2f9ba421eb4c811003d418f3d489be7bf64bcb`. That project in turn attributes
the original save-ID tables to the GPL-3.0 `mineminemine/MHXXSaveEditor`.
Neither reference source tree nor executable is vendored here. The generated
crosswalk records its exact input hashes so it can be audited and regenerated.

Regeneration is a research/maintenance operation; ordinary data builds consume
the committed crosswalk directly.

Charm skill IDs and legal ranges no longer come from a hand-maintained
reference table. `tools/export_game_names.py` reads the 206-entry save array
from the native `skillTypeData.skt` / `skillTypeData_jpn.gmd` pair and both
skill-position ranges for all four grades from `amuletSkillData00..07.amskl`.
Dex adds localized names only through exact Japanese-name matching; its
`SklTree_ID` is never written to the save.
