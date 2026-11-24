// Souls of Harkinian: loadout storage (see souls/ER-SYSTEM.md section 2).
//
// Six lists of item slots (right hand, left hand, spells, arrows, quick items, pouch) plus the active slot of each.
// Items are OoT item IDs. The lists are saved per file in the section "soulsLoadout". A save without the section is
// filled once, when it loads, from what Link has equipped (sword, shield, C-button items), so nobody starts unarmed.
#include "soh/Enhancements/controls/SoulsLoadout.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <nlohmann/json.hpp>
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
// Age tables of the pause menu (z_kaleido_scope_PAL.c)
extern u8 gItemAgeReqs[];
extern u8 gEquipAgeReqs[][4];
}

namespace {
const int kCap[SL_KIND_COUNT] = { 3, 3, 3, 2, 8, 4 };
uint8_t sSlots[SL_KIND_COUNT][8];
int sCurrent[SL_KIND_COUNT] = {};
uint32_t sLastUse[SL_KIND_COUNT] = {}, sLastSwitch[SL_KIND_COUNT] = {};
bool sPouchHeld = false;
bool sFilled = false; // the save has a loadout (loaded, or filled once)

bool Valid(int kind) {
    return kind >= 0 && kind < SL_KIND_COUNT;
}

void ClearAll() {
    for (int k = 0; k < SL_KIND_COUNT; k++) {
        for (int i = 0; i < 8; i++) {
            sSlots[k][i] = ITEM_NONE;
        }
        sCurrent[k] = 0;
    }
}

// An item can be used at Link's current age (0xFF age requirement = any age)
bool AgeOk(int req) {
    bool ok = req == 9 || req == gSaveContext.linkAge;
    return GameInteractor_Should(VB_ITEM_MEETS_AGE_REQ, ok, 0);
}

bool ItemAgeOk(uint8_t item) {
    return AgeOk(gItemAgeReqs[item]);
}

// The inventory slot item (for example the hookshot slot holds the hookshot or the longshot)
bool InInventory(uint8_t item) {
    return item < ITEM_NONE && INV_CONTENT(item) == item;
}

bool IsBottleItem(uint8_t item) {
    return item >= ITEM_BOTTLE && item <= ITEM_POE;
}

bool HasBottleWith(uint8_t item) {
    for (int s = SLOT_BOTTLE_1; s <= SLOT_BOTTLE_4; s++) {
        if (gSaveContext.inventory.items[s] == item) {
            return true;
        }
    }
    return false;
}

bool OwnsEquip(int type, int value) { // value 1..3 as in EQUIP_VALUE_*
    return CHECK_OWNED_EQUIP(type, value - 1) != 0;
}

bool OwnsAnySpell() {
    return InInventory(ITEM_DINS_FIRE) || InInventory(ITEM_FARORES_WIND) || InInventory(ITEM_NAYRUS_LOVE);
}

bool CanEquipRight(uint8_t item) {
    switch (item) {
        case ITEM_SWORD_KOKIRI:
            return OwnsEquip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_SWORD][EQUIP_VALUE_SWORD_KOKIRI]);
        case ITEM_SWORD_MASTER:
            return OwnsEquip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_SWORD][EQUIP_VALUE_SWORD_MASTER]);
        case ITEM_SWORD_BGS: // Biggoron Sword
            return gSaveContext.bgsFlag != 0 && OwnsEquip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_SWORD][EQUIP_VALUE_SWORD_BIGGORON]);
        case ITEM_SWORD_KNIFE: // Giant's Knife (the Biggoron slot before the real sword is obtained)
            return gSaveContext.bgsFlag == 0 && OwnsEquip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_SWORD][EQUIP_VALUE_SWORD_BIGGORON]);
        case ITEM_HAMMER:
        case ITEM_STICK:
            return InInventory(item) && ItemAgeOk(item);
    }
    return false;
}

bool CanEquipLeft(uint8_t item) {
    if (item >= ITEM_SHIELD_DEKU && item <= ITEM_SHIELD_MIRROR) {
        int v = item - ITEM_SHIELD_DEKU + 1; // EQUIP_VALUE_SHIELD_*
        return OwnsEquip(EQUIP_TYPE_SHIELD, v) && AgeOk(gEquipAgeReqs[EQUIP_TYPE_SHIELD][v]);
    }
    switch (item) {
        case ITEM_SLINGSHOT:
        case ITEM_BOW:
        case ITEM_BOOMERANG:
        case ITEM_HOOKSHOT:
        case ITEM_LONGSHOT:
            return InInventory(item) && ItemAgeOk(item);
        case SOULS_ITEM_CATALYST:
            return OwnsAnySpell();
    }
    return false;
}

