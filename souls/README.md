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

## Install the preset

1. Copy `presets/Souls of Harkinian.json` into the `presets` folder next to your `shipofharkinian.json`
   (create the folder if it doesn't exist).
2. In the game, open the menu, go to Settings > Presets, and apply "Souls of Harkinian".

The preset also turns on the creator's gameplay settings (enemy health bars, improved roll, faster text,
free camera and more). `make_preset.py` rebuilds it from any `shipofharkinian.json`.

## Code

- `soh/soh/Enhancements/controls/SoulsInput.cpp`: the layout switch and the jump.
- `soh/src/code/padmgr.c`: one call into it where the game reads the controller.
