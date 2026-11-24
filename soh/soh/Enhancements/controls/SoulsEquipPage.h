#pragma once
// Souls of Harkinian: the Equipment page of the Elden Ring system (souls/ER-SYSTEM.md, section 4).
// It replaces OoT's equipment subscreen in the pause menu while the Souls controls are on.
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

// The Souls page replaces the page (Souls controls on).
bool SoulsEquipPage_Active(void);
// The page is on screen and still (not turning, no item animation): the Souls page draws and takes input.
bool SoulsEquipPage_Shown(struct PlayState* play);
// Once per game frame from KaleidoScope_Update (before the pause state machine): input and the page state.
void SoulsEquipPage_Update(struct PlayState* play);
// True when Circle must not open OoT's save prompt (the page uses it for "Done" / "Back").
bool SoulsEquipPage_ConsumesBack(struct PlayState* play);
// True while a list is open: the page-turn buttons stay quiet.
bool SoulsEquipPage_BlocksPageTurn(struct PlayState* play);
// Every frame at the end of the pause draw: the 2D page over the pause screen.
void SoulsEquipPage_Draw(struct PlayState* play);

#ifdef __cplusplus
}
#endif