bool CanEquipQuick(uint8_t item) {
    switch (item) {
        case ITEM_NUT:
        case ITEM_BOMB:
        case ITEM_BOMBCHU:
            return InInventory(item) && ItemAgeOk(item);
        case SOULS_ITEM_EPONA:
            return CHECK_QUEST_ITEM(QUEST_SONG_EPONA) && GET_EVENTCHKINF(EVENTCHKINF_EPONA_OBTAINED);
    }
    return IsBottleItem(item) && HasBottleWith(item);
}

bool CanEquipPouch(uint8_t item) {
    switch (item) {
        case ITEM_OCARINA_FAIRY:
        case ITEM_OCARINA_TIME:
        case ITEM_LENS:
            return InInventory(item);
        case ITEM_BEAN:
            return InInventory(item) && ItemAgeOk(item);
        case SOULS_ITEM_TOGGLE_IRON:
            return OwnsEquip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_IRON) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_BOOTS][EQUIP_VALUE_BOOTS_IRON]);
        case SOULS_ITEM_TOGGLE_HOVER:
            return OwnsEquip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_HOVER) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_BOOTS][EQUIP_VALUE_BOOTS_HOVER]);
    }
    // Masks and trade items: they share two inventory slots, so only the one held now counts as owned
    if (item >= ITEM_LETTER_ZELDA && item <= ITEM_CLAIM_CHECK) {
        return gSaveContext.inventory.items[SLOT_TRADE_CHILD] == item ||
               gSaveContext.inventory.items[SLOT_TRADE_ADULT] == item;
    }
    return false;
}

// Which list an item of the current inventory belongs to (used by the one-time fill)
int KindOfItem(uint8_t item) {
    switch (item) {
        case ITEM_SWORD_KOKIRI:
        case ITEM_SWORD_MASTER:
        case ITEM_SWORD_BGS:
        case ITEM_SWORD_KNIFE:
        case ITEM_HAMMER:
        case ITEM_STICK:
            return SL_RIGHT;
        case ITEM_SLINGSHOT:
        case ITEM_BOW:
        case ITEM_BOOMERANG:
        case ITEM_HOOKSHOT:
        case ITEM_LONGSHOT:
            return SL_LEFT;
        case ITEM_DINS_FIRE:
        case ITEM_FARORES_WIND:
        case ITEM_NAYRUS_LOVE:
            return SL_SPELL;
        case ITEM_ARROW_FIRE:
        case ITEM_ARROW_ICE:
        case ITEM_ARROW_LIGHT:
        case ITEM_BOW_ARROW_FIRE:
        case ITEM_BOW_ARROW_ICE:
        case ITEM_BOW_ARROW_LIGHT:
            return SL_ARROW;
        case ITEM_NUT:
        case ITEM_BOMB:
        case ITEM_BOMBCHU:
            return SL_QUICK;
    }
    return IsBottleItem(item) ? SL_QUICK : SL_POUCH;
}

void AddToFirstFree(SoulsLoadoutKind kind, uint8_t item) {
    if (SoulsLoadout_IndexOf(kind, item) >= 0) {
        return;
    }
    for (int i = 0; i < kCap[kind]; i++) {
        if (sSlots[kind][i] == ITEM_NONE) {
            sSlots[kind][i] = item;
            return;
        }
    }
}

