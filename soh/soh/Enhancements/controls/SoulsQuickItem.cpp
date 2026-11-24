// Souls of Harkinian: quick item on Square (like the Elden Ring quick item).
//
//  - Pause menu, item page: Square adds the highlighted item to the list; Square again removes it. Up to 8 items.
//  - In play: tap Square to use the current item. Hold Square and press D-pad down (next) or up (previous) to switch.
//    While Square is held, the D-pad does not use C items.
//  - The list is saved per save file (section "soulsQuickItem"). Official SoH ignores the section.
//
// The current item is copied into the D-pad up slot of the game (gSaveContext.equips.buttonItems[4] and
// cButtonSlots[3]). This needs "D-pad equips" on, which this file turns on while SoulsControls is on. Square in play is
// turned into D-pad up (BTN_DUP) for the game's own item-button code, so age rules, bottles, arrows, the ocarina and the
// HUD icon all work as for a C button.
//
// A tap is a press and release within 6 input ticks (0.3 s at 20 fps). The item is used on release, so that a hold
// with a D-pad press can switch items without using the current one. A hold longer than that uses the item for as
// long as Square is held (bow, slingshot, hookshot aiming).
#include <libultraship/bridge/consolevariablebridge.h>
#include <nlohmann/json.hpp>
#include <vector>
#include "SoulsInput.h"
#include "SoulsQuickItem.h"
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
extern u8 gSlotAgeReqs[];
}

#define CVAR_SOULS_CONTROLS CVAR_ENHANCEMENT("SoulsControls")
#define DPAD_BITS (BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT)
#define QI_MAX 8
#define QI_AGE_REQ_NONE 9 // AGE_REQ_NONE in z_kaleido_scope.h
#define QI_TAP_TICKS 6

struct QuickEntry {
    u8 slot; // inventory slot
    u8 item; // item as it sits on a button (bow + magic arrow variants differ from the slot's item)
};

static std::vector<QuickEntry> sList;
static int sCur = 0;

// input state
static bool sSquareWasDown = false;
static u16 sPrevDpad = 0;
static int sHoldTicks = 0;
static bool sCycled = false;
static bool sActivated = false;
static int sTapLeft = 0;
static int sMenuPressTicks = 0;

static bool IsBottleSlot(int slot) {
    return slot >= SLOT_BOTTLE_1 && slot <= SLOT_BOTTLE_4;
}

// The item that Square would use for this entry now, or ITEM_NONE if it cannot be used (item gone, wrong age).
static u8 ResolveEntry(const QuickEntry& e) {
    if (e.slot >= 24) {
        return ITEM_NONE;
    }
    u8 held = gSaveContext.inventory.items[e.slot];
    if (held == ITEM_NONE) {
        return ITEM_NONE;
    }
    if (gSlotAgeReqs[e.slot] != QI_AGE_REQ_NONE && gSlotAgeReqs[e.slot] != gSaveContext.linkAge) {
        return ITEM_NONE;
    }
    return IsBottleSlot(e.slot) ? held : e.item;
}

// Copy the current entry into the D-pad up slot of the game
static void SyncSlot() {
    u8 item = ITEM_NONE;
    u8 slot = SLOT_NONE;
    if (!sList.empty()) {
        if (sCur >= (int)sList.size()) {
            sCur = 0;
        }
        item = ResolveEntry(sList[sCur]);
        if (item != ITEM_NONE) {
            slot = sList[sCur].slot;
        }
    }
    gSaveContext.equips.buttonItems[4] = item;
    gSaveContext.equips.cButtonSlots[3] = slot;
    // The other D-pad slots are not used
    for (int i = 5; i < 8; i++) {
        gSaveContext.equips.buttonItems[i] = ITEM_NONE;
    }
}

static void Cycle(int dir) {
    int n = (int)sList.size();
    if (n == 0) {
        return;
    }
    for (int tries = 0; tries < n; tries++) {
        sCur = ((sCur + dir) % n + n) % n;
        if (ResolveEntry(sList[sCur]) != ITEM_NONE) {
            break;
        }
    }
    SyncSlot();
}

static bool EntryMatches(const QuickEntry& e, int slot, int item) {
    return e.slot == slot && (IsBottleSlot(slot) || e.item == item);
}

extern "C" int SoulsQuickItem_InList(int slot, int item) {
    for (auto& e : sList) {
        if (EntryMatches(e, slot, item)) {
            return 1;
        }
    }
    return 0;
}

extern "C" int SoulsQuickItem_Count(void) {
    return (int)sList.size();
}

extern "C" int SoulsQuickItem_SlotAt(int index) {
    return (index >= 0 && index < (int)sList.size()) ? sList[index].slot : -1;
}

extern "C" int SoulsQuickItem_CurrentIndex(void) {
    return sCur;
}

