# Roadmap

Order of work. Each step is prototyped first, then built.

**Next: the Elden Ring system** (`ER-SYSTEM.md`): phase 1 controls + slots + HUD and phase 2 Equipment page ship
together, then phase 3 pouch, ocarina, Epona. Steps 2 and 3 below come after it (the 13 swords become right-hand
armaments in the new slots).

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
  DONE as the data layer: `soh/soh/Enhancements/cosmetics/SoulsLook.cpp` ("soulsLook" save section,
  `gEnhancements.SoulsPerFileLook`).

Build plan:
- Entry: `soh/src/overlays/gamestates/ovl_file_choose/z_file_nameset_NES.c`, `FileChoose_StartNameEntryNES`
  (sets `configMode = CM_NAME_ENTRY` for a new file). Add a creator mode before it (`CM_SOULS_CREATOR`), reached the same
  way; the Name row opens OoT's own name entry and returns to the creator. An existing file gets "Change look" on its menu.
- Drawing: reuse the file-select helpers in `z_file_choose.c` (window tiles `gFileSelWindow*`, highlight
  `gFileSelBigButtonHighlightTex`, name box `gFileSelNameBoxTex`, message font) so it matches the game exactly.
- Preview: `Player_DrawPauseImpl` (soh/src/code/z_player_lib.c) draws the real Link with sword, tunic, shield and boots
  as parameters. It takes a PlayState only for `state.gfxCtx` and `view.eye`; add a variant that takes a GraphicsContext and
  an eye vector, and load the child/adult Link object and a SkelAnime the way `KaleidoScope` does for the equipment page.
- Values: rows write the same cosmetics settings the Cosmetics Editor uses (`gCosmetics.Link.*`, swords, Navi...),
  then `ApplyOrResetCustomGfxPatches()`, so the preview and the game always agree; Done saves them with the file.

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
- Stat card on pickup and in the pause screen, compared with the equipped sword the souls way: every stat shows
  the equipped value > the new value, green when better, red when worse. The first column's header is the
  equipped weapon's name (wrapped to two lines when long). Shown in the prototype's chest demo.
- Placement: existing chests through the randomizer's item placement, or new chests (see the Unbound fork's
  actor and scene system).
- All weapons, names and numbers are original. Nothing is copied from other games.

Placement (approved). Source of truth: `map/hyrule.json`, drawn by `map/make_map.py` as
`map/hyrule-chests.png`. Swords use their creators' names. Chests that hold rupees are swapped first; in a dungeon the
exact chest is picked when the build gets there, never one that holds a key or an item the route needs.
The two Biggoron-class swords (Durandal, Golden Claymore) sit late in the adult route so the strongest attack comes last.

| # | Sword | Creator | Age | Class | Chest |
|---|---|---|---|---|---|
| 1 | Longer Kokiri Sword | nickelndime246 | child | Kokiri | Lost Woods grotto (rupee chest) |
| 2 | Razor Sword | Reppan | child | Kokiri | Dodongo's Cavern (bomb flower platform chest) |
| 3 | Phantom Sword | Fado | child | Kokiri | Kakariko open grotto (rupee chest) |
| 4 | Master Sword (LttP) | Anthus | adult | Master | Forest Temple |
| 5 | Phantom Sword | Fado | adult | Master | Death Mountain Trail storms grotto (rupee chest) |
| 6 | Razor Sword | Reppan | adult | Master | Fire Temple |
| 7 | Frostblade | sitton76 | adult | Master | Ice Cavern |
| 8 | WindWakerSword | acidicloki | adult | Master | Water Temple |
| 9 | Durandal | Jameriquiah | adult | Biggoron | Shadow Temple |
| 10 | Lokomo Sword | Jameriquiah | adult | Master | Spirit Temple |
| 11 | Golden Claymore | sitton76 | adult | Biggoron | Gerudo Training Ground |
| 12 | Tempered Sword | Anthus | adult | Master | Ganon's Castle, Light trial |
| 13 | Golden Sword | Anthus | adult | Master | Ganon's Castle, Spirit trial |

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
| Master | x2 | curve +25 %, Hyper Bosses | Hyper Enemies | - |
| Souls (hardest) | x4 | curve +50 %, Hyper Bosses | Hyper Enemies | no heart drops |
Damage taken and Hyper Bosses/Enemies are existing SoH settings (Damage Multiplier, Hyper Bosses, Hyper Enemies,
No Heart Drops); the path only sets them.

**6. Show it.** The stat card on pickup shows "Against the next boss: strong / even / weak" from the same formula,
and the enemy health bar (already in SoH) shows the effect live.
