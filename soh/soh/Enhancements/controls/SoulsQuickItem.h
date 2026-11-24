#pragma once

#include <libultraship/libultra/types.h>

// Souls of Harkinian: the quick item (Square). A per-save list of up to 8 items; Square uses the current one.
// The current item sits in the D-pad up slot of the game (buttonItems[4]), so the game's own item-use and HUD code
// does the work. Off unless CVAR_ENHANCEMENT("SoulsControls") is 1.
#ifdef __cplusplus
extern "C" {
#endif
// Play layout: reads the Square and D-pad state from the raw input and edits the output buttons.
void SoulsQuickItem_Play(u16 in, u16* out);
// Menu layout: remembers a Square press for the pause menu.
void SoulsQuickItem_Menu(u16 in);
// Pause menu item page: returns 1 once after a Square press, else 0.
int SoulsQuickItem_ConsumeMenuPress(void);
// Adds or removes the item (inventory slot + item as equipped). Returns 1 added, 2 removed, 3 list is full.
int SoulsQuickItem_Toggle(int slot, int item);
// 1 if this inventory slot/item is in the list.
int SoulsQuickItem_InList(int slot, int item);
// Item in the list that matches this item (used for the pause menu marker); count of entries in the list.
int SoulsQuickItem_Count(void);
int SoulsQuickItem_SlotAt(int index);
int SoulsQuickItem_CurrentIndex(void);
#ifdef __cplusplus
}; // extern "C"
#endif
