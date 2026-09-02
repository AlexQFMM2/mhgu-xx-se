# Attribution and research sources

This project is licensed under GPL-3.0-or-later.

- The Qt/Fusion UI structure and styling were adapted from the GPL-3.0
  `mh3u-se` / MH3U Save Editor project in the same workspace. Its copyright
  notices and GPL terms continue to apply to the adapted UI work.
- MHGU save offsets and behavior were independently implemented here after
  cross-checking the GPL-3.0 projects `Dawnshifter/MHXXSwitchSaveEditor` and
  `Gicotto/Cotto-MHGU-Editor`. Their source trees remain under the workspace
  research directory and are not vendored into this repository.
- Game-name CSV files are deterministically generated from the user's pinned
  MHXX Dex executable and reviewed save-ID crosswalks. See `data/README.md` for
  hashes and source details.
- Palico Chinese translations include reviewed community material attributed
  in `tools/reference/palico_cn_translation.json`.
- Armor statistics, resistances and skill contributions are imported from the
  MIT-licensed `JoeLago/MHGUDB-iOS` database at commit
  `a91a02bc076883df671c2a4fff3ac53be7d710a8`; its database SHA-256 and ID
  cross-checks are pinned in the data manifest.
- Talisman slot-generation thresholds and their fulfillment formula are from
  the MIT-licensed `AthenaADP/MHGU-ASS` at commit
  `ae7f125d1d381278d15908d7ce5b092076423918`. They are combined with the
  independently extracted native RomFS skill ranges; unknown combinations are
  rejected rather than approximated.

Monster Hunter, MHGU and MHXX are trademarks of Capcom. This is an unofficial
fan tool and is not affiliated with or endorsed by Capcom.
