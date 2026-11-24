// Souls of Harkinian: the Equipment page of the Elden Ring system (souls/ER-SYSTEM.md, section 4).
//
// With the Souls controls on, this page replaces OoT's equipment grid in the pause menu. OoT keeps the pause frame, the
// background and the Link preview (drawn here from the same frame buffer the old page used). The slots, the lists and
// the weapon compare card are drawn in 2D over the pause screen with the game's own font, in the layout of the approved
// web prototype (rows of slots on the left; Link and his status on the right; a list with a compare card on Cross).
//
// What it edits: the slots of SoulsLoadout (right hand, left hand, spells, arrows, quick items, pouch) and OoT's own
// tunic and boots (set the way OoT's equipment page sets them). The input layer reads the loadout in play.
// Buttons (menu layout of SoulsInput): D-pad / stick move, Cross = Equip / open, Triangle = Remove, Circle = Back.
#include <libultraship/bridge/consolevariablebridge.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "soh/Enhancements/controls/SoulsEquipPage.h"
#include "soh/Enhancements/controls/SoulsLoadout.h"
#include "soh/ShipUtils.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/frame_interpolation.h"
extern "C" {
#include "z64.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "regs.h"
#include "textures/icon_item_24_static/icon_item_24_static.h"
#include "textures/parameter_static/parameter_static.h"
#include "textures/title_static/title_static.h"
extern u8 gEquipAgeReqs[][4];
int16_t OTRGetRectDimensionFromLeftEdge(float v);
int16_t OTRGetRectDimensionFromRightEdge(float v);
extern uint16_t gSoulsRawPadButtons; // SoulsInput.cpp: the pad buttons before the Souls layout changes them
void KaleidoScope_DrawPauseLink2D(PlayState* play, s32 x, s32 y, s32 w, s32 h); // z_kaleido_equipment.c
}

