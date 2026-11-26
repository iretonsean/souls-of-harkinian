// Souls of Harkinian: loadout storage (see souls/ER-SYSTEM.md section 2).
//
// Six lists of item slots (right hand, left hand, spells, arrows, quick items, pouch) plus the active slot of each.
// Items are OoT item IDs. The lists are saved per file in the section "soulsLoadout". A save without the section is
// filled once, when it loads, from what Link has equipped (sword, C-button items), so nobody starts unarmed.
#include "soh/Enhancements/controls/SoulsLoadout.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <mutex>
#include <algorithm>
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
extern "C" {
#include "z64.h"
#include "macros.h"
#include "z64player.h"
#include "functions.h"
#include "variables.h"
// Age tables of the pause menu (z_kaleido_scope_PAL.c)
extern PlayState* gPlayState;
extern u8 gItemAgeReqs[];
extern u8 gEquipAgeReqs[][4];
void Interface_LoadItemIcon1(PlayState* play, u16 button);
}

namespace {
const int kCap[SL_KIND_COUNT] = { 3, 3, 3, 2, 8, 4 };
uint8_t sSlots[SL_KIND_COUNT][8];
int sCurrent[SL_KIND_COUNT] = {};
uint32_t sLastUse[SL_KIND_COUNT] = {}, sLastSwitch[SL_KIND_COUNT] = {};
bool sPouchHeld = false;
std::vector<uint8_t> sQuestOrder; // stones and medallions in collection order
bool sFilled = false; // the save has a loadout (saved with the "filled" flag, or filled once)
std::mutex sOrderMutex; // sQuestOrder is read by the save thread

// The player's own B / C assignments, kept while Souls controls overwrite them (restored when Souls turns off, and
// written into the save copy so the file keeps them)
bool sStashed = false;
uint8_t sStashItems[4] = {};
uint8_t sStashSlots[3] = {};
u8 sStashAge = 0;

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
bool AgeOk(int req, int item = 0) {
    bool ok = req == 9 || req == gSaveContext.linkAge;
    return GameInteractor_Should(VB_ITEM_MEETS_AGE_REQ, ok, item);
}

bool ItemAgeOk(uint8_t item) {
    return AgeOk(gItemAgeReqs[item], item);
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
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_SWORD][EQUIP_VALUE_SWORD_KOKIRI], item);
        case ITEM_SWORD_MASTER:
            return OwnsEquip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_MASTER) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_SWORD][EQUIP_VALUE_SWORD_MASTER], item);
        case ITEM_SWORD_BGS: // Giant's Knife (bgsFlag 0) or Biggoron Sword: one slot, the game decides which
            return OwnsEquip(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_SWORD][EQUIP_VALUE_SWORD_BIGGORON], item);
        case ITEM_SWORD_KNIFE: // the broken Giant's Knife: not equippable
            return false;
        case ITEM_HAMMER:
        case ITEM_STICK:
            return InInventory(item) && ItemAgeOk(item);
    }
    return false;
}

