#pragma once
// Souls of Harkinian: the Elden Ring style loadout (see souls/ER-SYSTEM.md).
// One shared interface for the input layer (phase 1), the HUD (phase 1) and the Equipment page (phase 2).
// Items are OoT ItemIDs (ITEM_*), except SOULS_ITEM_CATALYST. ITEM_NONE (0xFF) = empty slot.
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SL_RIGHT,  // 3 slots: swords, hammer, Deku Stick
    SL_LEFT,   // 3 slots: slingshot, bow, hookshot, longshot, boomerang, catalyst
    SL_SPELL,  // 3 slots: Din's Fire, Farore's Wind, Nayru's Love
    SL_ARROW,  // 2 slots: fire, ice, light arrows
    SL_QUICK,  // 8 slots: consumables + Epona
    SL_POUCH,  // 4 slots: tools (ocarinas, lens, boot toggles, beans, masks, trade items)
    SL_KIND_COUNT
} SoulsLoadoutKind;

// Not a real OoT item: the catalyst ("Fairy Magic") that lets a hand cast the current spell.
#define SOULS_ITEM_CATALYST 0xF0
// Pouch-only toggles for boots (the real boots stay OoT equipment on the Equipment page).
#define SOULS_ITEM_TOGGLE_IRON 0xF1
#define SOULS_ITEM_TOGGLE_HOVER 0xF2
// Quick item that calls Epona (phase 3).
#define SOULS_ITEM_EPONA 0xF3

bool SoulsLoadout_IsEnabled(void);                              // Souls controls on (same switch as SoulsInput)
int SoulsLoadout_Capacity(SoulsLoadoutKind kind);               // 3, 3, 3, 2, 8, 4
uint8_t SoulsLoadout_Get(SoulsLoadoutKind kind, int slot);      // item in a slot, ITEM_NONE if empty
void SoulsLoadout_Set(SoulsLoadoutKind kind, int slot, uint8_t item); // put (or clear with ITEM_NONE); removes the item from
                                                                // any other slot of the same kind first
int SoulsLoadout_Count(SoulsLoadoutKind kind);                  // filled slots
int SoulsLoadout_IndexOf(SoulsLoadoutKind kind, uint8_t item);  // slot of an item, -1 if not equipped there
int SoulsLoadout_CurrentSlot(SoulsLoadoutKind kind);            // the active slot (skips empty ones), -1 if all empty
uint8_t SoulsLoadout_Current(SoulsLoadoutKind kind);            // item in the active slot, ITEM_NONE if all empty
void SoulsLoadout_Cycle(SoulsLoadoutKind kind, int dir);        // next / previous filled slot (D-pad)
bool SoulsLoadout_HasCatalyst(void);                            // the catalyst is the current item of either hand
bool SoulsLoadout_CanEquip(SoulsLoadoutKind kind, uint8_t item);// owned + right age + right kind (game logic)

// For the HUD: frame counter (gameplay frames) of the last use / switch of a kind, for the slot flash.
uint32_t SoulsLoadout_LastUseFrame(SoulsLoadoutKind kind);
uint32_t SoulsLoadout_LastSwitchFrame(SoulsLoadoutKind kind);
void SoulsLoadout_NoteUse(SoulsLoadoutKind kind);               // the input layer calls this when it uses a slot
bool SoulsLoadout_PouchHeld(void);                              // Triangle held in play: the HUD shows the pouch (input layer sets it)
void SoulsLoadout_SetPouchHeld(bool held);

// For the HUD: the spirit stones (child) and medallions (adult) in the order the player got them. Writes up to max
// quest item ids (QUEST_KOKIRI_EMERALD, QUEST_MEDALLION_FOREST, ...) to out and returns how many. Saved per file.
int SoulsLoadout_QuestOrder(uint8_t* out, int max);

#ifdef __cplusplus
}
#endif