namespace {

// ---------------------------------------------------------------------------------------------------------------
// Data

enum { K_TUNIC = 100, K_BOOTS = 101, K_SHIELD = 102 }; // slot kinds that are OoT equipment, not loadout kinds

struct SlotDef {
    int kind; // SL_* or K_TUNIC / K_BOOTS; -1 = gap
    int idx;
};
struct RowDef {
    const char* label;
    int count;
    SlotDef s[10];
};
const RowDef kRows[4] = {
    { "Armaments",
      7,
      { { SL_RIGHT, 0 }, { SL_RIGHT, 1 }, { SL_RIGHT, 2 }, { -1, 0 }, { SL_LEFT, 0 }, { SL_LEFT, 1 }, { SL_LEFT, 2 } } },
    { "Magic & Gear",
      10,
      { { SL_SPELL, 0 },
        { SL_SPELL, 1 },
        { SL_SPELL, 2 },
        { -1, 0 },
        { SL_ARROW, 0 },
        { SL_ARROW, 1 },
        { -1, 0 },
        { K_SHIELD, 0 },
        { K_TUNIC, 0 },
        { K_BOOTS, 0 } } },
    { "Quick Items",
      8,
      { { SL_QUICK, 0 },
        { SL_QUICK, 1 },
        { SL_QUICK, 2 },
        { SL_QUICK, 3 },
        { SL_QUICK, 4 },
        { SL_QUICK, 5 },
        { SL_QUICK, 6 },
        { SL_QUICK, 7 } } },
    { "Pouch", 4, { { SL_POUCH, 0 }, { SL_POUCH, 1 }, { SL_POUCH, 2 }, { SL_POUCH, 3 } } },
};

struct ItemInfo {
    uint8_t id;
    const char* name;
    const char* desc;
};
const ItemInfo kItems[] = {
    { ITEM_SWORD_KOKIRI, "Kokiri Sword", "Child. The Kokiri's treasured blade." },
    { ITEM_SWORD_MASTER, "Master Sword", "Adult. The blade of evil's bane." },
    { ITEM_SWORD_BGS, "Biggoron Sword", "Adult. Two hands." },
    { ITEM_SWORD_KNIFE, "Broken Knife", "Adult. What is left of the knife." },
    { ITEM_HAMMER, "Megaton Hammer", "Adult. Breaks rocks, rusted switches." },
    { ITEM_STICK, "Deku Stick", "Child. Breaks after a few hits. Can carry fire." },
    { ITEM_SHIELD_DEKU, "Deku Shield", "Child. Wooden. Burns." },
    { ITEM_SHIELD_HYLIAN, "Hylian Shield", "Blocks most attacks." },
    { ITEM_SHIELD_MIRROR, "Mirror Shield", "Adult. Reflects light and magic." },
    { ITEM_SLINGSHOT, "Fairy Slingshot", "Child. Hold L1 to aim, release to fire." },
    { ITEM_BOW, "Fairy Bow", "Adult. Hold L1 to aim, release to fire." },
    { ITEM_HOOKSHOT, "Hookshot", "Adult. Pulls you to targets." },
    { ITEM_LONGSHOT, "Longshot", "Adult. A longer Hookshot." },
    { ITEM_BOOMERANG, "Boomerang", "Child. Stuns enemies, grabs items." },
    { SOULS_ITEM_CATALYST, "Fairy Magic", "Catalyst. In a hand, that hand casts the current spell." },
    { ITEM_DINS_FIRE, "Din's Fire", "Spell. A wall of fire around you." },
    { ITEM_FARORES_WIND, "Farore's Wind", "Spell. Warp point in a dungeon." },
    { ITEM_NAYRUS_LOVE, "Nayru's Love", "Spell. A barrier against damage." },
    { ITEM_ARROW_FIRE, "Fire Arrow", "Adult. Uses magic." },
    { ITEM_ARROW_ICE, "Ice Arrow", "Adult. Uses magic." },
    { ITEM_ARROW_LIGHT, "Light Arrow", "Adult. Uses magic." },
    { ITEM_TUNIC_KOKIRI, "Kokiri Tunic", "Your green tunic." },
    { ITEM_TUNIC_GORON, "Goron Tunic", "Adult. Resists heat." },
    { ITEM_TUNIC_ZORA, "Zora Tunic", "Adult. Breathe under water." },
    { ITEM_BOOTS_KOKIRI, "Kokiri Boots", "Plain boots." },
    { ITEM_BOOTS_IRON, "Iron Boots", "Adult. Sink in water. Pouch them for the Water Temple." },
    { ITEM_BOOTS_HOVER, "Hover Boots", "Adult. Walk on air for a moment." },
    { ITEM_NUT, "Deku Nut", "Stuns with a flash." },
    { ITEM_BOMB, "Bomb", "Lights and blows up." },
    { ITEM_BOMBCHU, "Bombchu", "Runs along walls." },
    { ITEM_BOTTLE, "Empty Bottle", "Catch something." },
    { ITEM_POTION_RED, "Red Potion", "Restores your hearts." },
    { ITEM_POTION_GREEN, "Green Potion", "Restores your magic." },
    { ITEM_POTION_BLUE, "Blue Potion", "Restores hearts and magic." },
    { ITEM_FAIRY, "Bottled Fairy", "Heals you, or saves you when you fall." },
    { ITEM_FISH, "Fish", "Feed it to Jabu-Jabu." },
    { ITEM_MILK_BOTTLE, "Lon Lon Milk", "Two drinks of hearts." },
    { ITEM_MILK_HALF, "Lon Lon Milk (half)", "One drink of hearts." },
    { ITEM_BLUE_FIRE, "Blue Fire", "Melts red ice." },
    { ITEM_BUG, "Bug", "Digs into soft soil." },
    { ITEM_POE, "Poe", "Sell it, or drink it." },
    { ITEM_BIG_POE, "Big Poe", "Sell it to the Poe Collector." },
    { SOULS_ITEM_EPONA, "Epona", "Adult. Calls Epona: Link plays her song and she comes." },
    { ITEM_OCARINA_FAIRY, "Fairy Ocarina", "Play notes with Cross and the D-pad." },
    { ITEM_OCARINA_TIME, "Ocarina of Time", "Play notes with Cross and the D-pad." },
    { ITEM_LENS, "Lens of Truth", "Uses magic. See hidden things." },
    { ITEM_BEAN, "Magic Bean", "Child. Plant in soft soil." },
    { SOULS_ITEM_TOGGLE_IRON, "Iron Boots", "Adult. Toggles them on and off." },
    { SOULS_ITEM_TOGGLE_HOVER, "Hover Boots", "Adult. Toggles them on and off." },
    { ITEM_MASK_KEATON, "Keaton Mask", "Child. Wear it." },
    { ITEM_MASK_SKULL, "Skull Mask", "Child. Wear it." },
    { ITEM_MASK_SPOOKY, "Spooky Mask", "Child. Wear it." },
    { ITEM_MASK_BUNNY, "Bunny Hood", "Child. Run faster." },
    { ITEM_MASK_GORON, "Goron Mask", "Child. Wear it." },
    { ITEM_MASK_ZORA, "Zora Mask", "Child. Wear it." },
    { ITEM_MASK_GERUDO, "Gerudo Mask", "Child. Wear it." },
    { ITEM_MASK_TRUTH, "Mask of Truth", "Child. Hear what Gossip Stones think." },
    { ITEM_WEIRD_EGG, "Weird Egg", "Child. Trade item." },
    { ITEM_CHICKEN, "Cucco", "Child. Trade item." },
    { ITEM_LETTER_ZELDA, "Zelda's Letter", "Child. Show it." },
    { ITEM_LETTER_RUTO, "Ruto's Letter", "Child. Show it to King Zora." },
    { ITEM_POCKET_EGG, "Pocket Egg", "Adult. Trade item." },
    { ITEM_POCKET_CUCCO, "Pocket Cucco", "Adult. Trade item." },
    { ITEM_COJIRO, "Cojiro", "Adult. Trade item." },
    { ITEM_ODD_MUSHROOM, "Odd Mushroom", "Adult. Trade item." },
    { ITEM_ODD_POTION, "Odd Potion", "Adult. Trade item." },
    { ITEM_SAW, "Poacher's Saw", "Adult. Trade item." },
    { ITEM_SWORD_BROKEN, "Broken Goron's Sword", "Adult. Trade item." },
    { ITEM_PRESCRIPTION, "Prescription", "Adult. Trade item." },
    { ITEM_FROG, "Eyeball Frog", "Adult. Trade item." },
    { ITEM_EYEDROPS, "Eye Drops", "Adult. Trade item." },
    { ITEM_CLAIM_CHECK, "Claim Check", "Adult. Trade item." },
};

// Every item a kind can hold, in list order. The list shows the ones the player owns and can use (CanEquip).
const uint8_t kCatRight[] = { ITEM_SWORD_KOKIRI, ITEM_SWORD_MASTER, ITEM_SWORD_BGS,
                              ITEM_SWORD_KNIFE,  ITEM_HAMMER,       ITEM_STICK };
const uint8_t kCatLeft[] = { ITEM_SLINGSHOT, ITEM_BOW,       ITEM_HOOKSHOT,
                             ITEM_LONGSHOT,  ITEM_BOOMERANG, SOULS_ITEM_CATALYST };
const uint8_t kCatShield[] = { ITEM_SHIELD_DEKU, ITEM_SHIELD_HYLIAN, ITEM_SHIELD_MIRROR };
const uint8_t kCatSpell[] = { ITEM_DINS_FIRE, ITEM_FARORES_WIND, ITEM_NAYRUS_LOVE };
const uint8_t kCatArrow[] = { ITEM_ARROW_FIRE, ITEM_ARROW_ICE, ITEM_ARROW_LIGHT };
const uint8_t kCatQuick[] = { ITEM_NUT,         ITEM_BOMB,     ITEM_BOMBCHU,   ITEM_BOTTLE,    ITEM_POTION_RED,
                              ITEM_POTION_GREEN, ITEM_POTION_BLUE, ITEM_FAIRY,  ITEM_FISH,      ITEM_MILK_BOTTLE,
                              ITEM_MILK_HALF,   ITEM_BLUE_FIRE, ITEM_BUG,      ITEM_POE,       ITEM_BIG_POE,
                              SOULS_ITEM_EPONA };
const uint8_t kCatPouch[] = {
    ITEM_OCARINA_FAIRY, ITEM_OCARINA_TIME, ITEM_LENS,        SOULS_ITEM_TOGGLE_IRON, SOULS_ITEM_TOGGLE_HOVER,
    ITEM_BEAN,          ITEM_MASK_KEATON,  ITEM_MASK_SKULL,  ITEM_MASK_SPOOKY,       ITEM_MASK_BUNNY,
    ITEM_MASK_GORON,    ITEM_MASK_ZORA,    ITEM_MASK_GERUDO, ITEM_MASK_TRUTH,        ITEM_WEIRD_EGG,
    ITEM_CHICKEN,       ITEM_LETTER_ZELDA, ITEM_LETTER_RUTO, ITEM_POCKET_EGG,        ITEM_POCKET_CUCCO,
    ITEM_COJIRO,        ITEM_ODD_MUSHROOM, ITEM_ODD_POTION,  ITEM_SAW,               ITEM_SWORD_BROKEN,
    ITEM_PRESCRIPTION,  ITEM_FROG,         ITEM_EYEDROPS,    ITEM_CLAIM_CHECK,
};
const uint8_t kCatTunic[] = { ITEM_TUNIC_KOKIRI, ITEM_TUNIC_GORON, ITEM_TUNIC_ZORA };
const uint8_t kCatBoots[] = { ITEM_BOOTS_KOKIRI, ITEM_BOOTS_IRON, ITEM_BOOTS_HOVER };

#define CAT(a)       \
    {                \
        a, (int)(sizeof(a) / sizeof(a[0])) }
struct CatRef {
    const uint8_t* ids;
    int n;
};
CatRef CatOf(int kind) {
    switch (kind) {
        case SL_RIGHT:
            return CAT(kCatRight);
        case SL_LEFT:
            return CAT(kCatLeft);
        case SL_SPELL:
            return CAT(kCatSpell);
        case SL_ARROW:
            return CAT(kCatArrow);
        case SL_QUICK:
            return CAT(kCatQuick);
        case SL_POUCH:
            return CAT(kCatPouch);
        case K_TUNIC:
            return CAT(kCatTunic);
        case K_SHIELD:
            return CAT(kCatShield);
        default:
            return CAT(kCatBoots);
    }
}

// Weapon stats for the compare card (attack physical / element, scaling Courage / Power / Wisdom, weight, reach, speed)
struct WeaponStat {
    uint8_t id;
    const char* type;
    int phys, elem;
    char sc[3];
    float weight;
    const char* reach;
    const char* speed;
    const char* passive;
};
const WeaponStat kWeapons[] = {
    { ITEM_SWORD_KOKIRI, "Short Sword", 100, 0, { 'C', 'D', 'E' }, 1.5f, "Short", "Fast", "No special effect." },
    { ITEM_SWORD_MASTER, "Straight Sword", 200, 0, { 'B', 'C', 'D' }, 3.0f, "Medium", "Normal", "Repels evil." },
    { ITEM_SWORD_BGS, "Greatsword", 400, 0, { 'C', 'A', 'E' }, 9.0f, "Long", "Slow", "Two hands." },
    { ITEM_SWORD_KNIFE, "Greatsword", 100, 0, { 'E', 'C', 'E' }, 7.0f, "Medium", "Slow", "Barely a weapon." },
    { ITEM_HAMMER, "Hammer", 200, 0, { 'D', 'A', 'E' }, 10.0f, "Short", "Slow", "Breaks rocks and rusted switches." },
    { ITEM_STICK, "Club", 200, 0, { 'D', 'D', 'E' }, 1.0f, "Medium", "Normal", "Breaks after a few hits." },
};

const ItemInfo* Info(uint8_t id) {
    for (const ItemInfo& i : kItems) {
        if (i.id == id) {
            return &i;
        }
    }
    return nullptr;
}
const WeaponStat* Weapon(uint8_t id) {
    for (const WeaponStat& w : kWeapons) {
        if (w.id == id) {
            return &w;
        }
    }
    return nullptr;
}
const char* ItemName(uint8_t id) {
    if (id == ITEM_SWORD_BGS && gSaveContext.bgsFlag == 0) {
        return "Giant's Knife";
    }
    const ItemInfo* i = Info(id);
    return i ? i->name : "";
}
const char* ItemDesc(uint8_t id) {
    if (id == ITEM_SWORD_BGS) {
        return gSaveContext.bgsFlag == 0 ? "Adult. Two hands. Breaks after a few fights."
                                         : "Adult. Two hands, never breaks.";
    }
    const ItemInfo* i = Info(id);
    return i ? i->desc : "";
}

// ---------------------------------------------------------------------------------------------------------------
// State

bool sPicker = false;
bool sConsumedB = false; // Circle closed a list this frame: OoT must not also take it
std::vector<uint8_t> sList; // the open list (built when it opens and after each change)
int sRow = 0, sCol = 0;       // the selected slot on the page (col counts the real slots, not the gaps)
int sOpenKind = 0, sOpenIdx = 0; // the slot the list was opened from
int sPcur = 0;                // the selected item in the list
uint32_t sFrame = 0;          // game frames while the page is up
uint32_t sErrUntil = 0;       // red flash on a refused item, until this frame
int sDirPrev = 0, sDirTimer = 0;
bool sTriPrev = false;

bool IsEquipPageUp(PlayState* play) {
    PauseContext* p = &play->pauseCtx;
    return p->pageIndex == PAUSE_EQUIP && p->state == 6 && p->unk_1E4 == 0 && p->debugState == 0;
}

void Sfx(u16 id) {
    Audio_PlaySfxGeneral(id, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                         &gSfxDefaultReverb);
}

// ---------------------------------------------------------------------------------------------------------------
// Slot and list logic

int SlotCount(int row) {
    int n = 0;
    for (int i = 0; i < kRows[row].count; i++) {
        n += kRows[row].s[i].kind >= 0;
    }
    return n;
}
SlotDef SlotAt(int row, int col) {
    int n = 0;
    for (int i = 0; i < kRows[row].count; i++) {
        if (kRows[row].s[i].kind >= 0) {
            if (n == col) {
                return kRows[row].s[i];
            }
            n++;
        }
    }
    return kRows[row].s[0];
}

const char* SlotName(SlotDef s, char* buf, int size) {
    switch (s.kind) {
        case SL_RIGHT:
            snprintf(buf, size, "Right Hand Armament %d", s.idx + 1);
            break;
        case SL_LEFT:
            snprintf(buf, size, "Left Hand Armament %d", s.idx + 1);
            break;
        case SL_SPELL:
            snprintf(buf, size, "Spell %d", s.idx + 1);
            break;
        case SL_ARROW:
            snprintf(buf, size, "Arrows %d", s.idx + 1);
            break;
        case SL_QUICK:
            snprintf(buf, size, "Quick Item %d", s.idx + 1);
            break;
        case SL_POUCH:
            snprintf(buf, size, "Pouch %d", s.idx + 1);
            break;
        case K_SHIELD:
            snprintf(buf, size, "Shield");
            break;
        case K_TUNIC:
            snprintf(buf, size, "Tunic");
            break;
        default:
            snprintf(buf, size, "Boots");
            break;
    }
    return buf;
}
const char* KindName(int kind) {
    switch (kind) {
        case SL_RIGHT:
            return "Right Hand";
        case SL_LEFT:
            return "Left Hand";
        case SL_SPELL:
            return "Spells";
        case SL_ARROW:
            return "Arrows";
        case SL_QUICK:
            return "Quick Items";
        case SL_POUCH:
            return "Pouch";
        case K_SHIELD:
            return "Shield";
        case K_TUNIC:
            return "Tunic";
        default:
            return "Boots";
    }
}

bool IsChooseOne(int kind) {
    return kind >= K_TUNIC;
}
// Shield, tunic and boots are OoT equipment: equip type, and the first item of the three (value 1)
int EquipTypeOf(int kind) {
    return kind == K_SHIELD ? EQUIP_TYPE_SHIELD : (kind == K_TUNIC ? EQUIP_TYPE_TUNIC : EQUIP_TYPE_BOOTS);
}
uint8_t FirstOf(int kind) {
    return kind == K_SHIELD ? ITEM_SHIELD_DEKU : (kind == K_TUNIC ? ITEM_TUNIC_KOKIRI : ITEM_BOOTS_KOKIRI);
}
int Capacity(int kind) {
    return IsChooseOne(kind) ? 1 : SoulsLoadout_Capacity((SoulsLoadoutKind)kind);
}

// Tunic and boots: value 1..3 as in OoT's equipment (0 = none)
uint8_t EquippedChooseOne(int kind) {
    int v = CUR_EQUIP_VALUE(EquipTypeOf(kind));
    if (v < 1 || v > 3) {
        return ITEM_NONE;
    }
    return (uint8_t)(FirstOf(kind) + v - 1);
}
bool ChooseOneUsable(int kind, uint8_t id) {
    int row = EquipTypeOf(kind);
    int value = id - FirstOf(kind) + 1;
    if (!CHECK_OWNED_EQUIP(row, value - 1)) {
        return false;
    }
    return GameInteractor_Should(VB_PLAYER_MEETS_AGE_REQ,
                                 gEquipAgeReqs[row][value] == 9 || gEquipAgeReqs[row][value] == gSaveContext.linkAge,
                                 gEquipAgeReqs[row][value]);
}

uint8_t SlotItem(SlotDef s) {
    if (IsChooseOne(s.kind)) {
        return EquippedChooseOne(s.kind);
    }
    return SoulsLoadout_Get((SoulsLoadoutKind)s.kind, s.idx);
}
// 0-based slot of the item in its kind, -1 if it is not equipped
int EquippedIndex(int kind, uint8_t id) {
    if (IsChooseOne(kind)) {
        return EquippedChooseOne(kind) == id ? 0 : -1;
    }
    return SoulsLoadout_IndexOf((SoulsLoadoutKind)kind, id);
}
int EquippedCount(int kind) {
    return IsChooseOne(kind) ? 1 : SoulsLoadout_Count((SoulsLoadoutKind)kind);
}

std::vector<uint8_t> BuildList(int kind) {
    std::vector<uint8_t> out;
    CatRef c = CatOf(kind);
    for (int i = 0; i < c.n; i++) {
        uint8_t id = c.ids[i];
        bool usable = IsChooseOne(kind) ? ChooseOneUsable(kind, id) : SoulsLoadout_CanEquip((SoulsLoadoutKind)kind, id);
        if (usable || EquippedIndex(kind, id) >= 0) {
            out.push_back(id);
        }
    }
    return out;
}

int ListCols(int count) {
    return count > 18 ? 8 : 6;
}

void SetChooseOne(int kind, uint8_t id) {
    int value = (id == ITEM_NONE) ? 0 : id - FirstOf(kind) + 1;
    Inventory_ChangeEquipment(EquipTypeOf(kind), value);
}

// Cross on an item in the list
void ListConfirm() {
    std::vector<uint8_t>& list = sList;
    if (list.empty()) {
        Sfx(NA_SE_SY_ERROR);
        return;
    }
    sPcur = CLAMP(sPcur, 0, (int)list.size() - 1);
    uint8_t id = list[sPcur];
    if (IsChooseOne(sOpenKind)) {
        SetChooseOne(sOpenKind, id);
        Sfx(NA_SE_SY_DECIDE);
        sPicker = false;
        return;
    }
    SoulsLoadoutKind kind = (SoulsLoadoutKind)sOpenKind;
    int at = SoulsLoadout_IndexOf(kind, id);
    if (at >= 0) {
        SoulsLoadout_Set(kind, at, ITEM_NONE); // equipped: Cross removes it
        Sfx(NA_SE_SY_DECIDE);
        return;
    }
    if (SoulsLoadout_Count(kind) >= SoulsLoadout_Capacity(kind)) {
        sErrUntil = sFrame + 10; // full: refuse
        Sfx(NA_SE_SY_ERROR);
        return;
    }
    int slot = sOpenIdx;
    if (SoulsLoadout_Get(kind, slot) != ITEM_NONE) {
        slot = -1;
        for (int i = 0; i < SoulsLoadout_Capacity(kind); i++) {
            if (SoulsLoadout_Get(kind, i) == ITEM_NONE) {
                slot = i;
                break;
            }
        }
    }
    if (slot >= 0) {
        SoulsLoadout_Set(kind, slot, id);
        Sfx(NA_SE_SY_DECIDE);
    }
}

void OpenList() {
    SlotDef s = SlotAt(sRow, sCol);
    std::vector<uint8_t> list = BuildList(s.kind);
    if (list.empty()) {
        Sfx(NA_SE_SY_ERROR);
        return;
    }
    sOpenKind = s.kind;
    sOpenIdx = s.idx;
    sList = list;
    uint8_t cur = SlotItem(s);
    sPcur = 0;
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i] == cur) {
            sPcur = (int)i;
        }
    }
    sPicker = true;
    Sfx(NA_SE_SY_DECIDE);
}