bool CanEquipLeft(uint8_t item) {
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
        case ITEM_LETTER_RUTO: // kept in a bottle slot, not a trade slot
            return HasBottleWith(item);
        case SOULS_ITEM_TOGGLE_IRON:
            return OwnsEquip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_IRON) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_BOOTS][EQUIP_VALUE_BOOTS_IRON], item);
        case SOULS_ITEM_TOGGLE_HOVER:
            return OwnsEquip(EQUIP_TYPE_BOOTS, EQUIP_VALUE_BOOTS_HOVER) &&
                   AgeOk(gEquipAgeReqs[EQUIP_TYPE_BOOTS][EQUIP_VALUE_BOOTS_HOVER], item);
    }
    // Masks and trade items: they share two inventory slots, so only the one held now counts as owned
    if (item >= ITEM_WEIRD_EGG && item <= ITEM_CLAIM_CHECK && item != ITEM_SOLD_OUT) {
        return (gSaveContext.inventory.items[SLOT_TRADE_CHILD] == item ||
                gSaveContext.inventory.items[SLOT_TRADE_ADULT] == item) &&
               ItemAgeOk(item);
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

bool IsSwordItem(uint8_t item) {
    return item == ITEM_SWORD_KOKIRI || item == ITEM_SWORD_MASTER || item == ITEM_SWORD_BGS;
}

// Put one equipped C-button item into the lists
void FillItem(uint8_t item) {
    if (item >= ITEM_NONE_FE) {
        return;
    }
    if (item >= ITEM_BOW_ARROW_FIRE && item <= ITEM_BOW_ARROW_LIGHT) {
        // The bow with a magic arrow: the loadout keeps the bow (left hand) and the arrow (arrow slot)
        AddToFirstFree(SL_LEFT, ITEM_BOW);
        item = (item == ITEM_BOW_ARROW_FIRE) ? ITEM_ARROW_FIRE
                                             : (item == ITEM_BOW_ARROW_ICE ? ITEM_ARROW_ICE : ITEM_ARROW_LIGHT);
    }
    if (item == ITEM_SWORD_KNIFE) {
        return;
    }
    SoulsLoadoutKind kind = (SoulsLoadoutKind)KindOfItem(item);
    AddToFirstFree(kind, item);
    if (kind == SL_SPELL) {
        AddToFirstFree(SL_LEFT, SOULS_ITEM_CATALYST); // a spell needs the catalyst in a hand
    }
}

// One-time fill for a save without a loadout: the swords and C-button items of both ages (the current age first;
// its equips are the vanilla ones kept in the stash, the other age's are the ones the game keeps for the age switch).
// The shield stays the equipped shield.
void FillFromEquipment() {
    ClearAll();
    for (int pass = 0; pass < 2; pass++) {
        bool current = pass == 0;
        uint8_t items[4];
        if (current) {
            for (int i = 0; i < 4; i++) {
                items[i] = sStashed ? sStashItems[i] : gSaveContext.equips.buttonItems[i];
            }
        } else {
            const ItemEquips& other = gSaveContext.linkAge == LINK_AGE_CHILD ? gSaveContext.adultEquips
                                                                              : gSaveContext.childEquips;
            for (int i = 0; i < 4; i++) {
                items[i] = other.buttonItems[i];
            }
        }
        if (IsSwordItem(items[0])) {
            AddToFirstFree(SL_RIGHT, items[0]);
        }
        for (int b = 1; b <= 3; b++) {
            FillItem(items[b]);
        }
    }
    sFilled = true;
}

bool BIsTemporary() {
    uint8_t b = gSaveContext.equips.buttonItems[0];
    bool normal = b == ITEM_SWORD_KOKIRI || b == ITEM_SWORD_MASTER || b == ITEM_SWORD_BGS || b == ITEM_SWORD_KNIFE ||
                  b == ITEM_NONE;
    if (!normal) {
        return true; // bow, slingshot, bombchu, fishing rod
    }
    if (gPlayState == nullptr) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    return gPlayState->shootingGalleryStatus > 0 || gSaveContext.minigameState == 1 ||
           gPlayState->bombchuBowlingStatus != 0 || (player != nullptr && (player->stateFlags1 & PLAYER_STATE1_ON_HORSE)) ||
           (gPlayState->sceneNum == SCENE_FISHING_POND && gPlayState->interfaceCtx.unk_260 != 0);
}

// An inventory-backed button assignment that is still valid (the bottle may be empty now, the item may be gone)
void SanitizeVanilla(uint8_t* items, uint8_t* slots) {
    for (int i = 0; i < 3; i++) {
        uint8_t item = items[i + 1], slot = slots[i];
        if (slot == SLOT_NONE || slot >= 24) {
            if (item < ITEM_NONE_FE) {
                items[i + 1] = ITEM_NONE;
            }
            slots[i] = SLOT_NONE;
            continue;
        }
        uint8_t content = gSaveContext.inventory.items[slot];
        if (content == ITEM_NONE) {
            items[i + 1] = ITEM_NONE;
            slots[i] = SLOT_NONE;
        } else if (slot >= SLOT_BOTTLE_1 && slot <= SLOT_BOTTLE_4) {
            items[i + 1] = content;
        }
    }
}

void CaptureVanilla() {
    if (BIsTemporary()) {
        return; // try again when the minigame is over
    }
    for (int i = 0; i < 4; i++) {
        sStashItems[i] = gSaveContext.equips.buttonItems[i];
    }
    for (int i = 0; i < 3; i++) {
        sStashSlots[i] = gSaveContext.equips.cButtonSlots[i];
    }
    sStashAge = gSaveContext.linkAge;
    sStashed = true;
}

void RestoreVanilla() {
    if (BIsTemporary()) {
        return;
    }
    if (sStashAge == gSaveContext.linkAge) {
        SanitizeVanilla(sStashItems, sStashSlots);
        for (int i = 0; i < 3; i++) {
            gSaveContext.equips.buttonItems[i + 1] = sStashItems[i + 1];
            gSaveContext.equips.cButtonSlots[i] = sStashSlots[i];
            if (gPlayState != nullptr) {
                Interface_LoadItemIcon1(gPlayState, i + 1);
            }
        }
        uint8_t sword = sStashItems[0];
        if (IsSwordItem(sword) && gSaveContext.equips.buttonItems[0] != sword && gPlayState != nullptr) {
            Player* player = GET_PLAYER(gPlayState);
            if (player == nullptr || player->meleeWeaponState != 0 ||
                (player->heldItemAction >= PLAYER_IA_SWORD_MASTER && player->heldItemAction <= PLAYER_IA_SWORD_BIGGORON)) {
                return; // the sword is drawn: wait until it is away
            }
            int value = sword == ITEM_SWORD_KOKIRI ? EQUIP_VALUE_SWORD_KOKIRI
                                                   : (sword == ITEM_SWORD_MASTER ? EQUIP_VALUE_SWORD_MASTER
                                                                                 : EQUIP_VALUE_SWORD_BIGGORON);
            gSaveContext.equips.buttonItems[0] = sword;
            Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, value);
            Interface_LoadItemIcon1(gPlayState, 0);
            Player_SetEquipmentData(gPlayState, player);
        }
    }
    sStashed = false;
}