// One-time fill for a save without a loadout: sword -> right hand 1, shield -> left hand 1, C-button items sorted
void FillFromEquipment() {
    ClearAll();
    uint8_t sword = gSaveContext.equips.buttonItems[0];
    if (sword == ITEM_SWORD_KOKIRI || sword == ITEM_SWORD_MASTER || sword == ITEM_SWORD_BGS ||
        sword == ITEM_SWORD_KNIFE) {
        sSlots[SL_RIGHT][0] = sword;
    }
    int shield = CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD);
    if (shield >= EQUIP_VALUE_SHIELD_DEKU && shield <= EQUIP_VALUE_SHIELD_MIRROR) {
        sSlots[SL_LEFT][0] = ITEM_SHIELD_DEKU + shield - 1;
    }
    for (int b = 1; b <= 3; b++) {
        uint8_t item = gSaveContext.equips.buttonItems[b];
        if (item >= ITEM_NONE_FE) {
            continue;
        }
        if (item >= ITEM_BOW_ARROW_FIRE && item <= ITEM_BOW_ARROW_LIGHT) {
            // The bow with a magic arrow: the loadout keeps the bow (left hand) and the arrow (arrow slot)
            AddToFirstFree(SL_LEFT, ITEM_BOW);
            item = (item == ITEM_BOW_ARROW_FIRE) ? ITEM_ARROW_FIRE
                                                 : (item == ITEM_BOW_ARROW_ICE ? ITEM_ARROW_ICE : ITEM_ARROW_LIGHT);
        }
        AddToFirstFree((SoulsLoadoutKind)KindOfItem(item), item);
    }
    sFilled = true;
}

void Save(const SaveContext& saveContext, int sectionID, bool fullSave) {
    nlohmann::json lists = nlohmann::json::array();
    for (int k = 0; k < SL_KIND_COUNT; k++) {
        nlohmann::json one = nlohmann::json::array();
        for (int i = 0; i < kCap[k]; i++) {
            one.push_back(sSlots[k][i]);
        }
        lists.push_back(one);
    }
    nlohmann::json cur = nlohmann::json::array();
    for (int k = 0; k < SL_KIND_COUNT; k++) {
        cur.push_back(sCurrent[k]);
    }
    SaveManager::Instance->SaveData("lists", lists.dump());
    SaveManager::Instance->SaveData("current", cur.dump());
}

void Load() {
    ClearAll();
    std::string text;
    SaveManager::Instance->LoadData("lists", text, std::string());
    nlohmann::json lists = nlohmann::json::parse(text.empty() ? "[]" : text, nullptr, false);
    if (!lists.is_discarded() && lists.is_array()) {
        for (int k = 0; k < SL_KIND_COUNT && k < (int)lists.size(); k++) {
            if (!lists[k].is_array()) {
                continue;
            }
            for (int i = 0; i < kCap[k] && i < (int)lists[k].size(); i++) {
                if (lists[k][i].is_number_integer()) {
                    sSlots[k][i] = (uint8_t)lists[k][i].get<int>();
                }
            }
        }
        sFilled = true;
    }
    SaveManager::Instance->LoadData("current", text, std::string());
    nlohmann::json cur = nlohmann::json::parse(text.empty() ? "[]" : text, nullptr, false);
    if (!cur.is_discarded() && cur.is_array()) {
        for (int k = 0; k < SL_KIND_COUNT && k < (int)cur.size(); k++) {
            if (cur[k].is_number_integer() && cur[k].get<int>() >= 0 && cur[k].get<int>() < kCap[k]) {
                sCurrent[k] = cur[k].get<int>();
            }
        }
    }
}

void Reset(bool isDebug) {
    ClearAll();
    sFilled = false;
}

void RegisterLoadout() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    ClearAll();
    SaveManager::Instance->AddInitFunction(Reset);
    SaveManager::Instance->AddSaveFunction("soulsLoadout", 1, Save, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction("soulsLoadout", 1, Load);
    // After the file is loaded: a save without the section gets its one-time fill
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t fileNum) {
        if (!sFilled && SoulsLoadout_IsEnabled()) {
            FillFromEquipment();
        }
    });
}
} // namespace

static RegisterShipInitFunc initSoulsLoadout(RegisterLoadout);

