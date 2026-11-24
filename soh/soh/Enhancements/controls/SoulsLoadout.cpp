// Souls of Harkinian: loadout storage. MINIMAL version from the lead so every branch compiles; the input branch
// (phase 1) replaces it with the full one: save section "soulsLoadout", the one-time fill for old saves,
// SoulsLoadout_CanEquip with real game logic, and the frame counters.
#include "soh/Enhancements/controls/SoulsLoadout.h"
#include <libultraship/bridge/consolevariablebridge.h>
extern "C" {
#include "z64.h"
#include "variables.h"
}

namespace {
const int kCap[SL_KIND_COUNT] = { 3, 3, 3, 2, 8, 4 };
uint8_t sSlots[SL_KIND_COUNT][8] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
};
int sCurrent[SL_KIND_COUNT] = {};
uint32_t sLastUse[SL_KIND_COUNT] = {}, sLastSwitch[SL_KIND_COUNT] = {};
bool Valid(int kind) {
    return kind >= 0 && kind < SL_KIND_COUNT;
}
} // namespace

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
    for (int k = 1; k <= cap; k++) {
        int i = ((cur + dir * k) % cap + cap) % cap;
        if (sSlots[kind][i] != ITEM_NONE) {
            sCurrent[kind] = i;
            break;
        }
    }
    sLastSwitch[kind] = (uint32_t)gSaveContext.ship.stats.playTimer;
}
extern "C" bool SoulsLoadout_HasCatalyst(void) {
    return SoulsLoadout_Current(SL_LEFT) == SOULS_ITEM_CATALYST || SoulsLoadout_Current(SL_RIGHT) == SOULS_ITEM_CATALYST;
}
extern "C" bool SoulsLoadout_CanEquip(SoulsLoadoutKind kind, uint8_t item) {
    return Valid(kind) && item != ITEM_NONE; // placeholder: the input branch adds owned / age / kind checks
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
