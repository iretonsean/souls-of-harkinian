# Roadmap

Order of work. Each step is prototyped first, then built.

## Done
- Separate menu and play layouts for the D-pad (`SoulsInput.cpp`).
- Built-in jump on Cross (Roc's Feather jump, no item on the HUD).
- "Souls of Harkinian" preset with the creator's settings.

## 1. Character creator (prototype done)
- Opens instead of OoT's name screen when a new file starts; also reachable for an existing file.
- Rows: Name, Base Model, Hair, Tunics, Boots & Gauntlets, Body, Sword & Shield colors, Navi, Equipment, Randomize, Done.
- Uses OoT's own file-select sprites, message font and the real child/adult Link model for the preview
  (drawn by the game like the pause screen's equipment page).
- The look (cosmetics, body size, model pack choice) is saved per save file instead of globally.

## 2. Equipment skins from installed packs
- At startup, scan the `mods` folder: for each `.o2r`/`.otr`, note which Link equipment models it replaces
  (Kokiri/Master/Biggoron Sword, Deku/Hylian/Mirror Shield, whole gear sets).
- Register each as a selectable skin under its own name instead of letting the pack override the original.
- The creator's Equipment section picks a skin per item; the player draw code uses the chosen skin.
- Packs are never bundled: most GameBanana packs do not allow redistribution. Players install their own.

## 3. Custom swords with stats, found in chests
- New swords as custom items, built the same way as SoH's Roc's Feather (item id, icon, name, get-item text).
- Per-sword stats: attack (physical + element), scaling on Courage / Power / Wisdom, weight, reach, speed,
  passive effect. OoT's base damage (Kokiri 1, Master 2, Biggoron 4) maps to attack 100 / 200 / 400.
- Stat card on pickup and in the pause screen, compared with the equipped sword.
- Placement: existing chests through the randomizer's item placement, or new chests (see the Unbound fork's
  actor and scene system).
- All weapons, names and numbers are original. Nothing is copied from other games.