extern "C" bool SoulsLoadout_IsEnabled(void) {
    return CVarGetInteger(CVAR_ENHANCEMENT("SoulsControls"), 0) != 0;
}
extern "C" int SoulsLoadout_Capacity(SoulsLoadoutKind kind) {
    return Valid(kind) ? kCap[kind] : 0;
}
extern "C" uint8_t SoulsLoadout_Get(SoulsLoadoutKind kind, int slot) {
    return (Valid(kind) && slot >= 0 && slot < kCap[kind]) ? sSlots[kind][slot] : (uint8_t)ITEM_NONE;
}
extern "C" int SoulsLoadout_IndexOf(SoulsLoadoutKind kind, uint8_t item) {
    if (!Valid(kind) || item == ITEM_NONE) {
        return -1;
    }
    for (int i = 0; i < kCap[kind]; i++) {
        if (sSlots[kind][i] == item) {
            return i;
        }
    }
    return -1;
}
extern "C" void SoulsLoadout_Set(SoulsLoadoutKind kind, int slot, uint8_t item) {
    if (!Valid(kind) || slot < 0 || slot >= kCap[kind]) {
        return;
    }
    int other = SoulsLoadout_IndexOf(kind, item);
    if (other >= 0) {
        sSlots[kind][other] = ITEM_NONE;
    }
    sSlots[kind][slot] = item;
}
extern "C" int SoulsLoadout_Count(SoulsLoadoutKind kind) {
    int n = 0;
    for (int i = 0; i < SoulsLoadout_Capacity(kind); i++) {
        n += sSlots[kind][i] != ITEM_NONE;
    }
    return n;
}
extern "C" int SoulsLoadout_CurrentSlot(SoulsLoadoutKind kind) {
    if (!Valid(kind)) {
        return -1;
    }
    for (int k = 0; k < kCap[kind]; k++) {
        int i = (sCurrent[kind] + k) % kCap[kind];
        if (sSlots[kind][i] != ITEM_NONE) {
            return i;
        }
    }
    return -1;
}
extern "C" uint8_t SoulsLoadout_Current(SoulsLoadoutKind kind) {
    int i = SoulsLoadout_CurrentSlot(kind);
    return i < 0 ? (uint8_t)ITEM_NONE : sSlots[kind][i];
}
extern "C" void SoulsLoadout_Cycle(SoulsLoadoutKind kind, int dir) {
    int cur = SoulsLoadout_CurrentSlot(kind);
    if (cur < 0) {
        return;
    }
    int cap = kCap[kind];
    // Prefer the next filled slot whose item can be used now (age, ownership); else the next filled slot
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 1; k <= cap; k++) {
            int i = ((cur + dir * k) % cap + cap) % cap;
            if (sSlots[kind][i] != ITEM_NONE && (pass == 1 || SoulsLoadout_CanEquip(kind, sSlots[kind][i]))) {
                if (i != cur) {
                    sCurrent[kind] = i;
                    sLastSwitch[kind] = (uint32_t)gSaveContext.ship.stats.playTimer;
                }
                return;
            }
        }
    }
}
extern "C" bool SoulsLoadout_HasCatalyst(void) {
    return SoulsLoadout_Current(SL_LEFT) == SOULS_ITEM_CATALYST || SoulsLoadout_Current(SL_RIGHT) == SOULS_ITEM_CATALYST;
}
extern "C" bool SoulsLoadout_CanEquip(SoulsLoadoutKind kind, uint8_t item) {
    if (!Valid(kind) || item == ITEM_NONE) {
        return false;
    }
    switch (kind) {
        case SL_RIGHT:
            return CanEquipRight(item);
        case SL_LEFT:
            return CanEquipLeft(item);
        case SL_SPELL:
            return (item == ITEM_DINS_FIRE || item == ITEM_FARORES_WIND || item == ITEM_NAYRUS_LOVE) &&
                   InInventory(item);
        case SL_ARROW:
            return (item == ITEM_ARROW_FIRE || item == ITEM_ARROW_ICE || item == ITEM_ARROW_LIGHT) &&
                   InInventory(item) && ItemAgeOk(item);
        case SL_QUICK:
            return CanEquipQuick(item);
        case SL_POUCH:
            return CanEquipPouch(item);
        default:
            return false;
    }
}
extern "C" uint32_t SoulsLoadout_LastUseFrame(SoulsLoadoutKind kind) {
    return Valid(kind) ? sLastUse[kind] : 0;
}
extern "C" uint32_t SoulsLoadout_LastSwitchFrame(SoulsLoadoutKind kind) {
    return Valid(kind) ? sLastSwitch[kind] : 0;
}
extern "C" void SoulsLoadout_NoteUse(SoulsLoadoutKind kind) {
    if (Valid(kind)) {
        sLastUse[kind] = (uint32_t)gSaveContext.ship.stats.playTimer;
    }
}
extern "C" bool SoulsLoadout_PouchHeld(void) {
    return sPouchHeld;
}
extern "C" void SoulsLoadout_SetPouchHeld(bool held) {
    sPouchHeld = held;
}