const uint8_t kOrderItems[] = { QUEST_KOKIRI_EMERALD, QUEST_GORON_RUBY,      QUEST_ZORA_SAPPHIRE,
                                QUEST_MEDALLION_FOREST, QUEST_MEDALLION_FIRE,   QUEST_MEDALLION_WATER,
                                QUEST_MEDALLION_SPIRIT, QUEST_MEDALLION_SHADOW, QUEST_MEDALLION_LIGHT };

// Add stones / medallions that the file has but the order list does not (new ones go to the end)
void UpdateQuestOrder() {
    std::lock_guard<std::mutex> lock(sOrderMutex);
    for (uint8_t q : kOrderItems) {
        if (CHECK_QUEST_ITEM(q)) {
            if (std::find(sQuestOrder.begin(), sQuestOrder.end(), q) == sQuestOrder.end()) {
                sQuestOrder.push_back(q);
            }
        } else {
            sQuestOrder.erase(std::remove(sQuestOrder.begin(), sQuestOrder.end(), q), sQuestOrder.end());
        }
    }
}

void Save(const SaveContext& saveContext, int sectionID, bool fullSave) {
    std::vector<uint8_t> order;
    {
        std::lock_guard<std::mutex> lock(sOrderMutex);
        order = sQuestOrder;
    }
    SaveManager::Instance->SaveData("questOrder", nlohmann::json(order).dump());
    SaveManager::Instance->SaveData("filled", (int)sFilled);
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
    SaveManager::Instance->LoadData("questOrder", text, std::string());
    nlohmann::json order = nlohmann::json::parse(text.empty() ? "[]" : text, nullptr, false);
    std::lock_guard<std::mutex> lock(sOrderMutex);
    sQuestOrder.clear();
    if (!order.is_discarded() && order.is_array()) {
        for (auto& q : order) {
            if (q.is_number_integer()) {
                sQuestOrder.push_back((uint8_t)q.get<int>());
            }
        }
    }
    SaveManager::Instance->LoadData("lists", text, std::string());
    nlohmann::json lists = nlohmann::json::parse(text.empty() ? "[]" : text, nullptr, false);
    if (!lists.is_discarded() && lists.is_array()) {
        for (int k = 0; k < SL_KIND_COUNT && k < (int)lists.size(); k++) {
            if (!lists[k].is_array()) {
                continue;
            }
            for (int i = 0; i < kCap[k] && i < (int)lists[k].size(); i++) {
                if (lists[k][i].is_number_integer()) {
                    int v = lists[k][i].get<int>();
                    // Unknown ids (damaged or newer save) are cleared: icons and tables are indexed by the id
                    if (v == ITEM_NONE || (v >= 0 && v <= ITEM_SWORD_KNIFE) || (v >= SOULS_ITEM_CATALYST && v <= SOULS_ITEM_EPONA)) {
                        sSlots[k][i] = (uint8_t)v;
                    }
                }
            }
        }
    }
    for (int i = 0; i < kCap[SL_LEFT]; i++) { // one copy per item: a copy in both hands stays in the right hand
        if (sSlots[SL_LEFT][i] != ITEM_NONE && SoulsLoadout_IndexOf(SL_RIGHT, sSlots[SL_LEFT][i]) >= 0) {
            sSlots[SL_LEFT][i] = ITEM_NONE;
        }
    }
    int filled = 0;
    SaveManager::Instance->LoadData("filled", filled, 0);
    sFilled = filled != 0; // lists without the flag (old versions, or saved before the fill) get the fill again
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
    {
        std::lock_guard<std::mutex> lock(sOrderMutex);
        sQuestOrder.clear();
    }
    sFilled = false;
    sStashed = false;
    for (int k = 0; k < SL_KIND_COUNT; k++) {
        sLastUse[k] = sLastSwitch[k] = 0;
    }
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
    // Every frame in play: watch the stones and medallions (also with Souls controls off); with Souls controls on,
    // keep the player's own button items, and give a save without a loadout its one-time fill (also when the switch
    // is turned on in the middle of a session); with them off, give the button items back.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (gPlayState == nullptr) {
            return;
        }
        UpdateQuestOrder();
        if (SoulsLoadout_IsEnabled()) {
            if (sStashed && sStashAge != gSaveContext.linkAge) {
                sStashed = false; // age change: the game loaded the other age's buttons
            }
            if (!sStashed) {
                CaptureVanilla();
            }
            if (sStashed && !sFilled) {
                FillFromEquipment();
            }
        } else if (sStashed) {
            RestoreVanilla();
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
    // One copy per item: a hand item leaves the other hand when it goes into this one
    int inOther = SoulsLoadout_OtherHandIndexOf(kind, item);
    if (inOther >= 0) {
        sSlots[kind == SL_RIGHT ? SL_LEFT : SL_RIGHT][inOther] = ITEM_NONE;
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
                    sLastSwitch[kind] = gPlayState != nullptr ? gPlayState->gameplayFrames : 0;
                }
                return;
            }
        }
    }
}
extern "C" int SoulsLoadout_QuestOrder(uint8_t* out, int max) {
    int n = 0;
    std::lock_guard<std::mutex> lock(sOrderMutex);
    for (uint8_t q : sQuestOrder) {
        if (n < max) {
            out[n++] = q;
        }
    }
    return n;
}
extern "C" bool SoulsLoadout_HasCatalyst(void) {
    return SoulsLoadout_Current(SL_LEFT) == SOULS_ITEM_CATALYST || SoulsLoadout_Current(SL_RIGHT) == SOULS_ITEM_CATALYST;
}
extern "C" int SoulsLoadout_OtherHandIndexOf(SoulsLoadoutKind kind, uint8_t item) {
    if (kind != SL_RIGHT && kind != SL_LEFT) {
        return -1;
    }
    return SoulsLoadout_IndexOf(kind == SL_RIGHT ? SL_LEFT : SL_RIGHT, item);
}
extern "C" bool SoulsLoadout_CanEquip(SoulsLoadoutKind kind, uint8_t item) {
    if (!Valid(kind) || item == ITEM_NONE) {
        return false;
    }
    switch (kind) {
        case SL_RIGHT:
        case SL_LEFT: // both hands take every armament
            return CanEquipRight(item) || CanEquipLeft(item);
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
        sLastUse[kind] = gPlayState != nullptr ? gPlayState->gameplayFrames : 0;
    }
}
extern "C" bool SoulsLoadout_PouchHeld(void) {
    return sPouchHeld;
}
extern "C" void SoulsLoadout_SetPouchHeld(bool held) {
    sPouchHeld = held;
}

extern "C" bool SoulsLoadout_ButtonItemsStashed(void) {
    return sStashed;
}
extern "C" bool SoulsLoadout_TemporaryB(void) {
    return BIsTemporary();
}
// The save file keeps the player's own C buttons, not the ones the loadout puts there each frame
extern "C" void SoulsLoadout_PatchSaveCopy(void* saveContext) {
    SaveContext* copy = (SaveContext*)saveContext;
    if (copy == nullptr || !sStashed || !SoulsLoadout_IsEnabled() || sStashAge != copy->linkAge) {
        return;
    }
    uint8_t items[4], slots[3];
    for (int i = 0; i < 4; i++) {
        items[i] = sStashItems[i];
    }
    for (int i = 0; i < 3; i++) {
        slots[i] = sStashSlots[i];
    }
    SanitizeVanilla(items, slots);
    for (int i = 0; i < 3; i++) {
        copy->equips.buttonItems[i + 1] = items[i + 1];
        copy->equips.cButtonSlots[i] = slots[i];
    }
}
