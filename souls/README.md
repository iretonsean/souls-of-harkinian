# Souls of Harkinian

A fork of [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) 9.3.1 with souls-style controls.
Everything is off until you apply the preset or turn on `gEnhancements.SoulsControls`.

## What it changes

- **Separate menu and play layouts.** In play, the D-pad is the C buttons (items, Navi, ocarina notes).
  In menus (pause screen, file select, text boxes), the D-pad moves the cursor, Cross confirms and Circle
  goes back. To put an item on a C button in the pause screen, hold R1 and press that D-pad direction.
- **Built-in jump on Cross.** Cross starts Link's own ledge jump (the running jump when he runs forward, the
  standing jump otherwise). No item is involved and nothing shows on the HUD. One jump until Link lands; no
  jump while he carries something, swims, climbs or during cutscenes.

- **Character creator.** Starting a new Vanilla or Master Quest file opens "Create Character" before the name entry:
  difficulty path, tunic, hair, boots and Navi colors, body size and sword blades, with a live preview of the real child
  and adult Link (L1 switches). Turn it off with `gEnhancements.SoulsCreator = 0`.
- **Each save file keeps its own look and path** (colors, body size, difficulty), stored in the save.

## Layout (PlayStation names)

| Button | Play | Menus |
|---|---|---|
| Cross | Jump | Confirm |
| Circle | Roll; hold to sprint | Back |
| Triangle | Interact | Confirm |
| Square | Item (C-left) | Put item on C-left |
| D-pad | C buttons | Move cursor |
| R1 / R2 | Sword | R1 + D-pad: put item on that C button |
| L1 / L3 | Shield | Next page |
| L2 / R3 | Lock-on | Previous page |
| Options | Pause | Close |

## Install

1. **The game.** Souls of Harkinian is a build of Ship of Harkinian 9.3.1. Set it up like SoH: it needs your own
   Ocarina of Time ROM to create `oot.o2r` (and `oot-mq.o2r` for Master Quest). Nothing from the game is included here.
2. **The build.** Linux, Windows and macOS builds come from this repository's GitHub Actions
   ("generate-builds", artifacts `soh-linux`, `soh-windows`; signing in to GitHub is needed to download them).
3. **Start it once and close it**, so it creates its folder with `shipofharkinian.json`.
4. **Run the installer.** It copies the preset and downloads every dependency below:

   ```
   curl -fsSL https://raw.githubusercontent.com/iretonsean/souls-of-harkinian/souls/souls/install.sh | bash -s -- --soh "/path/to/your SoH folder"
   ```

   From a checkout: `bash souls/install.sh --soh "/path/to/your SoH folder"`. Add `--no-skins` to install only the preset.
   It needs `curl`, `unzip` and `python3` (and `7z` for packs shipped as .7z). Windows: run it in Git Bash or WSL.
5. **In the game**, open the menu, go to Settings > Presets and apply "Souls of Harkinian".

The preset also turns on the creator's gameplay settings (enemy health bars, improved roll, faster text, free camera
and more). `make_preset.py` rebuilds it from any `shipofharkinian.json`.

## Dependencies

Third-party packs are **not in this repository**: their authors' licenses don't allow redistribution. The installer
downloads them from GameBanana, the authors' own pages, onto your computer, and writes a `SOURCE.txt` with the
author and link next to each one. The list is `dependencies.json`.

### Weapon skins (installed into `souls-skins/`, not `mods/`)

They are the looks of the custom swords. They stay out of `mods/` on purpose: in `mods/` a pack would replace the
original sword for good. The Souls build loads them under their own names (roadmap step 2); until that ships, they
are downloaded but unused.

| Weapon | Pack | Author |
|---|---|---|
| Deku Splitter | [Longer Kokiri Sword](https://gamebanana.com/mods/664520) | nickelndime246 |
| Cinder Edge | [Artwork Accurate LttP Master Sword 2.0](https://gamebanana.com/mods/703064) | Anthus |
| Gale Saber | [WindWakerSword](https://gamebanana.com/mods/728972) | acidicloki |
| Rimeedge | [Frostblade over Master Sword](https://gamebanana.com/mods/452111) | sitton76 |
| Stormbrand | [Razor Sword Collection](https://gamebanana.com/mods/597197) | Reppan |
| Gravestone | [Durandal over Biggoron's Sword](https://gamebanana.com/mods/594055) | Jameriquiah |
| Sun Scimitar | [Lokomo Sword](https://gamebanana.com/mods/691194) | Jameriquiah |
| Desert Fang | [Golden Claymore over Biggoron's Sword](https://gamebanana.com/mods/442039) | sitton76 |
| Sage's Brand | [Phantom Sword](https://gamebanana.com/mods/689100) | Fado |

### Optional, not installed automatically

- **PlayStation button icons:** [Xbox + PlayStation Style Controls](https://gamebanana.com/mods/462822) by trevvv.
  Its layouts follow the standard OoT controls, not the Souls layout, so some icons won't match. Use it as is or skip it.

## Code

- `soh/soh/Enhancements/controls/SoulsInput.cpp`: the layout switch and the jump.
- `soh/src/code/padmgr.c`: one call into it where the game reads the controller.
- `soh/soh/Enhancements/cosmetics/SoulsCreator.cpp`: the creator and its Link preview; hooks in
  `soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c`.
- `soh/soh/Enhancements/cosmetics/SoulsLook.cpp`: the per-save look and path.
- `souls/install.sh`, `souls/dependencies.json`: installer and the list of third-party packs.
