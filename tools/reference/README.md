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