void EmptySlot() {
    SlotDef s = SlotAt(sRow, sCol);
    if (IsChooseOne(s.kind)) {
        // like OoT's "remove": no shield; back to the plain Kokiri tunic / boots
        uint8_t to = (s.kind == K_SHIELD) ? (uint8_t)ITEM_NONE : FirstOf(s.kind);
        if (EquippedChooseOne(s.kind) != to) {
            SetChooseOne(s.kind, to);
            Sfx(NA_SE_SY_DECIDE);
        }
        return;
    }
    if (SoulsLoadout_Get((SoulsLoadoutKind)s.kind, s.idx) != ITEM_NONE) {
        SoulsLoadout_Set((SoulsLoadoutKind)s.kind, s.idx, ITEM_NONE);
        Sfx(NA_SE_SY_DECIDE);
    }
}

// dir: 1 up, 2 down, 3 left, 4 right
void Move(int dir) {
    if (!sPicker) {
        int oldRow = sRow, oldCol = sCol;
        if (dir == 1 || dir == 2) {
            sRow = (sRow + (dir == 2 ? 1 : 3)) % 4;
            sCol = std::min(sCol, SlotCount(sRow) - 1);
        } else {
            int n = SlotCount(sRow);
            sCol = (sCol + (dir == 4 ? 1 : n - 1)) % n;
        }
        if (oldRow != sRow || oldCol != sCol) {
            Sfx(NA_SE_SY_CURSOR);
        }
        return;
    }
    std::vector<uint8_t>& list = sList;
    int n = (int)list.size();
    if (n == 0) {
        return;
    }
    int cols = ListCols(n), old = sPcur;
    if (dir == 3 || dir == 4) {
        sPcur = (sPcur + (dir == 4 ? 1 : n - 1)) % n;
    } else {
        sPcur = CLAMP(sPcur + (dir == 2 ? cols : -cols), 0, n - 1);
    }
    if (old != sPcur) {
        Sfx(NA_SE_SY_CURSOR);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing (the layout of the web prototype: 426 x 240 units, anchored to the left edge of the wide screen)
//
// The drawing code is outside the anonymous namespace: OPEN_DISPS declares frame interpolation functions that
// must have C linkage.
} // namespace

static int sOx = 0;
static float sSc = 1.0f, sOy = 0.0f; // layout units -> game units: x * sSc + sOx, y * sSc + sOy
static bool sFillOk = false;    // the RDP is set up for fills (cleared by everything else that draws)
#define LX(v) (((v) * sSc) + sOx)
#define LY(v) (((v) * sSc) + sOy) // left edge of the screen in game units (0 on 4:3, about -53 on 16:9)

static void FillQ(GraphicsContext* gfx, float x, float y, float w, float h, int r, int g, int b, int a) {
    if (a <= 0 || w <= 0 || h <= 0) {
        return;
    }
    int x0 = (int)lroundf(LX(x) * 4), y0 = (int)lroundf(LY(y) * 4);
    int x1 = (int)lroundf(LX(x + w) * 4), y1 = (int)lroundf(LY(y + h) * 4);
    OPEN_DISPS(gfx);
    if (!sFillOk) {
        gDPPipeSync(POLY_OPA_DISP++);
        gSPClearGeometryMode(POLY_OPA_DISP++, G_ZBUFFER | G_SHADE | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                                                  G_TEXTURE_GEN_LINEAR | G_LOD | G_SHADING_SMOOTH);
        gDPSetOtherMode(POLY_OPA_DISP++,
                        G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                            G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_1PRIMITIVE,
                        G_AC_NONE | G_ZS_PIXEL | G_RM_CLD_SURF | G_RM_CLD_SURF2);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        sFillOk = true;
    }
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, a);
    // gDPFillWideRectangle with the coordinates already in quarter units
    Gfx* g0 = POLY_OPA_DISP++;
    Gfx* g1 = POLY_OPA_DISP++;
    g0->words.w0 = _SHIFTL(G_FILLWIDERECT, 24, 8) | _SHIFTL(x1, 0, 24);
    g0->words.w1 = _SHIFTL(y1, 0, 24);
    g1->words.w0 = _SHIFTL(x0, 0, 24);
    g1->words.w1 = _SHIFTL(y0, 0, 24);
    CLOSE_DISPS(gfx);
}