extern "C" int SoulsQuickItem_Toggle(int slot, int item) {
    for (size_t i = 0; i < sList.size(); i++) {
        if (EntryMatches(sList[i], slot, item)) {
            sList.erase(sList.begin() + i);
            if (sCur > (int)i || sCur >= (int)sList.size()) {
                sCur = sCur > 0 ? sCur - 1 : 0;
            }
            SyncSlot();
            return 2;
        }
    }
    if ((int)sList.size() >= QI_MAX) {
        return 3;
    }
    sList.push_back({ (u8)slot, (u8)item });
    if (sList.size() == 1) {
        sCur = 0;
    }
    SyncSlot();
    return 1;
}

extern "C" int SoulsQuickItem_ConsumeMenuPress(void) {
    if (sMenuPressTicks > 0) {
        sMenuPressTicks = 0;
        return 1;
    }
    return 0;
}

extern "C" void SoulsQuickItem_Menu(u16 in) {
    bool square = (in & BTN_CLEFT) != 0;
    if (sMenuPressTicks > 0) {
        sMenuPressTicks--;
    }
    if (square && !sSquareWasDown) {
        sMenuPressTicks = 3;
    }
    sSquareWasDown = square;
    sPrevDpad = in & DPAD_BITS;
    sHoldTicks = 0;
    sCycled = sActivated = false;
    sTapLeft = 0;
}

extern "C" void SoulsQuickItem_Play(u16 in, u16* out) {
    // The game needs "D-pad equips" for the extra item slot
    if (!CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0)) {
        CVarSetInteger(CVAR_ENHANCEMENT("DpadEquips"), 1);
    }
    bool square = (in & BTN_CLEFT) != 0;
    u16 dpad = in & DPAD_BITS;
    u16 pressed = dpad & ~sPrevDpad;
    sPrevDpad = dpad;
    sMenuPressTicks = 0;

    if (square) {
        if (!sSquareWasDown) {
            sHoldTicks = 0;
            sCycled = sActivated = false;
        }
        sHoldTicks++;
        // The D-pad does not use C items while Square is held
        *out &= ~(BTN_CUP | BTN_CDOWN | BTN_CLEFT | BTN_CRIGHT);
        if (!sActivated) {
            if (pressed & BTN_DDOWN) {
                Cycle(1);
                sCycled = true;
            } else if (pressed & BTN_DUP) {
                Cycle(-1);
                sCycled = true;
            }
            if (!sCycled && sHoldTicks > QI_TAP_TICKS) {
                sActivated = true; // long hold: use the item while held
            }
        }
        if (sActivated) {
            *out |= BTN_DUP;
        }
    } else if (sSquareWasDown) {
        if (!sActivated && !sCycled && sHoldTicks <= QI_TAP_TICKS) {
            sTapLeft = 2; // tap: use the item
        }
        sCycled = sActivated = false;
        sHoldTicks = 0;
    }
    if (sTapLeft > 0) {
        *out |= BTN_DUP;
        sTapLeft--;
    }
    sSquareWasDown = square;
}

// MARK: - Save file

static void QuickItem_Save(const SaveContext& saveContext, int sectionID, bool fullSave) {
    nlohmann::json list = nlohmann::json::array();
    for (auto& e : sList) {
        list.push_back({ e.slot, e.item });
    }
    SaveManager::Instance->SaveData("list", list.dump());
    SaveManager::Instance->SaveData("current", sCur);
}

static void QuickItem_Load() {
    std::string text;
    SaveManager::Instance->LoadData("list", text, std::string());
    nlohmann::json list = nlohmann::json::parse(text.empty() ? "[]" : text, nullptr, false);
    sList.clear();
    if (!list.is_discarded() && list.is_array()) {
        for (auto& e : list) {
            if (sList.size() < QI_MAX && e.is_array() && e.size() == 2 && e[0].is_number_integer() &&
                e[1].is_number_integer() && e[0].get<int>() >= 0 && e[0].get<int>() < 24) {
                sList.push_back({ (u8)e[0].get<int>(), (u8)e[1].get<int>() });
            }
        }
    }
    SaveManager::Instance->LoadData("current", sCur, 0);
    if (sCur < 0 || sCur >= (int)sList.size()) {
        sCur = 0;
    }
}

static void QuickItem_Reset(bool isDebug) {
    sList.clear();
    sCur = 0;
}

static void RegisterSoulsQuickItem() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    SaveManager::Instance->AddInitFunction(QuickItem_Reset);
    SaveManager::Instance->AddSaveFunction("soulsQuickItem", 1, QuickItem_Save, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction("soulsQuickItem", 1, QuickItem_Load);
}

static void RegisterSoulsQuickItemSync() {
    COND_HOOK(OnPlayerUpdate, CVarGetInteger(CVAR_SOULS_CONTROLS, 0), []() {
        if (gPlayState != nullptr) {
            SyncSlot();
        }
    });
}

static RegisterShipInitFunc initSoulsQuickItem(RegisterSoulsQuickItem);
static RegisterShipInitFunc initSoulsQuickItemSync(RegisterSoulsQuickItemSync, { CVAR_SOULS_CONTROLS });
