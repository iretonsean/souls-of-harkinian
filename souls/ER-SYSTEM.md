# Elden Ring system for Souls of Harkinian

Status: approved design (prototype reviewed 2026-11-24). Build order: phase 1 + phase 2 ship together, then phase 3.
Players keep today's controls until all of it is done.

Rule for every part: reuse OoT's own sprites, item icons, fonts and drawing code; draw nothing new unless no game art
exists. No text on the play HUD.

## 1. Buttons in play (PlayStation names)

| Button | Action |
|---|---|
| R1 | Right-hand armament (any armament can be in either hand). Sword = swing (B); hold = charged spin attack. Slingshot, Bow, Hookshot, Longshot, Boomerang, Hammer, Deku Stick = that hand's C button (hold to aim, release to fire). Catalyst = cast the current spell |
| L1 | The same for the left-hand armament (C-down for a non-sword item, B for a sword) |
| R2 | With a sword in the right hand: tap = strong attack (OoT jump attack), hold = charged spin attack. Otherwise the same as R1 |
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
| Options | Pause menu |
| Touchpad | Toggle the minimap on / off (OoT's L button; Elden Ring's touchpad is the map). When shown, the minimap sits above the runes at the HUD margin |

The A, B and C buttons are no longer used for items in play. Menus keep the existing Souls menu layout.

## 2. Slots (saved per file, save section "soulsLoadout")

| Kind | Slots | Items |
|---|---|---|
| Right hand | 3 | Any armament: Kokiri Sword, Master Sword, Giant's Knife, Biggoron Sword, Megaton Hammer, Deku Stick, Fairy Slingshot, Fairy Bow, Hookshot, Longshot, Boomerang, catalyst ("Fairy Magic") (later: the 13 pack swords) |
| Left hand | 3 | The same list as the right hand |
| Spells | 3 | Din's Fire, Farore's Wind, Nayru's Love. Cast only while the catalyst is the current item of a hand |
| Arrows | 2 | Fire, Ice, Light Arrows (used by the bow) |
| Shield / Tunic / Boots | 1 each | OoT's equipment, chosen on the Equipment page (the shield is always on Link's arm, guarded with L2) |
| Quick items | 8 | Consumables: Deku Nuts, Bombs, Bombchus, every bottle content, Empty Bottle, plus Epona |
| Pouch | 4 | Tools: both ocarinas, Lens of Truth, Iron / Hover Boots (toggle), Magic Beans, all masks, all trade items |

- Lists only offer items the player owns and can use at the current age (game logic decides).
- An item can sit in one slot of its kind only. The two hands count as one kind for this: an armament is in one hand
  at a time (the Equipment page refuses it in the other hand, see section 4).
- Two swords, one per hand: the game still has one sword on B. Pressing a hand's button with the other hand's sword
  on B puts a drawn sword away, switches the sword, and then the swing goes out once (the press is queued, not lost).
  While guarding (L2), an attack with either hand's sword lowers the shield first.
- Switching with an item out: if the old item is in Link's hand (sword drawn, Deku Stick, Hammer) and the player
  switches it with the D-pad, or a hand button switches the sword, the new sword, stick or hammer comes out ready
  (the game's own item change, no swing and no separate draw press). Switching to a ranged tool or the catalyst puts
  the old item away. With nothing out, switching draws nothing.
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
- Right Hand and Left Hand lists both show every armament the player owns. An armament that is in the other hand is
  dimmed and Cross on it is refused (red flash, "In the left hand. Remove it there first."); the compare card compares
  against the weapon in the opened slot.
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

Built (phase 3) notes:
- Pouch slot positions: up = pouch 1, left = 2, right = 3, down = 4 (the slots the HUD shows while Triangle is held).
  Triangle still fires A on press; the first pouch D-pad press of a hold ends the A press. A pouch item is pressed on C-left
  for three game frames (the game's own item code runs); a lens, mask or held item stays on a free C button while in use,
  because the game drops it when it is on no C button. Iron and Hover boots toggle directly.
- Ocarina out: Cross = A, D-pad = C notes, Circle = B. Triangle does nothing (it is often still held from the pouch press).
  Buttons still held when the ocarina came out are not notes until released.
- Epona: Square takes the ocarina out and presses C-up, C-left, C-right twice; Circle or R1 cancels. Her icon is drawn live
  from her skin model (SoulsEpona.cpp) over the icon box, with no depth buffer.

