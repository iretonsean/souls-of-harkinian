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

## 3b. Difficulty that follows weapon power
Goal: stronger swords feel stronger, but bosses never collapse in two hits and a weak sword never makes a fight hopeless.

**Where it hooks.** Every hit Link lands goes through `CollisionCheck_ApplyDamage` (soh/src/code/z_collision_check.c),
which already applies SoH's co-op damage multiplier. One function there computes the final damage. Enemy health and
hit damage are small whole numbers (bosses ~10-30 health, swords 1-4 damage), so fractional damage is kept per enemy
and carried to the next hit instead of being rounded away.

**1. Weapon damage.** Each custom sword behaves like one of the three vanilla sword classes (Kokiri, Master, Biggoron), so
the game's own hit reactions, damage tables and weaknesses still apply. Its attack sets a multiplier over that class:
`weapon factor = sword attack / class attack` (class attack 100 / 200 / 400).

**2. Scaling from progress, not experience points.** The three scaling stats come from things Link already collects:
- Courage: heart containers (3-20)
- Power: strength upgrades (Goron Bracelet, Silver and Golden Gauntlets) and the Biggoron Sword
- Wisdom: magic meter, spells and medallions
A grade (S, A, B, C, D, E) sets how much each stat adds, up to about +40 % attack at full progress for an S grade.

**3. Boss expectation curve.** Each boss has an expected attack for the point in the game where it appears, for example
Gohma 100, King Dodongo 110, Barinade 120, Phantom Ganon 200, Volvagia 220, Morpha 240, Bongo Bongo 260, Twinrova 300,
Ganondorf 320, Ganon 400. Damage against a boss is
`vanilla damage x (your attack / expected attack) ^ 0.6`, clamped to 0.5x-2x.
The 0.6 power gives diminishing returns: twice the expected attack deals about 1.5x damage, not 2x.
Regular enemies use the expectation of the area's dungeon.

**4. Elements.** A small per-boss table: weak (x1.5), normal, resistant (x0.5), immune. Examples: Twinrova's two witches
resist their own element, and the ice witch is weak to fire. Element damage is the elemental part of the sword's attack.

**5. Difficulty path, chosen in the character creator (stored per save file).** Built from SoH options that already exist
plus the expectation curve:
| Path | Damage taken | Bosses | Enemies | Extras |
|---|---|---|---|---|
| Story | x0.5 | expectation lowered 20 % | normal | heart drops on |
| Hero (vanilla) | x1 | curve as listed | normal | - |
| Souls | x2 | curve +25 %, Hyper Bosses | Hyper Enemies | - |
| Master | x4 | curve +50 %, Hyper Bosses | Hyper Enemies | no heart drops |
Damage taken and Hyper Bosses/Enemies are existing SoH settings (Damage Multiplier, Hyper Bosses, Hyper Enemies,
No Heart Drops); the path only sets them.

**6. Show it.** The stat card on pickup shows "Against the next boss: strong / even / weak" from the same formula,
and the enemy health bar (already in SoH) shows the effect live.