// A frame of thickness t drawn inside the box
static void Ring(GraphicsContext* gfx, float x, float y, float w, float h, float t, int r, int g, int b, int a) {
    FillQ(gfx, x, y, w, t, r, g, b, a);
    FillQ(gfx, x, y + h - t, w, t, r, g, b, a);
    FillQ(gfx, x, y + t, t, h - 2 * t, r, g, b, a);
    FillQ(gfx, x + w - t, y + t, t, h - 2 * t, r, g, b, a);
}

static void Disc(GraphicsContext* gfx, float cx, float cy, float rad, int r, int g, int b, int a) {
    for (float dy = -rad; dy < rad; dy += 1.0f) {
        float yy = dy + 0.5f;
        float hw = sqrtf(std::max(0.0f, rad * rad - yy * yy));
        FillQ(gfx, cx - hw, cy + dy, 2 * hw, 1.0f, r, g, b, a);
    }
}

// Text in the game's font at the prototype's scale; returns the advance in units
static float TextW(const char* text, float scale) {
    float w = 0;
    for (const char* c = text; *c != '\0'; c++) {
        w += Ship_GetCharFontWidth((u8)*c) * scale;
    }
    return w;
}

static float Text(GraphicsContext* gfx, const char* text, float x, float y, int r, int g, int b, float scale,
                  bool shadow = true) {
    sFillOk = false;
    OPEN_DISPS(gfx);
    Gfx_SetupDL_39Opa(gfx);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 0);
    int sz = (int)lroundf(16.0f * scale * sSc * 4); // quarter units
    int texScale = (int)(1024.0f / (scale * sSc));
    float cx = x;
    for (const char* c = text; *c != '\0'; c++) {
        if (*c != ' ') {
            void* tex = Ship_GetCharFontTexture((u8)*c);
            int xq = (int)lroundf(LX(cx) * 4), yq = (int)lroundf(LY(y) * 4);
            gDPPipeSync(POLY_OPA_DISP++);
            gDPLoadTextureBlock_4b(POLY_OPA_DISP++, tex, G_IM_FMT_I, FONT_CHAR_TEX_WIDTH, FONT_CHAR_TEX_HEIGHT, 0,
                                   G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                                   G_TX_NOLOD, G_TX_NOLOD);
            if (shadow) {
                int o = (int)lroundf(0.8f * sSc * 4);
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 0, 0, 0, 255);
                gSPWideTextureRectangle(POLY_OPA_DISP++, xq + o, yq + o, xq + o + sz, yq + o + sz, G_TX_RENDERTILE, 0,
                                        0, texScale, texScale);
                gDPPipeSync(POLY_OPA_DISP++);
            }
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, 255);
            gSPWideTextureRectangle(POLY_OPA_DISP++, xq, yq, xq + sz, yq + sz, G_TX_RENDERTILE, 0, 0, texScale,
                                    texScale);
        }
        cx += Ship_GetCharFontWidth((u8)*c) * scale;
    }
    CLOSE_DISPS(gfx);
    return TextW(text, scale);
}

