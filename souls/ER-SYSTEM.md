# Elden Ring system for Souls of Harkinian

Status: approved design (prototype reviewed 2026-11-24). Build order: phase 1 + phase 2 ship together, then phase 3.
Players keep today's controls until all of it is done.

Rule for every part: reuse OoT's own sprites, item icons, fonts and drawing code; draw nothing new unless no game art
exists. No text on the play HUD.

## 1. Buttons in play (PlayStation names)

| Button | Action |
|---|---|
| R1 / R2 | Right-hand armament: attack / strong attack (OoT jump attack). Hold R2 = charged spin attack |
| L1 | Left-hand item: Slingshot, Bow, Hookshot, Longshot, Boomerang = hold to aim, release to fire. Catalyst = cast the current spell |
| L2 | Guard with the equipped shield (OoT's R). Always available, as in OoT; not with a two-handed sword |
| D-pad right / left | Switch right / left hand to its next filled slot |
| D-pad up | Switch the current spell |
| D-pad down | Switch the current quick item |
| Square | Use the current quick item |
| Triangle | Interact (OoT's A-button actions: talk, open, check, climb, grab, throw) |
| Triangle + D-pad | Use that pouch slot (up, left, right, down = pouch 1-4) |
| Circle | Roll / backstep; hold = sprint (exists) |
| Cross | Jump (exists) |
| R3 | Lock-on (Z-target) |
| L3 | First-person look (OoT's C-up) |
| Create | Navi |
| Options / touchpad | Pause menu / map page |

The A, B and C buttons are no longer used for items in play. Menus keep the existing Souls menu layout.

## 2. Slots (saved per file, save section "soulsLoadout")

| Kind | Slots | Items |
|---|---|---|
| Right hand | 3 | Kokiri Sword, Master Sword, Giant's Knife, Biggoron Sword, Broken Knife, Megaton Hammer, Deku Stick (later: the 13 pack swords) |
| Left hand | 3 | Fairy Slingshot, Fairy Bow, Hookshot, Longshot, Boomerang, catalyst ("Fairy Magic") |
| Spells | 3 | Din's Fire, Farore's Wind, Nayru's Love. Cast only while the catalyst is the current item of a hand |
| Arrows | 2 | Fire, Ice, Light Arrows (used by the bow) |
| Shield / Tunic / Boots | 1 each | OoT's equipment, chosen on the Equipment page (the shield is always on Link's arm, guarded with L2) |
| Quick items | 8 | Consumables: Deku Nuts, Bombs, Bombchus, every bottle content, Empty Bottle, plus Epona |
| Pouch | 4 | Tools: both ocarinas, Lens of Truth, Iron / Hover Boots (toggle), Magic Beans, all masks, all trade items |

- Lists only offer items the player owns and can use at the current age (game logic decides).
- An item can sit in one slot of its kind only.
- A two-handed weapon (Giant's Knife, Biggoron Sword) in the right hand puts the shield on the back, as in OoT.
- Old saves: on the first load, the slots are filled once from what the player has equipped (sword, C items; the shield
  stays the equipped shield), so
  nobody starts unarmed. After that only the player changes them.

## 3. HUD (phase 1)

All positions in N64 screen units, anchored to the screen edges with one margin of 12 on every side. No text anywhere.

- Top left: the carved stone heart from the Quest Status page (22 wide, rounded corners, thin white frame like the magic
  meter's). Collected heart pieces fill its hollow with OoT's own piece sprites in heart red (255,70,50); on the 4th the
  whole heart lights up, then the hollow empties and the HP meter grows by one heart.
- Right of it, 6 apart, two meters of the same height drawn with OoT's magic meter sprites (scale 0.68), centred on the
  stone: HP (heart red, length 14 + 7 per heart) and magic (OoT green 0,200,0; 48 single / 96 double). A meter that drops
  shows the lost part in yellow (250,250,0) for a moment. No numbers, no stamina.
- Top right: the collected Spiritual Stones (child) or Medallions (adult), only the collected ones, filling from the right
  in collection order; icons 11.25 wide, gap 2.25; the visible art sits on the top and right margins. Medallions: no rim,
  a two-layer hard black shadow (100 % at 1.8 / 1.6 icon-px, 90 % at 1.0 / 1.0) to match the shadow drawn into the stone
  icons.
- Bottom left: the item cross with Elden Ring proportions: four borderless soft dark panels, 19.7 wide x 23.75 tall,
  gap 3.5; spell on top, quick item at the bottom (on the bottom margin), left hand and right hand straddling the gap
  between them; item icons at 84 % of the slot width, ammo counts on the icon. Small preview panels (0.45 of the height,
  80 % opacity) show the next spell (right of the spell slot, top-aligned) and the next quick item (right of the
  quick-item slot, bottom-aligned) when there are 2 or more. A used or switched slot darkens with a soft warm inner glow.
  The spell slot is dimmed without a catalyst in hand. Holding Triangle swaps the four slots to the pouch items (no mark).
- Bottom right: rupees like Elden Ring's runes (soft dark band, rupee icon, number). The dungeon map sits above it at the
  same margin.
- Triangle actions show as a small prompt near the bottom centre (a triangle mark + OoT's own action word texture),
  replacing OoT's A-button text.
- Hidden: A, B, C buttons, the D-pad item display, OoT's hearts row, magic meter and rupee counter.

## 4. Equipment page (phase 2)

Replaces OoT's equipment subscreen in the pause menu. Elden Ring layout, OoT look.

- Rows: Armaments (right hand 1-3, left hand 1-3), Magic & Gear (spells 1-3, arrows 1-2, shield, tunic, boots), Quick Items 1-8,
  Pouch 1-4. Right panel: Link and his status.
- Selected slot: steady gold frame with a glow (no pulsing).
- Cross on a slot opens the list of that kind: "pick up to N". Equipped items show a gold fill and their slot number;
  a counter "n / N" turns red when full; when full, the rest are dimmed and Cross on them is refused (red flash,
  "All N slots are full. Remove one first."). Cross on an equipped item removes it. A new item goes into the opened slot
  if it is empty, else the first free slot. Shield, tunic and boots are choose-one. Triangle on a slot empties it; Circle = done.
- Weapons show the souls-style compare card: every stat as equipped value > new value, green when better, red when
  worse; the first column's header is the equipped weapon's name.

## 5. Pouch, ocarina, Epona (phase 3)

- Pouch: Triangle + D-pad uses pouch 1-4 at once (no cycling).
- Ocarina: while it is out, Cross plays the A note and D-pad up / down / left / right play the C notes; Circle puts it away.
- Epona: a quick item once Epona and her song are obtained. Square plays Epona's Song for the player (the song's notes
  appear on the staff) and she arrives as usual. No text. Her slot icon is her head, drawn live from her 3D model, the way
  the character creator draws Link.