static std::vector<std::string> WrapText(const char* text, float maxWidth, float scale) {
    std::vector<std::string> lines;
    std::string line, word;
    std::string all = std::string(text) + " ";
    for (char c : all) {
        if (c != ' ') {
            word += c;
            continue;
        }
        std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && TextW(trial.c_str(), scale) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// OoT's file select window (gFileSelWindow1..20Tex: a 240 x 160 sheet of 4 x 5 tiles), stretched as a nine-patch
static void Window(GraphicsContext* gfx, float x, float y, float w, float h) {
    static const void* const kTex[20] = {
        gFileSelWindow1Tex,  gFileSelWindow2Tex,  gFileSelWindow3Tex,  gFileSelWindow4Tex,  gFileSelWindow5Tex,
        gFileSelWindow6Tex,  gFileSelWindow7Tex,  gFileSelWindow8Tex,  gFileSelWindow9Tex,  gFileSelWindow10Tex,
        gFileSelWindow11Tex, gFileSelWindow12Tex, gFileSelWindow13Tex, gFileSelWindow14Tex, gFileSelWindow15Tex,
        gFileSelWindow16Tex, gFileSelWindow17Tex, gFileSelWindow18Tex, gFileSelWindow19Tex, gFileSelWindow20Tex,
    };
    static const int kColW[4] = { 64, 64, 64, 48 };
    const float us[4] = { 0, 16, 224, 240 }, vs[4] = { 0, 26, 146, 160 };
    const float xs[4] = { x, x + 8, x + w - 8, x + w }, ys[4] = { y, y + 13, y + h - 7, y + h };
    FillQ(gfx, x + 3, y + 3, w - 6, h - 6, 10, 10, 12, 215); // keeps the pages behind from showing through
    sFillOk = false;
    OPEN_DISPS(gfx);
    Gfx_SetupDL_39Opa(gfx);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 120, 124, 134, 255);
    for (int ty = 0; ty < 5; ty++) {
        float tv0 = ty * 32.0f, tv1 = tv0 + 32;
        float tu0 = 0;
        for (int tx = 0; tx < 4; tx++) {
            float tu1 = tu0 + kColW[tx];
            gDPPipeSync(POLY_OPA_DISP++);
            gDPLoadTextureBlock(POLY_OPA_DISP++, (void*)kTex[ty * 4 + tx], G_IM_FMT_IA, G_IM_SIZ_16b, kColW[tx], 32, 0,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
            for (int pj = 0; pj < 3; pj++) {
                float v0 = std::max(tv0, vs[pj]), v1 = std::min(tv1, vs[pj + 1]);
                if (v0 >= v1) {
                    continue;
                }
                for (int pi = 0; pi < 3; pi++) {
                    float u0 = std::max(tu0, us[pi]), u1 = std::min(tu1, us[pi + 1]);
                    if (u0 >= u1) {
                        continue;
                    }
                    float kx = (xs[pi + 1] - xs[pi]) / (us[pi + 1] - us[pi]);
                    float ky = (ys[pj + 1] - ys[pj]) / (vs[pj + 1] - vs[pj]);
                    float dx0 = xs[pi] + (u0 - us[pi]) * kx, dx1 = xs[pi] + (u1 - us[pi]) * kx;
                    float dy0 = ys[pj] + (v0 - vs[pj]) * ky, dy1 = ys[pj] + (v1 - vs[pj]) * ky;
                    int xq0 = (int)lroundf(LX(dx0) * 4), xq1 = (int)lroundf(LX(dx1) * 4);
                    int yq0 = (int)lroundf(LY(dy0) * 4), yq1 = (int)lroundf(LY(dy1) * 4);
                    if (xq1 <= xq0 || yq1 <= yq0) {
                        continue;
                    }
                    int dsdx = (int)lroundf(1024.0f * (u1 - u0) / ((dx1 - dx0) * sSc));
                    int dtdy = (int)lroundf(1024.0f * (v1 - v0) / ((dy1 - dy0) * sSc));
                    gSPWideTextureRectangle(POLY_OPA_DISP++, xq0, yq0, xq1, yq1, G_TX_RENDERTILE,
                                            (int)lroundf((u0 - tu0) * 32), (int)lroundf((v0 - tv0) * 32), dsdx, dtdy);
                }
            }
            tu0 = tu1;
        }
    }
    CLOSE_DISPS(gfx);
}

// An item icon (the same textures the C buttons used); shade < 255 dims it
static void Icon(GraphicsContext* gfx, uint8_t id, float fx, float fy, float fsize, int shade, int alpha) {
    const void* tex = nullptr;
    int dim = 32;
    switch (id) {
        case SOULS_ITEM_CATALYST:
            tex = gQuestIconMagicJarSmallTex;
            dim = 24;
            break;
        case SOULS_ITEM_TOGGLE_IRON:
            tex = gItemIcons[ITEM_BOOTS_IRON];
            break;
        case SOULS_ITEM_TOGGLE_HOVER:
            tex = gItemIcons[ITEM_BOOTS_HOVER];
            break;
        case SOULS_ITEM_EPONA:
            tex = gCarrotIconTex;
            dim = 16;
            break;
        default:
            if (id < 158) {
                tex = gItemIcons[id];
            }
            break;
    }
    if (tex == nullptr) {
        return;
    }
    int xq0 = (int)lroundf(LX(fx) * 4), yq0 = (int)lroundf(LY(fy) * 4);
    int sq = (int)lroundf(fsize * sSc * 4);
    sFillOk = false;
    OPEN_DISPS(gfx);
    Gfx_SetupDL_39Opa(gfx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, shade, shade, shade, alpha);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPLoadTextureBlock(POLY_OPA_DISP++, (void*)tex, G_IM_FMT_RGBA, G_IM_SIZ_32b, dim, dim, 0,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    int dsdx = (int)lroundf(1024.0f * dim / (fsize * sSc));
    gSPWideTextureRectangle(POLY_OPA_DISP++, xq0, yq0, xq0 + sq, yq0 + sq, G_TX_RENDERTILE, 0, 0, dsdx, dsdx);
    CLOSE_DISPS(gfx);
}

// 7x7 pixel marks for the button prompts (X, triangle, circle) and the check mark
static void Glyph(GraphicsContext* gfx, int which, float x, float y, float px, int r, int g, int b) {
    static const char* const kGlyphs[3][7] = {
        { "1000001", "0100010", "0010100", "0001000", "0010100", "0100010", "1000001" },
        { "0001000", "0001000", "0010100", "0010100", "0100010", "0100010", "1111111" },
        { "0011100", "0100010", "1000001", "1000001", "1000001", "0100010", "0011100" },
    };
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 7; col++) {
            if (kGlyphs[which][row][col] == '1') {
                FillQ(gfx, x + col * px, y + row * px, px, px, r, g, b, 255);
            }
        }
    }
}
static void CheckMark(GraphicsContext* gfx, float x, float y, float px, int r, int g, int b) {
    static const char* const kRowsCheck[6] = { "0000011", "0000110", "1001100", "1111000", "0110000", "0100000" };
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            if (kRowsCheck[row][col] == '1') {
                FillQ(gfx, x + col * px, y + row * px, px, px, r, g, b, 255);
            }
        }
    }
}

struct Prompt {
    int glyph; // 0 Cross, 1 Triangle, 2 Circle
    const char* label;
};

static void DrawPrompts(GraphicsContext* gfx, const std::vector<Prompt>& prompts, float y = 222, float cx = 213) {
    float total = 0;
    for (const Prompt& p : prompts) {
        total += 18 + TextW(p.label, 0.7f) + 12;
    }
    float x = cx - total / 2;
    for (const Prompt& p : prompts) {
        Disc(gfx, x + 6.5f, y + 6.5f, 6.8f, 20, 20, 24, 255);
        Disc(gfx, x + 6.5f, y + 6.5f, 6.2f, 150, 150, 156, 255);
        Glyph(gfx, p.glyph, x + 6.5f - 3.15f, y + 6.5f - 3.15f, 0.9f, 255, 255, 255);
        Text(gfx, p.label, x + 16, y + 1.5f, 255, 255, 255, 0.7f);
        x += 18 + TextW(p.label, 0.7f) + 12;
    }
}

namespace {

const float kSL = 23, kSG = 3.5f; // slot square and the space between slots
const int kBlue[3] = { 190, 205, 235 };
const int kGold[3] = { 255, 220, 120 };

} // namespace

// A slot: dark square, thin light frame, the icon. on = dark gold fill with a gold frame (equipped in a list),
// dim = the icon at about 30 %.
static void DrawSlot(GraphicsContext* gfx, float x, float y, float s, uint8_t item, bool on, bool dim, float iconMargin) {
    if (on) {
        FillQ(gfx, x, y, s, s, 70, 56, 20, 205);
        Ring(gfx, x, y, s, s, 1.0f, 232, 200, 110, 255);
    } else {
        FillQ(gfx, x, y, s, s, 4, 8, 24, 166);
        Ring(gfx, x, y, s, s, 0.75f, 160, 190, 255, 140);
    }
    if (item != ITEM_NONE) {
        Icon(gfx, item, x + iconMargin, y + iconMargin, s - 2 * iconMargin, dim ? 80 : 255, dim ? 90 : 255);
    }
}

// The steady gold selection: a glow, a gold frame, a light inner glow (red for a refused item)
static void DrawSelection(GraphicsContext* gfx, float x, float y, float s, bool red) {
    int r = 255, g = red ? 90 : 214, b = red ? 80 : 110;
    Ring(gfx, x - 5.5f, y - 5.5f, s + 11, s + 11, 1.5f, r, g, b, 20);
    Ring(gfx, x - 4.0f, y - 4.0f, s + 8, s + 8, 1.5f, r, g, b, 40);
    Ring(gfx, x - 2.5f, y - 2.5f, s + 5, s + 5, 1.5f, r, g, b, 90);
    Ring(gfx, x - 1.4f, y - 1.4f, s + 2.8f, s + 2.8f, 1.4f, r, g, b, 255);
    Ring(gfx, x, y, s, s, 2.5f, r, std::min(255, g + 10), std::min(255, b + 30), 60);
}

static void DrawAmmo(GraphicsContext* gfx, uint8_t item, float x, float y, float s) {
    switch (item) {
        case ITEM_STICK:
        case ITEM_NUT:
        case ITEM_BOMB:
        case ITEM_BOMBCHU:
        case ITEM_SLINGSHOT:
        case ITEM_BOW:
        case ITEM_BEAN:
            break;
        default:
            return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", AMMO(item));
    float w = TextW(buf, 0.5f);
    Text(gfx, buf, x + s - 2 - w, y + s - 8.5f, 255, 255, 255, 0.5f);
}

// The slot number (or a check mark) on the top-right corner of an equipped cell: a small round gold badge
static void DrawBadge(GraphicsContext* gfx, float x, float y, float c, int number) {
    float cx = x + c - 3.0f, cy = y + 2.0f;
    Disc(gfx, cx, cy, 5.1f, 26, 18, 6, 255);
    Disc(gfx, cx, cy, 4.5f, 232, 200, 110, 255);
    if (number > 0) {
        char t[4];
        snprintf(t, sizeof(t), "%d", number);
        float w = TextW(t, 0.45f);
        Text(gfx, t, cx - w / 2, cy - 3.6f, 26, 18, 6, 0.45f, false);
    } else {
        CheckMark(gfx, cx - 3.0f, cy - 2.6f, 0.85f, 26, 18, 6);
    }
}

static void DrawMainPage(GraphicsContext* gfx, PlayState* play) {
    SlotDef cur = SlotAt(sRow, sCol);
    char name[40];

    Window(gfx, 8, 18, 248, 196);
    Text(gfx, SlotName(cur, name, sizeof(name)), 20, 22, kGold[0], kGold[1], kGold[2], 0.7f);

    for (int ri = 0; ri < 4; ri++) {
        float y0 = 42.0f + ri * 41;
        Text(gfx, kRows[ri].label, 20, y0, kBlue[0], kBlue[1], kBlue[2], 0.55f);
        float xs[10], xc = 20;
        for (int i = 0; i < kRows[ri].count; i++) {
            xs[i] = xc;
            xc += kRows[ri].s[i].kind < 0 ? 7 : kSL + kSG;
        }
        int sel = -1;
        for (int i = 0; i < kRows[ri].count; i++) {
            SlotDef s = kRows[ri].s[i];
            if (s.kind < 0) {
                continue;
            }
            if (ri == sRow && s.kind == cur.kind && s.idx == cur.idx) {
                sel = i;
            }
            uint8_t item = SlotItem(s);
            DrawSlot(gfx, xs[i], y0 + 10, kSL, item, false, false, 2.5f);
            DrawAmmo(gfx, item, xs[i], y0 + 10, kSL);
        }
        if (sel >= 0) {
            DrawSelection(gfx, xs[sel], y0 + 10, kSL, false);
        }
    }

    // Right: Link and his status
    Window(gfx, 262, 18, 156, 196);
    FillQ(gfx, 276, 34, 128, 92, 10, 14, 26, 255);
    sFillOk = false;
    KaleidoScope_DrawPauseLink2D(play, (int)lroundf(LX(276)), (int)lroundf(LY(34)), (int)lroundf(128 * sSc),
                                 (int)lroundf(92 * sSc));
    sFillOk = false;
    Ring(gfx, 276, 34, 128, 92, 0.6f, 160, 190, 255, 128);

    uint8_t right = SoulsLoadout_Current(SL_RIGHT), shield = EquippedChooseOne(K_SHIELD);
    const WeaponStat* ws = Weapon(right);
    char atk[16] = "-";
    if (ws != nullptr) {
        snprintf(atk, sizeof(atk), "%d", ws->phys + ws->elem);
    }
    uint8_t tunic = EquippedChooseOne(K_TUNIC), boots = EquippedChooseOne(K_BOOTS);
    struct Line {
        const char* a;
        const char* b;
    } lines[5] = {
        { "Attack (R)", atk },
        { "Shield", shield != ITEM_NONE ? ItemName(shield) : "-" },
        { "Spells", SoulsLoadout_HasCatalyst() ? "Ready" : "Need a catalyst" },
        { "Tunic", tunic != ITEM_NONE ? ItemName(tunic) : "-" },
        { "Boots", boots != ITEM_NONE ? ItemName(boots) : "-" },
    };
    for (int i = 0; i < 5; i++) {
        Text(gfx, lines[i].a, 276, 132.0f + i * 10, kBlue[0], kBlue[1], kBlue[2], 0.52f);
        Text(gfx, lines[i].b, 330, 132.0f + i * 10, 255, 255, 255, 0.52f);
    }
    uint8_t item = SlotItem(cur);
    Text(gfx, item != ITEM_NONE ? ItemName(item) : "(empty)", 276, 186, kGold[0], kGold[1], kGold[2], 0.62f);
    if (item != ITEM_NONE) {
        std::vector<std::string> d = WrapText(ItemDesc(item), 130, 0.48f);
        for (size_t i = 0; i < d.size() && i < 2; i++) {
            Text(gfx, d[i].c_str(), 276, 197.0f + i * 8, 255, 255, 255, 0.48f);
        }
    }

    std::vector<Prompt> prompts = { { 0, "Change" } };
    if (IsChooseOne(cur.kind) ? true : item != ITEM_NONE) {
        prompts.push_back({ 1, "Remove" });
    }
    prompts.push_back({ 2, "Back" });
    DrawPrompts(gfx, prompts);
}

// Compare card colors
static const int kUp[3] = { 120, 255, 140 }, kDown[3] = { 255, 120, 120 }, kDim[3] = { 160, 175, 210 };
static int Grade(char c) {
    switch (c) {
        case 'S':
            return 6;
        case 'A':
            return 5;
        case 'B':
            return 4;
        case 'C':
            return 3;
        case 'D':
            return 2;
        case 'E':
            return 1;
        default:
            return 0;
    }
}
static int Rank(const char* s, const char* const* order, int n) {
    for (int i = 0; i < n; i++) {
        if (strcmp(s, order[i]) == 0) {
            return i + 1;
        }
    }
    return 0;
}

static void DrawStatCard(GraphicsContext* gfx, uint8_t newId, uint8_t baseId, float x, float y) {
    static const char* const kReach[] = { "Short", "Medium", "Long" };
    static const char* const kSpeed[] = { "Slow", "Normal", "Fast" };
    const WeaponStat* w = Weapon(newId);
    const WeaponStat* o = Weapon(baseId);
    if (w == nullptr) {
        return;
    }
    if (o == nullptr) {
        o = w;
    }
    Window(gfx, x, y, 204, 188);
    Text(gfx, ItemName(newId), x + 12, y + 3, kGold[0], kGold[1], kGold[2], 0.8f);
    Text(gfx, w->type, x + 14, y + 22, kBlue[0], kBlue[1], kBlue[2], 0.56f);

    std::vector<std::string> head = WrapText(ItemName(baseId != ITEM_NONE ? baseId : newId), 62, 0.5f);
    for (size_t i = 0; i < head.size() && i < 2; i++) {
        float yy = y + 46 + i * 8.5f - (head.size() > 1 ? 8.5f : 0);
        Text(gfx, head[i].c_str(), x + 76, yy, kDim[0], kDim[1], kDim[2], 0.5f);
    }
    Text(gfx, "New", x + 150, y + 46, kDim[0], kDim[1], kDim[2], 0.5f);

    struct Row {
        const char* label;
        std::string a, b;
        int cmp; // 1 better, -1 worse, 0 same, 2 header only
    };
    char buf[2][16];
    std::vector<Row> rows;
    auto num = [](int v) { return std::to_string(v); };
    auto cmpI = [](int nv, int ov) { return nv == ov ? 0 : (nv > ov ? 1 : -1); };
    rows.push_back({ "Attack", num(o->phys + o->elem), num(w->phys + w->elem), cmpI(w->phys + w->elem, o->phys + o->elem) });
    rows.push_back({ "  Physical", num(o->phys), num(w->phys), cmpI(w->phys, o->phys) });
    rows.push_back({ "  Element", o->elem ? num(o->elem) : "-", w->elem ? num(w->elem) : "-", cmpI(w->elem, o->elem) });
    rows.push_back({ "Scaling", "", "", 2 });
    static const char* const kSc[3] = { "  Courage", "  Power", "  Wisdom" };
    for (int i = 0; i < 3; i++) {
        rows.push_back({ kSc[i], std::string(1, o->sc[i]), std::string(1, w->sc[i]), cmpI(Grade(w->sc[i]), Grade(o->sc[i])) });
    }
    snprintf(buf[0], sizeof(buf[0]), "%.1f", o->weight);
    snprintf(buf[1], sizeof(buf[1]), "%.1f", w->weight);
    rows.push_back({ "Weight", buf[0], buf[1], w->weight == o->weight ? 0 : (w->weight < o->weight ? 1 : -1) });
    rows.push_back({ "Reach", o->reach, w->reach, cmpI(Rank(w->reach, kReach, 3), Rank(o->reach, kReach, 3)) });
    rows.push_back({ "Speed", o->speed, w->speed, cmpI(Rank(w->speed, kSpeed, 3), Rank(o->speed, kSpeed, 3)) });
    for (size_t i = 0; i < rows.size(); i++) {
        float yy = y + 57 + i * 10.4f;
        const Row& r = rows[i];
        Text(gfx, r.label, x + 14, yy, 255, 255, 255, 0.56f);
        if (r.cmp == 2) {
            continue;
        }
        const int* c = r.cmp > 0 ? kUp : (r.cmp < 0 ? kDown : kDim);
        Text(gfx, r.a.c_str(), x + 76, yy, kDim[0], kDim[1], kDim[2], 0.56f);
        Text(gfx, ">", x + 138, yy, kDim[0], kDim[1], kDim[2], 0.56f);
        Text(gfx, r.b.c_str(), x + 150, yy, r.cmp == 0 ? 255 : c[0], r.cmp == 0 ? 255 : c[1], r.cmp == 0 ? 255 : c[2],
             0.56f);
    }
    std::vector<std::string> pass = WrapText(w->passive, 180, 0.5f);
    for (size_t i = 0; i < pass.size() && i < 2; i++) {
        Text(gfx, pass[i].c_str(), x + 14, y + 163 + i * 9, 170, 225, 255, 0.5f);
    }
}

static void DrawListPage(GraphicsContext* gfx) {
    int kind = sOpenKind;
    std::vector<uint8_t>& list = sList;
    if (list.empty()) {
        sPicker = false;
        return;
    }
    sPcur = CLAMP(sPcur, 0, (int)list.size() - 1);
    uint8_t cur = list[sPcur];
    int cap = Capacity(kind), n = EquippedCount(kind);
    bool multi = cap > 1, full = multi && n >= cap;
    bool onCur = EquippedIndex(kind, cur) >= 0;
    bool blocked = full && !onCur;

    Window(gfx, 8, 18, 188, 196);
    Text(gfx, KindName(kind), 20, 22, kGold[0], kGold[1], kGold[2], 0.7f);
    if (multi) {
        char t[16];
        snprintf(t, sizeof(t), "%d / %d", n, cap);
        Text(gfx, t, 184 - TextW(t, 0.7f), 22, full ? 255 : kBlue[0], full ? 130 : kBlue[1], full ? 110 : kBlue[2],
             0.7f);
    }

    int cols = ListCols((int)list.size());
    float c = cols == 8 ? 19.0f : 24.0f, st = cols == 8 ? 21.2f : 28.0f;
    float gridBottom = 40;
    for (size_t i = 0; i < list.size(); i++) {
        float x = 20 + (int)(i % cols) * st, y = 40 + (int)(i / cols) * st;
        gridBottom = y + c;
        bool on = EquippedIndex(kind, list[i]) >= 0, off = full && !on;
        DrawSlot(gfx, x, y, c, list[i], on, off, c * 0.1f);
    }
    {
        float x = 20 + (sPcur % cols) * st, y = 40 + (sPcur / cols) * st;
        DrawSelection(gfx, x, y, c, sErrUntil > sFrame);
    }
    for (size_t i = 0; i < list.size(); i++) {
        int at = EquippedIndex(kind, list[i]);
        if (at < 0) {
            continue;
        }
        float x = 20 + (int)(i % cols) * st, y = 40 + (int)(i / cols) * st;
        DrawBadge(gfx, x, y, c, multi ? at + 1 : 0);
    }

    char note[48] = "";
    if (blocked) {
        snprintf(note, sizeof(note), "All %d slots are full. Remove one first.", cap);
    }
    if (kind == SL_RIGHT && Weapon(cur) != nullptr) {
        uint8_t base = SoulsLoadout_Get(SL_RIGHT, sOpenIdx);
        if (base == ITEM_NONE) {
            base = SoulsLoadout_Current(SL_RIGHT);
        }
        if (base == ITEM_NONE) {
            base = ITEM_SWORD_KOKIRI;
        }
        DrawStatCard(gfx, cur, base, 202, 18);
        if (gridBottom < 160) {
            Text(gfx, ItemName(cur), 20, 168, 255, 255, 255, 0.7f);
            std::vector<std::string> d = WrapText(ItemDesc(cur), 168, 0.48f);
            for (size_t i = 0; i < d.size() && i < 2; i++) {
                Text(gfx, d[i].c_str(), 20, 182.0f + i * 9, 255, 255, 255, 0.48f);
            }
        }
        if (note[0] != '\0') {
            Text(gfx, note, 20, 201, 255, 130, 110, 0.48f);
        }
    } else {
        Window(gfx, 202, 18, 216, 196);
        Icon(gfx, cur, 278, 46, 64, blocked ? 90 : 255, blocked ? 100 : 255);
        Text(gfx, ItemName(cur), 216, 130, kGold[0], kGold[1], kGold[2], 0.8f);
        std::vector<std::string> d = WrapText(ItemDesc(cur), 188, 0.5f);
        for (size_t i = 0; i < d.size() && i < 3; i++) {
            Text(gfx, d[i].c_str(), 216, 150.0f + i * 10, 255, 255, 255, 0.5f);
        }
        if (note[0] != '\0') {
            Text(gfx, note, 216, 176, 255, 130, 110, 0.5f);
        }
    }

    std::vector<Prompt> prompts;
    if (IsChooseOne(kind)) {
        if (!onCur) {
            prompts.push_back({ 0, "Equip" });
        }
    } else if (onCur) {
        prompts.push_back({ 0, "Remove" });
    } else if (!blocked) {
        prompts.push_back({ 0, "Equip" });
    }
    prompts.push_back({ 2, "Done" });
    DrawPrompts(gfx, prompts);
}

namespace {
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Entry points

extern "C" bool SoulsEquipPage_Active(void) {
    return SoulsLoadout_IsEnabled();
}

extern "C" bool SoulsEquipPage_Shown(PlayState* play) {
    return SoulsLoadout_IsEnabled() && IsEquipPageUp(play);
}

extern "C" bool SoulsEquipPage_ConsumesBack(PlayState* play) {
    return sConsumedB;
}

extern "C" bool SoulsEquipPage_BlocksPageTurn(PlayState* play) {
    return SoulsEquipPage_Shown(play) && sPicker;
}

extern "C" void SoulsEquipPage_Update(PlayState* play) {
    sConsumedB = false;
    if (!SoulsLoadout_IsEnabled()) {
        sPicker = false;
        return;
    }
    PauseContext* p = &play->pauseCtx;
    Input* input = &play->state.input[0];

    // Triangle and Cross both reach the menu as A: tell them apart from the raw pad buttons
    uint16_t raw = gSoulsRawPadButtons;
    bool triDown = (raw & BTN_A) != 0 && (raw & 0x0040) == 0; // Circle sends A + Modifier 1
    bool triEdge = triDown && !sTriPrev;
    sTriPrev = triDown;

    if (!IsEquipPageUp(play)) {
        sPicker = false; // a list never stays open when the page is not up (page turn, Start, closing the pause)
        sDirPrev = 0;
        return;
    }
    sFrame++;
    p->cursorSpecialPos = 0; // the page turn arrows are not a cursor stop on this page (L / R still turn the page)
    p->cursorItem[PAUSE_EQUIP] = PAUSE_ITEM_NONE; // no OoT name panel or "To Equip" prompt over this page
    p->namedItem = PAUSE_ITEM_NONE;

    // Direction: D-pad or stick, with a repeat while held
    int dir = 0;
    uint16_t cur = input->cur.button;
    if (cur & BTN_DUP) {
        dir = 1;
    } else if (cur & BTN_DDOWN) {
        dir = 2;
    } else if (cur & BTN_DLEFT) {
        dir = 3;
    } else if (cur & BTN_DRIGHT) {
        dir = 4;
    } else if (input->rel.stick_y > 30) {
        dir = 1;
    } else if (input->rel.stick_y < -30) {
        dir = 2;
    } else if (input->rel.stick_x < -30) {
        dir = 3;
    } else if (input->rel.stick_x > 30) {
        dir = 4;
    }
    bool fire = false;
    if (dir != 0 && dir != sDirPrev) {
        fire = true;
        sDirTimer = 8;
    } else if (dir != 0 && --sDirTimer <= 0) {
        fire = true;
        sDirTimer = 3;
    }
    sDirPrev = dir;
    if (fire) {
        Move(dir);
    }

    bool pressA = CHECK_BTN_ALL(input->press.button, BTN_A);
    bool pressB = CHECK_BTN_ALL(input->press.button, BTN_B);
    bool cross = pressA && !triEdge;
    if (!sPicker) {
        if (cross) {
            OpenList();
        } else if (triEdge) {
            EmptySlot();
        }
        // Circle on the page itself is OoT's own (the save prompt)
    } else {
        if (cross) {
            ListConfirm();
        } else if (pressB) {
            sPicker = false;
            sConsumedB = true;
            Sfx(NA_SE_SY_DECIDE);
        }
    }
}

extern "C" void SoulsEquipPage_Draw(PlayState* play) {
    if (!SoulsEquipPage_Shown(play)) {
        return;
    }
    GraphicsContext* gfx = play->state.gfxCtx;
    // The layout is 426 x 240 units: centred in the visible width, scaled down when the screen is narrower (4:3)
    int left = OTRGetRectDimensionFromLeftEdge(0), right = OTRGetRectDimensionFromRightEdge(320);
    float width = (float)(right - left);
    sSc = std::min(1.0f, width / 426.0f);
    sOx = (int)lroundf(left + (width - 426.0f * sSc) / 2);
    sOy = (240.0f - 240.0f * sSc) / 2;
    sFillOk = false;
    // Dim the whole visible screen, whatever its shape
    {
        float sc = sSc, oy = sOy;
        int ox = sOx;
        sSc = 1.0f;
        sOy = 0;
        sOx = 0;
        FillQ(gfx, (float)left - 4, -4, width + 8, 248, 0, 0, 0, 205);
        sSc = sc;
        sOy = oy;
        sOx = ox;
    }
    Text(gfx, "EQUIPMENT", 14, 4, 255, 220, 120, 0.95f);
    if (sPicker) {
        DrawListPage(gfx);
    } else {
        DrawMainPage(gfx, play);
    }
}
